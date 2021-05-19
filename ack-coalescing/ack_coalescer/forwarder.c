#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>

#include "common.h"

char *tundevname = "tun0";

void options(int argc, char **argv)
{
	int opt;

	while ((opt = getopt(argc, argv, "i:")) != -1) {
		switch (opt) {
			case 'i':
				tundevname = optarg;
				break;
			default:
				fprintf(stderr, "Usage: %s -i iface\n", argv[0]);
				exit(-1);
		}
	}
}

int packet_loop(int tunfd, int pipefd)
{
	unsigned char buf[PIPE_PKT_SIZE];
	unsigned int sender_len;
	ssize_t len;

	while (1) {
		len = read(pipefd, &sender_len, sizeof(unsigned int));
		if (len < sizeof(unsigned int)) {
			perror("len read");
			exit(-1);
		}
		if (sender_len > PIPE_PKT_SIZE) {
			fprintf(stderr, "Too long packet %u, abort\n", sender_len);
			exit(-1);
		}
		len = read(pipefd, &buf, sender_len);
		if (len < sender_len) {
			perror("partial packet read");
			exit(-1);
		}
		len = write(tunfd, &buf, len);
		if (len < sender_len) {
			perror("partial tun write");
			exit(-1);
		}
	}
}

int main(int argc, char **argv)
{
	int tunfd, pipefd;

	options(argc, argv);
	tunfd = create_tun(tundevname);
	pipefd = get_pipe(PIPE_NAME, tundevname);
	packet_loop(tunfd, pipefd);

	close(tunfd);
	close(pipefd);

	return 0;
}
