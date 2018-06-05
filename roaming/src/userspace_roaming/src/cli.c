#include <stdio.h>  
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>  
#include <sys/socket.h>  
#include <sys/un.h>  
#include <fcntl.h>
#include <errno.h>

#include "log.h"
#include "usock.h"
#include "msg_proto.h"
#include "cli.h"
#include "tools.h"

static void ip6Tomac(char *ip6addr)
{
	struct in6_addr sin6;
	u8 macbuf[ETH_DEC_LEN];

	if(NULL == ip6addr || !strlen(ip6addr))
		goto failed;

	if(inet_pton(AF_INET6, ip6addr, &sin6) != 1)
		goto failed;

	_ip6Tomac(&sin6, macbuf);

	printf("%02x%02x%02x%02x%02x%02x\n", MAC_STR(macbuf));
	return;

failed:
	printf("Failed\n");
}

int send_cmd(message *msg)
{
	int sockfd = -1, len;
	struct sockaddr_un srv_addr;  

	//creat unix socket  
	sockfd = socket(PF_UNIX, SOCK_STREAM, 0);  
	if(sockfd < 0) {
		LOG(LOG_ERR, "Cannot create communication socket: [%d]%s", errno, strerror(errno));  
		return -1;
	}

	memset(&srv_addr, 0, sizeof(srv_addr));
	srv_addr.sun_family = AF_UNIX;
	strcpy(srv_addr.sun_path, UNIX_DOMAIN);

	if(connect(sockfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) == -1) {
		LOG(LOG_ERR, "Connect failed: [%d]%s", errno, strerror(errno));
		goto errout;
	}

	len = msg->len;
	msg->len = htonl(len);
	if(send(sockfd, (char *)msg, len, 0) == -1) {
		LOG(LOG_ERR, "Send failed: [%d]%s", errno, strerror(errno));
		goto errout;
	}

	// read response
	close(sockfd);
	return 0;

errout:
	close(sockfd);
	return -1;
}

/*
 * argv: -c arg1 arg2
 *
 * */
int do_client(int argc, char **argv)
{  
	char sbuf[512] = {'\0'};
	char command[32] = {'\0'};

	if(argc < 2) {
		return -1;
	}

	if(!strcmp("ip6Tomac", argv[1])) {
		if(3 == argc)
			ip6Tomac(argv[2]);
		return 0;
	} else if(!strcmp("help", argv[1])) {
		printf("./roaming -c help\n");
		printf("\tenable|disable: enable|disable kmod_zyc_roaming\n");
		printf("\tsync: sync client and router info with filesystem\n");
		printf("\thelp: print this message\n");

		return 0;
	}

	strncpy(command, argv[1], strlen(argv[1]));

	message *msg;
	cli_cmd_t *cmd;

	memset(sbuf, 0, sizeof(sbuf));

	msg = (message *)sbuf;
	msg->type = UMSG_CLI_CMD;
	msg->len = sizeof(message);
	cmd = (cli_cmd_t *)(msg + 1);

	strncpy(cmd->cmd, command, 32);
	msg->len += strlen(command);

	return send_cmd(msg);
}
