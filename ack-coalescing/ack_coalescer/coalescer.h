#ifndef COALESCER_H
#define COALESCER_H

#include <linux/types.h>
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
};

typedef void (*strategy_func)(struct flow *fl);
strategy_func get_strategy(char *strategy);


#endif
