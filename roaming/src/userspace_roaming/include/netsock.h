#ifndef __NETSOCK_H__
#define __NETSOCK_H__

#include "roaming.h"

struct sock_info {
	int sockfd;
	int cnts;
	int scope_id[4];
	char interfaces[4][IFNAMSIZ];
};

void sock_port_init(int port);
int mcast_sock_send(char *sbuf, int bufsiz);
int mcast_sock_open(struct sock_info *info);
void mcast_sock_close(struct sock_info *info);

int ucast_sock_send(char *ipaddr, char *sbuf, int bufsiz);
#endif
