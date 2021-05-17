#ifndef COALESCER_H
#define COALESCER_H

#include <stdbool.h>

#include <linux/types.h>
#include <sys/time.h>
#include <unistd.h>

struct packet {
        ssize_t len;
	struct iphdr *ip;
	struct tcphdr *tcp;
	struct packet *next;

	unsigned char data[0];
};

void send_packet(struct packet *pkt);

struct flow {
        __be32 saddr;
        __be32 daddr;
        __be16 sport;
        __be16 dport;

        struct packet *pkt;
        unsigned int pkt_count;
        struct timeval timeout;
};

typedef void (*strategy_func)(struct flow *fl, bool timeout);
strategy_func get_strategy(char *strategy);

struct event {
        struct flow *fl;
        struct event *next;
};

struct event *get_next_event();

#define USECS_IN_SEC 1000000

long long init_period_packets;

#endif
