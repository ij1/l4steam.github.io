#include "coalescer.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#include "common.h"

#define PKT_SIZE PIPE_PKT_SIZE

#define FLOW_HASH_SIZE	512
struct flow **flow_hash = NULL;

int pipefd = -1;

char *tundevname = "tun0";
strategy_func strategy = NULL;
long long init_period_packets = 0;

void options(int argc, char **argv)
{
	int opt;

	while ((opt = getopt(argc, argv, "i:p:s:")) != -1) {
		switch (opt) {
			case 'i':
				tundevname = optarg;
				break;
			case 's':
				strategy = get_strategy(optarg);
				if (strategy == NULL) {
					fprintf(stderr, "Invalid strategy: %s\n", optarg);
					exit(-1);
				}
				break;
			case 'p':
				init_period_packets = atoll(optarg);
				break;
			default:
				fprintf(stderr, "Usage: %s -i iface\n", argv[0]);
				exit(-1);
		}
	}

	if (strategy == NULL) {
		fprintf(stderr, "Strategy required\n");
		exit(-1);
	}
}

void send_packet(struct packet *pkt)
{
	ssize_t len;

	len = write(pipefd, &(pkt->len), sizeof(unsigned int));
	if (len < sizeof(unsigned int)) {
		perror("len write");
		exit(-1);
	}
	len = write(pipefd, &(pkt->data), pkt->len);
	if (len < pkt->len) {
		perror("pkt write");
		exit(-1);
	}
}

struct flow *get_flow(struct packet *pkt)
{
	struct flow *fl;
	int flow_idx;
	bool wrap = false;
	
	flow_idx = pkt->ip->saddr ^ pkt->ip->daddr ^
		   (pkt->tcp->source << 16) ^ pkt->tcp->dest;
	flow_idx %= FLOW_HASH_SIZE;

	while (1) {
		fl = flow_hash[flow_idx];
		if (fl == NULL) {
			fl = malloc(sizeof(struct flow));

			fl->saddr = pkt->ip->saddr;
			fl->daddr = pkt->ip->daddr;
			fl->sport = pkt->tcp->source;
			fl->dport = pkt->tcp->dest;
			fl->pkt = NULL;
			fl->pkt_count = 0;
			fl->timeout.tv_sec = 0;
			fl->timeout.tv_usec = 0;

			flow_hash[flow_idx] = fl;
			break;
		}
		if (pkt->tcp->source == fl->sport &&
		    pkt->tcp->dest == fl->dport &&
		    pkt->ip->saddr == fl->saddr &&
		    pkt->ip->daddr == fl->daddr)
			break;

		flow_idx++;
		if (flow_idx >= FLOW_HASH_SIZE) {
			flow_idx = 0;
			if (wrap) {
				fprintf(stderr, "Increase flow hash table size\n");
				exit(1);
			}
			wrap = true;
		}
	}

	return fl;
}

void handle_packet(struct packet *pkt)
{
	struct flow *fl = get_flow(pkt);

	pkt->next = fl->pkt;
	fl->pkt = pkt;
	fl->pkt_count++;

	strategy(fl, false);
}

int packet_loop(int tunfd)
{
	struct packet *pkt;
	ssize_t len;
	fd_set fdset, exceptset;
	int res;
	struct timeval tv;
	struct timeval *tvptr;
	struct event *e;

	while (1) {
		FD_ZERO(&fdset);
		FD_ZERO(&exceptset);
		FD_SET(tunfd, &fdset);
		FD_SET(tunfd, &exceptset);

		e = get_next_event();
		if (e != NULL) {
			if (gettimeofday(&tv, NULL)) {
				perror("gettimeofday");
				exit(-1);
			}
			tv.tv_sec = e->fl->timeout.tv_sec - tv.tv_sec;
			tv.tv_usec = e->fl->timeout.tv_usec - tv.tv_usec;
			if (tv.tv_usec < 0) {
				tv.tv_usec += USECS_IN_SEC;
				tv.tv_sec--;
			}
			if (tv.tv_sec < 0) {
				tv.tv_sec = 0;
				tv.tv_usec = 0;
			}
			tvptr = &tv;
		} else {
			tvptr = NULL;
		}

		res = select(tunfd + 1, &fdset, NULL, &exceptset, tvptr);
		if (res < 0) {
			perror("select");
			exit(1);
		}

		if (e != NULL) {
			if (gettimeofday(&tv, NULL)) {
				perror("gettimeofday");
				exit(-1);
			}
			if (e->fl->timeout.tv_sec < tv.tv_sec ||
			    (e->fl->timeout.tv_sec == tv.tv_sec &&
			     e->fl->timeout.tv_usec < tv.tv_usec)) {
				strategy(e->fl, true);
			}
		}

		if (!FD_ISSET(tunfd, &fdset)) {
			continue;
		}

		pkt = malloc(sizeof(*pkt) + PKT_SIZE);
		len = read(tunfd, &(pkt->data), PKT_SIZE);
		if (len < 0) {
			perror("read");
			exit(-1);
		}
		pkt->len = len;

		if (pkt->len < sizeof(struct iphdr)) {
			fprintf(stderr, "Less than IP header\n");
			exit(-1);
		}

		pkt->ip = (struct iphdr *)&(pkt->data);
		if (pkt->len < (pkt->ip->ihl << 2)) {
			fprintf(stderr, "No IP header\n");
			exit(-1);
		}
		if (pkt->ip->protocol != IPPROTO_TCP) {
			fprintf(stderr, "Not a TCP packet: %u\n", pkt->ip->protocol);
			free(pkt);
			continue;
		}
		pkt->tcp = (struct tcphdr *)&(pkt->data[pkt->ip->ihl << 2]);
		if (pkt->len < (pkt->ip->ihl << 2) + (pkt->tcp->th_off << 2)) {
			fprintf(stderr, "No TCP header\n");
			exit(-1);
		}

		handle_packet(pkt);
	}
}

int main(int argc, char **argv)
{
	int tunfd;
	int flags;

	flow_hash = calloc(FLOW_HASH_SIZE, sizeof(struct flow *));
	if (flow_hash == NULL) {
		fprintf(stderr, "flow_hash alloc failed!\n");
		exit(-1);
	}
	memset(flow_hash, 0, FLOW_HASH_SIZE * sizeof(struct flow *));

	options(argc, argv);
	tunfd = create_tun(tundevname);
	pipefd = get_pipe(PIPE_NAME);

	if ((flags = fcntl(tunfd, F_GETFL, 0)) < 0) {
		perror("fcntl read");
		exit(-1);
	}
	if (fcntl(tunfd, F_SETFL, flags | O_NONBLOCK)) {
		perror("fcntl write");
		exit(-1);
	}
	
	packet_loop(tunfd);

	close(tunfd);
	close(pipefd);

	return 0;
}
