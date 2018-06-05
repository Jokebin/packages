#ifndef __USOCK_H__
#define __USOCK_H__

#define UNIX_DOMAIN "/tmp/UNIX.domain"  

int get_unixfd();
int init_unix();
void close_unix();
int unix_recv(int fd, int *rcvfd, char **rbuf);

#endif
