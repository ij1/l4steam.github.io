#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "coalescer.h"

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
	send_packet(fl->pkt);
	free_packets(fl);
}

#define GRANT_DELAY 20000

void ackreqgrant(struct flow *fl, bool timeout)
{
	if (timeout) {
		del_event_for_flow(fl);
		send_packet(fl->pkt);
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
	{"reqgrant", ackreqgrant}
};

strategy_func get_strategy(char *strategy)
{
	int i;

	for (i = 0; i < sizeof(strategies) / sizeof(strategy); i++) {
		if (!strcmp(strategies[i].name, strategy))
			return strategies[i].func;
	}

	return NULL;
}
