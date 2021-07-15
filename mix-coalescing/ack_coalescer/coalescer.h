#ifndef COALESCER_H
#define COALESCER_H

#include <stdbool.h>

#include <linux/types.h>
#include <sys/time.h>
#include <unistd.h>

struct packet {
        unsigned int len;
	struct iphdr *ip;
	struct tcphdr *tcp;
	struct packet *next;

	unsigned char data[0];
};

void send_packet(struct packet *pkt);

struct queue {
        struct packet *pkt;
        unsigned int pkt_count;
        struct timeval timeout;
};

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
