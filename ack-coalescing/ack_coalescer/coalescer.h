#ifndef COALESCER_H
#define COALESCER_H

#include <stdbool.h>

#include <linux/types.h>
#include <sys/time.h>
#include <unistd.h>

struct packet {
	struct packet *next;
	struct packet *prev;
        unsigned int len;
	struct iphdr *ip;
	struct tcphdr *tcp;

	unsigned char data[0];
};

void send_packet(struct packet *pkt);

struct queue {
        struct packet pkt;
        unsigned int pkt_count;
        struct timeval timeout;
};

extern struct queue queue;

static inline void plist_insert_after(struct packet *pkt, struct packet *after)
{
        pkt->prev = after;
        pkt->next = after->next;
        after->next->prev = pkt;
        after->next = pkt;
}

static inline void plist_remove_packet(struct packet *pkt)
{
        pkt->next->prev = pkt->prev;
        pkt->prev->next = pkt->next;
        pkt->next = NULL;
        pkt->prev = NULL;
}

typedef void (*strategy_func)(struct queue *q, bool timeout);
strategy_func get_strategy(char *strategy);

struct event {
        struct queue *queue;
        struct event *next;
};

struct event *get_next_event();

#define USECS_IN_SEC 1000000

long long init_period_packets;

#endif
