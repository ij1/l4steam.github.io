#include <stdlib.h>
#include <string.h>

#include "coalescer.h"

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

void immediate(struct flow *fl)
{
	send_packet(fl->pkt);
	free(fl->pkt);
	free_packets(fl);
}

void halfdrop(struct flow *fl)
{
	if (fl->pkt_count < 2)
		return;
	send_packet(fl->pkt);
	free_packets(fl);
}

struct strategy {
	char *name;
	strategy_func func;
};

struct strategy strategies[] = {
	{"immediate", immediate},
	{"halfdrop", halfdrop},
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
