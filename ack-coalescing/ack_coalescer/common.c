#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/stat.h>

#include <linux/if.h>
#include <linux/if_tun.h>

#include "common.h"

int create_tun(char *tundevname)
{
	int tunfd;
	int err;
	struct ifreq ifr;

	tunfd = open("/dev/net/tun", O_RDWR);
	if (tunfd < 0) {
		perror("open tun control");
		exit(-1);
	}

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
	strncpy(ifr.ifr_name, tundevname, IFNAMSIZ);

	err = ioctl(tunfd, TUNSETIFF, &ifr);

	if (err < 0) {
		perror("ioctl");
		close(tunfd);
		exit(-1);
	}

	return tunfd;
}

int get_pipe(char *name)
{
	int fd;
	int res;

	res = mkfifo(name, 0600);
	if (res < 0 && errno != EEXIST) {
		perror("mkfifo");
		exit(-1);
	}

	fd = open(name, O_RDWR);
	if (fd < 0) {
		perror("open pipe");
		exit(-1);
	}

	return fd;	
}
