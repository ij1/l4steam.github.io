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

struct queue queue;

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

struct queue *get_queue(struct packet *pkt)
{
	return &queue;
}

void handle_packet(struct packet *pkt)
{
	struct queue *q = get_queue(pkt);

	pkt->next = q->pkt;
	q->pkt = pkt;
	q->pkt_count++;

	strategy(q, false);
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
			tv.tv_sec = e->queue->timeout.tv_sec - tv.tv_sec;
			tv.tv_usec = e->queue->timeout.tv_usec - tv.tv_usec;
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
			if (e->queue->timeout.tv_sec < tv.tv_sec ||
			    (e->queue->timeout.tv_sec == tv.tv_sec &&
			     e->queue->timeout.tv_usec < tv.tv_usec)) {
				strategy(e->queue, true);
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

	options(argc, argv);
	tunfd = create_tun(tundevname);
	pipefd = get_pipe(PIPE_NAME, tundevname);

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
