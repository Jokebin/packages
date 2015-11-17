#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define RESP_LEN 204800

struct timeval timeout = {
	.tv_sec = 100,
	.tv_usec = 0,
};


int new_sock(char *r_ipaddr, int r_port, char *l_ipaddr, int l_port)
{
	if(r_ipaddr == NULL) {
		printf("No valid remote address\n");
		return -1;
	}

	struct sockaddr_in r_addr, l_addr;
	int addr_len = sizeof(struct sockaddr_in);
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	int ret = -1;

	if(sockfd == -1) {
		perror("socket");
		return -1;
	}

	memset(&r_addr, 0, addr_len);
	memset(&l_addr, 0, addr_len);
	
	r_addr.sin_family = AF_INET;
	r_addr.sin_port = htons(r_port);
	r_addr.sin_addr.s_addr = inet_addr(r_ipaddr);

	int reuse = 1;
	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int))) {
		perror("setsockopt");
		goto errout;
	}

	if(l_ipaddr != NULL) {
		l_addr.sin_family = AF_INET;
		//l_addr.sin_port = htons(l_port);
		l_addr.sin_addr.s_addr = inet_addr(l_ipaddr);
		if((ret = bind(sockfd, (struct sockaddr *)&l_addr, addr_len)) == -1 ) {
			perror("bind");
			goto errout;
		}
	}

	if(connect(sockfd, (struct sockaddr *)&r_addr, sizeof(r_addr))) {
		perror("connect");
		goto errout;
	}	

	return sockfd;

errout:
	close(sockfd);
	return -1;
}

int send_request(int fd, char *request, int len)
{
	int ret = -1;

//	if(request && send(fd, request, len, 0) == -1) {
//		perror("send");
//		return -1;
//	}
	if(NULL == request){
		return -1;
	}
	ret = write(fd, request, len);
	printf("write : %d len: %d\n", ret, len);
	perror("send");
	return ret;

}

int read_response(int *fd, char *buf, int len)
{
	int count = 0;
	int ret = 1;
	char tmpbuf[BUFSIZ] = {0};
	fd_set rdset;
	int maxfd;

	timeout.tv_sec = 5;
	memset(buf, 0, len);
	while(ret > 0) {

		memset(tmpbuf, 0, BUFSIZ);
	//	ret = recv(*fd, tmpbuf, BUFSIZ, 0);	
		ret = read(*fd, tmpbuf, BUFSIZ);
		perror("read");
		printf("read:%d\n", ret);
		printf("tmpbuf:%s\n", tmpbuf);
	//	if(ret == EINTR || ret == EAGAIN)
	//		continue;
	//	if(ret <= 0)
	//		break;
	//	count += ret;
	//	if(count <= len - 1) {
	//		strcat(buf, tmpbuf);
	//	} else {
	//		printf("The response is too big, would you set a larger buf to receive it\n");
	//		break;
	//	}
	}
	//if(count > 0)
	//	buf[count] = '\0';
	return ret;

}

int main(char argc, char **argv)
{
	int fd1, fd2, fd3;
	char strRequest[BUFSIZ] = {0};
	char strResponse[RESP_LEN] = {0};

	char *post_data = "authenticator=apAuthLocalUser"\
					   "&submit%5BapAuthLocalUserconnect%5D=%E8%BF%9E%E6%8E%A5"\
					   "&apAuthLocalUser%5Busername%5D=xbin"\
					   "&apAuthLocalUser%5Bpassword%5D=12345678";

	fd1 = new_sock("192.168.1.207", 80, NULL, 0);
	sprintf(strRequest, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",\
			"/authpuppy/web/login", "192.168.1.207:80");
	send_request(fd1, strRequest, strlen(strRequest));
	read_response(&fd1, strResponse, sizeof(strResponse));
//	printf("########################## %s\n", strResponse);
//	printf("########################## fd = %d\n", fd1);
//	close(fd1);
//	fd1 = new_sock("192.168.1.105", 80, "192.168.1.56", 0);
//	send_request(fd1, strRequest, strlen(strRequest));
//	read_response(&fd1, strResponse, sizeof(strResponse));
//	printf("########################## %s\n", strResponse);
//	printf("########################## fd = %d\n", fd1);
	
	close(fd1);
	fd1 = new_sock("192.168.1.105", 80, "192.168.1.56", 0);
	sprintf(strRequest, "POST %s HTTP/1.1\r\n"\
						"Host: %s\r\n"\
						"Connection: keep-alive\r\n"\
						"Content-Type: application/x-www-form-urlencoded\r\n"\
						"Content-Length: %ld\r\n"\
						"\r\n"\
						"%s",\
						"/authpuppy/web/login", "192.168.1.207:80", strlen(post_data), post_data);
	printf("11111111111111111111111111111\n");
	send_request(fd1, strRequest, strlen(strRequest));
	read_response(&fd1, strResponse, sizeof(strResponse));

	printf("########################## %s\n", strResponse);
	printf("########################## fd = %d\n", fd1);

	sprintf(strRequest, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",\
			"/authpuppy/web/", "192.168.1.207:80");
	printf("2222222222222222222222222222\n");
	send_request(fd1, strRequest, strlen(strRequest));
	read_response(&fd1, strResponse, sizeof(strResponse));
	if(fd1 != -1)
		close(fd1);

	return 0;
errout:
	return -1;
}
