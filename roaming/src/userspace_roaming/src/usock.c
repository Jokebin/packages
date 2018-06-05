#include <stdio.h>  
#include <unistd.h>
#include <sys/types.h>  
#include <sys/socket.h>  
#include <sys/un.h>   

#include "usock.h"
#include "log.h"

static char recv_buf[1024];   

int unix_fd;  

int get_unixfd()
{
	return unix_fd;
}

int init_unix()
{
	int ret;
	int i;
	int len;
	struct sockaddr_un srv_addr;

	unix_fd = socket(PF_UNIX,SOCK_STREAM,0);

	if(unix_fd < 0) {
		perror("cannot create communication socket");
		return -1;
	}

	//set server addr_param  
	srv_addr.sun_family = AF_UNIX;
	strncpy(srv_addr.sun_path, UNIX_DOMAIN, sizeof(srv_addr.sun_path) - 1);
	unlink(UNIX_DOMAIN);  

	//bind sockfd & addr
	ret=bind(unix_fd, (struct sockaddr*)&srv_addr, sizeof(srv_addr));
	if(ret == -1) {
		perror("cannot bind server socket");
		close(unix_fd);
		unlink(UNIX_DOMAIN);
		return -1;
	}

	//listen sockfd
	ret=listen(unix_fd, 1);
	if(ret == -1) {
		perror("cannot listen the client connect request");
		close(unix_fd);
		unlink(UNIX_DOMAIN);
		return -1;
	}

	return unix_fd;
}

int unix_recv(int fd, int *rcvfd, char **rbuf)
{
	int ret;
	int i;

	//have connect request use accept  
	*rcvfd = accept(fd, NULL, NULL);  
	if(*rcvfd < 0) {
		LOG(LOG_ERR, "cannot accept client connect request");

		close(fd);
		unlink(UNIX_DOMAIN);
		return -1;
	}

	*rbuf = recv_buf;
	memset(recv_buf, 0, 1024);  
	ret = read(*rcvfd, recv_buf, sizeof(recv_buf));  

	if(ret < 0) {
		LOG(LOG_ERR, "An error occurred while reading");
		close(unix_fd);  
		unlink(UNIX_DOMAIN);  
	}

	return ret;
}

void close_unix()
{
	if(unix_fd != -1) {
		close(unix_fd);  
		unlink(UNIX_DOMAIN);  
	}
}
