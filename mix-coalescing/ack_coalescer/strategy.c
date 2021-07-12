#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "coalescer.h"

int accecn_aware = 0;

static struct event event_q = { .fl = NULL, .next = NULL };

static long long packet_count = 0;

void add_event_for_flow(struct flow *fl)
{
	struct event *ep = &event_q;
	struct event *newe = malloc(sizeof(struct event));

	if (newe == NULL) {
		fprintf(stderr, "event malloc\n");
		exit(-1);
	}

	newe->fl = fl;
	newe->next = NULL;

	while (1) {
		if (ep->next == NULL)
			break;

		if (newe->fl->timeout.tv_sec < ep->next->fl->timeout.tv_sec ||
		    (newe->fl->timeout.tv_sec == ep->next->fl->timeout.tv_sec &&
		     newe->fl->timeout.tv_usec < ep->next->fl->timeout.tv_usec)) {
			break;
		}
		ep = ep->next;
	}
	newe->next = ep->next;
	ep->next = newe;
}

void del_event_for_flow(struct flow *fl)
{
	struct event *ep = &event_q;
	struct event *e = NULL;

	while (1) {
		if (ep->next == NULL)
			break;

		if (ep->next->fl == fl) {
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

void free_packets(struct flow *fl)
{
	struct packet *pkt;

	while (fl->pkt != NULL) {
		pkt = fl->pkt->next;
		free(fl->pkt);
		fl->pkt = pkt;
	}
	fl->pkt_count = 0;
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

void accecn_aware_send(struct flow *fl)
{
	struct packet *pkt;
	struct packet *sendlist[3];
	unsigned int sendcnt = 0;
	unsigned int accecncnt = 0;
	int i;

	if (fl->pkt_count <= 0)
		return;

	pkt = fl->pkt;
	sendlist[sendcnt++] = pkt;

	while (pkt != NULL) {
		if (packet_has_accecn_option(pkt)) {
			if (pkt != fl->pkt)
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

void immediate(struct flow *fl, bool timeout)
{
	send_packet(fl->pkt);
	free_packets(fl);
}

void halfdrop(struct flow *fl, bool timeout)
{
	packet_count++;
	if (packet_count > init_period_packets && fl->pkt_count < 2)
		return;

	if (accecn_aware) {
		accecn_aware_send(fl);
	} else {
		send_packet(fl->pkt);
	}
	free_packets(fl);
}

void every16(struct flow *fl, bool timeout)
{
	packet_count++;
	if (packet_count > init_period_packets && fl->pkt_count < 16)
		return;

	if (accecn_aware) {
		accecn_aware_send(fl);
	} else {
		send_packet(fl->pkt);
	}
	free_packets(fl);
}

#define GRANT_DELAY 4250

void ackreqgrant(struct flow *fl, bool timeout)
{
	if (timeout) {
		del_event_for_flow(fl);
		if (accecn_aware) {
			accecn_aware_send(fl);
		} else {
			send_packet(fl->pkt);
		}
		free_packets(fl);

		fl->timeout.tv_usec = 0;
		fl->timeout.tv_sec = 0;
		return;
	}

	packet_count++;
	if (packet_count < init_period_packets) {
		send_packet(fl->pkt);
		free_packets(fl);
		return;
	}

	if (fl->pkt_count == 1) {
		if (gettimeofday(&(fl->timeout), NULL)) {
			perror("gettimeofday");
			exit(1);
		}
		fl->timeout.tv_usec += GRANT_DELAY;
		if (fl->timeout.tv_usec >= USECS_IN_SEC) {
			fl->timeout.tv_usec -= USECS_IN_SEC;
			fl->timeout.tv_sec++;
		}

		add_event_for_flow(fl);
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
