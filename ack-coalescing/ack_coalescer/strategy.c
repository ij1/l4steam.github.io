#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "coalescer.h"

int accecn_aware = 0;

static struct event event_q = { .queue = NULL, .next = NULL };

static long long packet_count = 0;

void add_event_for_queue(struct queue *q)
{
	struct event *ep = &event_q;
	struct event *newe = malloc(sizeof(struct event));

	if (newe == NULL) {
		fprintf(stderr, "event malloc\n");
		exit(-1);
	}

	newe->queue = q;
	newe->next = NULL;

	while (1) {
		if (ep->next == NULL)
			break;

		if (newe->queue->timeout.tv_sec < ep->next->queue->timeout.tv_sec ||
		    (newe->queue->timeout.tv_sec == ep->next->queue->timeout.tv_sec &&
		     newe->queue->timeout.tv_usec < ep->next->queue->timeout.tv_usec)) {
			break;
		}
		ep = ep->next;
	}
	newe->next = ep->next;
	ep->next = newe;
}

void del_event_for_queue(struct queue *q)
{
	struct event *ep = &event_q;
	struct event *e = NULL;

	while (1) {
		if (ep->next == NULL)
			break;

		if (ep->next->queue == q) {
			e = ep->next;
			ep->next = ep->next->next;
			e->next = NULL;
			break;
		}
		ep = ep->next;
	}

	if (e != NULL)
		free(e);
}

struct event *get_next_event()
{
	return event_q.next;
}

void free_packets(struct queue *q)
{
	struct packet *pkt;

	while (q->pkt != NULL) {
		pkt = q->pkt->next;
		free(q->pkt);
		q->pkt = pkt;
	}
	q->pkt_count = 0;
}

#define TCPOPT_EXP              254     /* Experimental */
/* Magic number to be after the option value for sharing TCP
 * experimental options. See draft-ietf-tcpm-experimental-options-00.txt
 */
#define TCPOPT_ACCECN0_MAGIC    0xACC0
#define TCPOPT_ACCECN1_MAGIC    0xACC1


bool packet_has_accecn_option(struct packet *pkt)
{
	int optspace = (pkt->tcp->th_off << 2) - sizeof(struct tcphdr);
	unsigned char *opt = (unsigned char *)(pkt->tcp) + sizeof(struct tcphdr);
	uint16_t magic;

	while (optspace > 0) {
		switch (*opt) {
		case TCPOPT_EOL:
			return false;
		case TCPOPT_NOP:
			opt++;
			optspace--;
			break;
		case TCPOPT_EXP:
			if (optspace < 4)
				return false;
			magic = ntohs(*((uint16_t *)&opt[2]));
			if (magic == TCPOPT_ACCECN0_MAGIC ||
			    magic == TCPOPT_ACCECN1_MAGIC) {
				if (optspace - opt[1] < 0)
					return false;
				return true;
			}

		default:
			if (optspace < 2)
				return false;
			optspace -= opt[1];
			opt += opt[1];
			break;
		}
	}
	return false;
}

void accecn_aware_send(struct queue *q)
{
	struct packet *pkt;
	struct packet *sendlist[3];
	unsigned int sendcnt = 0;
	unsigned int accecncnt = 0;
	int i;

	if (q->pkt_count <= 0)
		return;

	pkt = q->pkt;
	sendlist[sendcnt++] = pkt;

	while (pkt != NULL) {
		if (packet_has_accecn_option(pkt)) {
			if (pkt != q->pkt)
				sendlist[sendcnt++] = pkt;

			accecncnt++;
			if (accecncnt >= 2)
				break;
		}
		pkt = pkt->next;
	}

	for (i = sendcnt - 1; i >= 0; i--)
		send_packet(sendlist[i]);
}

void immediate(struct queue *q, bool timeout)
{
	send_packet(q->pkt);
	free_packets(q);
}

void halfdrop(struct queue *q, bool timeout)
{
	packet_count++;
	if (packet_count > init_period_packets && q->pkt_count < 2)
		return;

	if (accecn_aware) {
		accecn_aware_send(q);
	} else {
		send_packet(q->pkt);
	}
	free_packets(q);
}

void every16(struct queue *q, bool timeout)
{
	packet_count++;
	if (packet_count > init_period_packets && q->pkt_count < 16)
		return;

	if (accecn_aware) {
		accecn_aware_send(q);
	} else {
		send_packet(q->pkt);
	}
	free_packets(q);
}

#define GRANT_DELAY 4250

void ackreqgrant(struct queue *q, bool timeout)
{
	if (timeout) {
		del_event_for_queue(q);
		if (accecn_aware) {
			accecn_aware_send(q);
		} else {
			send_packet(q->pkt);
		}
		free_packets(q);

		q->timeout.tv_usec = 0;
		q->timeout.tv_sec = 0;
		return;
	}

	packet_count++;
	if (packet_count < init_period_packets) {
		send_packet(q->pkt);
		free_packets(q);
		return;
	}

	if (q->pkt_count == 1) {
		if (gettimeofday(&(q->timeout), NULL)) {
			perror("gettimeofday");
			exit(1);
		}
		q->timeout.tv_usec += GRANT_DELAY;
		if (q->timeout.tv_usec >= USECS_IN_SEC) {
			q->timeout.tv_usec -= USECS_IN_SEC;
			q->timeout.tv_sec++;
		}

		add_event_for_queue(q);
	}
}

struct strategy {
	char *name;
	strategy_func func;
};

struct strategy strategies[] = {
	{"immediate", immediate},
	{"halfdrop", halfdrop},
	{"every16", every16},
	{"reqgrant", ackreqgrant},
	{"accecn-aware-reqgrant", ackreqgrant}
};

#define ACCECN_AWARE "accecn-aware"

strategy_func get_strategy(char *strategy)
{
	int i;

	accecn_aware = 0;
	if (!strncmp(strategy, ACCECN_AWARE, strlen(ACCECN_AWARE)))
		accecn_aware = 1;

	for (i = 0; i < sizeof(strategies) / sizeof(strategy); i++) {
		if (!strcmp(strategies[i].name, strategy))
			return strategies[i].func;
	}

	return NULL;
}
