#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include "send_thread.h"
#include "service.h"

void send_handler(void *arg);

void send_thread_func(mydata *arg)
{
	pthread_t send_handler_thread;
	if(pthread_create(&send_handler_thread, NULL, (void*)send_handler, (void*)arg) < 0 )
	{
		perror("pthread_create failed");
		return;
	}
	pthread_detach(send_handler_thread);
}

void send_handler(void *arg)
{
	mydata *send_msg = (mydata*)arg;
	assert(send_msg != NULL);

	int ret = -1;
	int send_socket = -1; 
	char buf[128];
	struct sockaddr_in cli_addr;
	
	cli_addr.sin_family = AF_INET;
	cli_addr.sin_port = htons(CLIENT_PORT);
	if(inet_pton(AF_INET, send_msg->router_ip, &cli_addr.sin_addr) <= 0)
	{
		perror("inet_pton failed");
		goto retur;
	}

	send_socket = socket(AF_INET, SOCK_STREAM, 0);

	if(send_socket < 0)
	{
		perror("socket error");
		goto retur;
	}

	printf("start to connect!\n");
	if(connect(send_socket, (struct sockaddr*)&cli_addr, sizeof(cli_addr)) != 0)
	{
		perror("connect to client router failed");
		goto error;
	}	
	
	printf("start to send!\n");
	memset(buf, 0, sizeof(buf));
	sprintf(buf, "%s%s", send_msg->comm, send_msg->content);
	printf("%s\n",buf);

	if(send(send_socket, buf, sizeof(buf), 0) < 0)
	{
		perror("send failed");
		goto error;
	}
	printf("send to router___%s\n", send_msg->content);
error:
	close(send_socket);
retur:
	return;
}
