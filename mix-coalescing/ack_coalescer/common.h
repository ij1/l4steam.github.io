#ifndef COMMON_H
#define COMMON_H

#define PIPE_NAME "ack_coal_pipe_%s"

#define PIPE_PKT_SIZE 1500

int create_tun(char *tundevname);
int get_pipe(char *name, char *tundevname);

#endif
