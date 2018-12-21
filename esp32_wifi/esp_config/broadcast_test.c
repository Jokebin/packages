#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/in.h>
#include "esp_config.h"

void broadcast_recv(void)
{
	int opt = -1;
	int ret = -1;
	int sockfd = -1;
	struct sockaddr_in saddr;
	struct sockaddr_in raddr;
	socklen_t sklen = 0;

	struct esp_msg *msg;
	char rbuf[128];

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sockfd < 0) {
		perror("socket");
		exit(1);
	}

	opt = 1;
	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		perror("setsockopt");
		close(sockfd);
		exit(1);
	}

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(SERVER_PORT);
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	sklen = sizeof(saddr);
	
	if(bind(sockfd, (struct sockaddr *)&saddr, sklen) < 0) {
		perror("bind");
		close(sockfd);
		exit(1);
	}

	while(1) {
		ret = recvfrom(sockfd, rbuf, sizeof(rbuf)-1, 0, (struct sockaddr *)&raddr, &sklen);
		if(-1 == ret) {
			perror("recvfrom");
			break;
		}

		if(0 == ret)
			break;

		rbuf[ret] = '\0';
		msg = (struct esp_msg *)rbuf;
		printf("serport: %d, serip: %s, ssid: %s, psword: %s, magic_id: %d\n",
				ntohs(msg->serport), msg->serip, msg->ssid, msg->psword, ntohl(msg->magic_id));
	}

	close(sockfd);
}

int main(void)
{
	broadcast_recv();

	return 0;
}
