#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>
#include <assert.h>
#include <pthread.h>

#define BUFSIZE 1024
#define RESP_LEN 409600
#define TstIp "180.97.33.107"
#define WebPort 80

#define TSTREQ "GET /index.html HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n"
#define VIATUAL_PREFIX "vth"

#define HTTP_POST "POST %s HTTP/1.1\r\nHost: %s\r\nConnection: %s\r\n\r\n"
#define HTTP_GET "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: %s\r\n\r\n"
#define LOCATION "Location: http://"
#define	Addr_len sizeof(struct sockaddr_in)
#define OFFSET 17

#define AuthIp "125.95.190.162"
#define AuthPort 50002
#define AuthUrl "125.95.190.162:50002"

#define RouteUrl "172.30.22.1:2060"

#define PORT_OFFSET 1000
#define DEF_LOCAL_IP_PREFIX "172.30.22."
#define DEF_LOCAL_IP_START 254
#define DEF_TST_USER "1000k"
#define DEF_TST_PASSWD "12345678"

int cli_num = 1;
int request_num = 10;
int link_state = 0; //long or short link for http, 1 for long, 0 for short
char connection[16];
char gw_port[7];
char gw_address[16];
char gw_id[13];

typedef struct __cli_info{
	int cli_id;
	char name[128];
	char passwd[32];
	char ipaddr[16];
	pthread_t cli_pth;
	struct __cli_info *next;
}_cli_info;

_cli_info *cli_list = NULL;

/* return the Location from the html-buf*/
int get_location(char *html, char *buf)
{
	char *local = NULL;
	int localen = 0;
	int pos = 0;
	
	if(html && buf && (local = strstr(html, LOCATION))) {
		if(strlen(local) > OFFSET) {
			local += OFFSET;
			while(local[pos] != '\r' && local[pos] != '\n')
				pos++;

			if(pos != 0) {
				local[pos] = '\0';
				if((local = strstr(local, "/")) != NULL) {
					strcpy(buf, local);
					return 0;
				}
			}
		}
	}

	return -1;
}

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
		if(l_port)
			l_addr.sin_port = htons(l_port);
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

/* send msg to remote server*/
int send_msg(int fd, char *msg)
{
	int ret = 0;
	char strRequest[BUFSIZE] = {0};

	strcpy(strRequest, msg);
	if ((ret = write(fd, strRequest, strlen(msg))) == -1) {
		perror("write");
	}

	return ret;
}

int read_response(int *fd, char *response, int len, int flag)
{
	int ret = -1;
	int count = 0;
	char tmp[1024];

	memset(response, 0, len);
	
	while(1) {
		memset(tmp, 0, 1024);
		ret = recv(*fd, tmp, 1024, 0);
		if(ret == EINTR)
			continue;
		if(ret <= 0)
			break;
		count += ret;
		if(count <= len - 1)
			strcat(response, tmp);
		else {
			if(ret > 0)
				printf("Reading is unfinished, receive buf is too small! count=%d ret=%d!\n", count, ret);
			break;
		}
	}

	if(count != 0) {
		response[count]='\0';
		//printf("###########response\n%s\n", response);
	}
	/* if http is short-link, close the socket*/
	if(!flag) {
		close(*fd);
		*fd = -1;
	}

	return count?count:ret;
}

/* send a http request to the router, and then access the
 * gw_id,gw_port,gw_address from the response
 */
int get_route_info()
{
	struct sockaddr_in test_addr;
	int test_sockfd = -1;
	char strResponse[RESP_LEN] = {0};
	char strLocaltion[BUFSIZE] = {0};
	char *q, *p = NULL;

	test_sockfd = new_sock(TstIp, WebPort, NULL, 0);
	send_msg(test_sockfd, TSTREQ);
	read_response(&test_sockfd, strResponse, RESP_LEN, 0);

	if(!get_location(strResponse, strLocaltion)) {
		//get gw_address from response, when gw_address is null, and the len of gw_address cannt smaller than 15 
		if((q = strstr(strLocaltion, "gw_address")) && (p = strstr(q, "&")) && p - q >= 17) {
			memset(gw_address, 0, sizeof(gw_address));
			strncpy(gw_address, q+11, p-q-11);
		} else
			return -1;

		//get gw_id from response, only when gw_id is null, and gw_id cannt be null 
		if((q = strstr(strLocaltion, "gw_id")) && (p = strstr(q, "&")) && p - q > 7) {
			memset(gw_id, 0, sizeof(gw_id));
			strncpy(gw_id, q+6, p-q-6);
		} else
			return -1;

		//get gw_port from response, when gw_port is zero, and the len of gw_port cannt smaller than 1
		if((q = strstr(strLocaltion, "gw_port")) && (p = strstr(q, "&")) && p - q > 9) {
			memset(gw_port, 0, sizeof(gw_port));
			strncpy(gw_port, q+8, p-q-8);
		} else
			return -1;
	}

	return 0;
}

/*
 * create the key-value str for http POST
 */
int post_key_value_str(char *var_buf, _cli_info *client)
{
	if(!var_buf || !client)	
		return -1;
	sprintf(var_buf, "gw_id=%s&gw_address=%s&gw_port=%s&authenticator=apAuthLocalUser&submit"\
			"%%5BapAuthLocalUserconnect%%5D=%%E8%%BF%%9E%%E6%%8E%%A5&apAuthLocalUser%%5Busername%%5D"\
			"=%s&apAuthLocalUser%%5Bpassword%%5D=%s", gw_id, gw_address, gw_port, client->name, client->passwd);

	return 0;
}

/*
 * the main routine of a client
 */
void *init_client(void *arg)
{
	_cli_info *client = (_cli_info *)arg;
	int remote_sockfd, auth_sockfd, route_sockfd;
	struct timeval start_time, end_time;
	char strRequest[BUFSIZE] = {0};
	char strResponse[RESP_LEN] = {0};
	char strLocaltion[BUFSIZE] = {0};
	char strTmp[BUFSIZE] = {0};
	double spend_time;
	int i = 0;
	
	for(; i < request_num; i++) {
		/* Init a socket for http*/
		if((remote_sockfd = new_sock(TstIp, WebPort, client->ipaddr, 0)) == -1)
			goto error;
		/* Send the request */
		send_msg(remote_sockfd, TSTREQ);
		read_response(&remote_sockfd, strResponse, RESP_LEN, 0);
	}
	gettimeofday(&start_time, NULL);
	/* Read in the response */
	if(!get_location(strResponse, strLocaltion)) {
		if((auth_sockfd = new_sock(AuthIp, AuthPort, client->ipaddr, 0)) == -1)
			goto error;
		sprintf(strRequest, HTTP_GET, strLocaltion, AuthUrl, connection);
		send_msg(auth_sockfd, strRequest);
		//read_response(&auth_sockfd, strResponse, RESP_LEN, link_state);
		//printf("######################%s\n", strResponse);
		
		post_key_value_str(strTmp, client);
		sprintf(strRequest, "POST %s HTTP/1.1\r\n"\
					"Host: 125.95.190.162:50002\r\n"\
					"Content-Type: application/x-www-form-urlencoded\r\n"\
					"Connection: %s\r\n"\
					"Content-Length: %d\r\n"\
					"\r\n"\
					"%s", strLocaltion, connection, (int)strlen(strTmp), strTmp);

		if((!link_state || auth_sockfd <= 0) && (auth_sockfd = new_sock(AuthIp, AuthPort, client->ipaddr, 0)) == -1)
			goto error;
		send_msg(auth_sockfd, strRequest);
		read_response(&auth_sockfd, strResponse, RESP_LEN, link_state);
		//printf("######################%s\n", strResponse);
		/* send the token to wifidog*/
		if(!get_location(strResponse, strLocaltion)) {
			route_sockfd = new_sock(gw_address, atoi(gw_port), client->ipaddr, 0);
			sprintf(strRequest, HTTP_GET, strLocaltion, RouteUrl, "close");
			send_msg(route_sockfd, strRequest);
			read_response(&route_sockfd, strResponse, RESP_LEN, 0);

			/* access the protal-web*/
			if(!get_location(strResponse, strLocaltion)) {
				do {
					if((!link_state || auth_sockfd <= 0) && (auth_sockfd = new_sock(AuthIp, AuthPort, client->ipaddr, 0)) == -1)
						goto error;
					sprintf(strRequest, HTTP_GET, strLocaltion, AuthUrl, "close");
					send_msg(auth_sockfd, strRequest);
				} while(read_response(&auth_sockfd, strResponse, RESP_LEN, 0) <= 0);// read the final response
					printf("######################%s\n", strResponse);
			}
		}
	}
		gettimeofday(&end_time, NULL);
		spend_time = ((double)end_time.tv_sec - (double)start_time.tv_sec) + (((double)end_time.tv_usec - (double)start_time.tv_usec)/1000000);
		printf("##########client: %d spend %f seconds\n", client->cli_id, spend_time);

retu:
	/* Close the connection */
	if(link_state) {
		close(route_sockfd);
		close(auth_sockfd);
	}

error:
	pthread_exit(NULL);
}

#if 0
int checkipaddr(char *addr)
{
	char ch[4];
	int in[5];
	int t_port = -1;
	if(addr == NULL) 
		return -1;
	if(sscanf(addr, "%d%c%d%c%d%c%d%c%d", &in[0], &ch[0], \
				&in[1], &ch[1], &in[2], &ch[2], &in[3], &ch[3], &in[4]) == 9) {
		int i;
		for(i = 0; i < 3; i++) {
			if(ch[i] != '.') {
				printf("Invalid ipaddr\n");
				return 0;
			}
		}
		if(ch[i] != ':') {
			printf("Invalid ipaddr format -- should be ip:port\n");
			return 0;
		}

		for(i = 0; i < 4; i++) {
			if(in[i] > 255 || in[i] < 0) {
				printf("Invalid ipaddr, ip should in 0.0.0.0~255.255.255.255\n");	
				return 0;
			}
		}
		if(in[i] >= 65535 || in[i] <= 0) {
			printf("Invalid port %d, port should in 1~65535\n", in[i]);
			return 0;
		}

		return 1;
	}

	printf("Invalid ipaddr######\n");
	return 0;
}
#endif

void destory()
{
	char cmd[BUFSIZE] = {0};
	char devname[32] = {0};

	_cli_info *tmp = cli_list;
	while(cli_list) {
		sprintf(devname, "%s%d", VIATUAL_PREFIX, cli_list->cli_id);
		sprintf(cmd, "sudo ip link del link dev %s", devname);
		system(cmd);
		cli_list = cli_list->next;
		free(tmp);
	}
}

void init_virtual_dev(int dev_id, char *ipaddr)
{
	char cmd[BUFSIZE] = {0};
	char devname[32] = {0};

	if(!ipaddr) return;
	sprintf(devname, "%s%d", VIATUAL_PREFIX, dev_id);
	sprintf(cmd, "sudo ip link del link dev %s >/dev/null", devname);
	system(cmd);
	sprintf(cmd, "sudo ip link add link wlan0 dev %s type macvlan", devname);
	system(cmd);

	sprintf(cmd, "sudo ifconfig %s %s", devname, ipaddr);
	system(cmd);

	sprintf(cmd, "sudo ifconfig %s up", devname);
	system(cmd);

	sprintf(cmd, "sudo ip route del 172.30.0.0/16");
	system(cmd);
}

_cli_info *default_cli_list(int num)
{
	int i, ipstart = DEF_LOCAL_IP_START;
	_cli_info *tmp = NULL;
	_cli_info *pcli = NULL;
	_cli_info *new_list = NULL;

	printf("num = %d cli_num = %d\n", num, cli_num);
	for(i = num; i < cli_num; ipstart--) {
		tmp = (_cli_info *)malloc(sizeof(_cli_info));
		if(tmp == NULL) {
			perror("Malloc");
			exit(-1);
		}
		memset(tmp, 0, sizeof(_cli_info));
		sprintf(tmp->ipaddr, "%s%d", DEF_LOCAL_IP_PREFIX, ipstart);
		strcpy(tmp->name, DEF_TST_USER);
		strcpy(tmp->passwd, DEF_TST_PASSWD);
		tmp->cli_id = i++;
		tmp->next = NULL;
		printf("cli_info: %d %s %s %s\n", tmp->cli_id, tmp->name, tmp->passwd, tmp->ipaddr);
		if(pcli) {
			pcli->next = tmp;	
			pcli = pcli->next;
		} else
			pcli = tmp;

		if(!new_list)
			new_list = pcli;
	}
	if(NULL != cli_list) {
		pcli = cli_list;
		while(pcli->next) {
			pcli = pcli->next;	
		}
		pcli->next = new_list;
	}

	return new_list;
}

_cli_info *read_user_file(char *file)
{
	FILE *fp = NULL;
	char buf[128];
	int num = 0;
	_cli_info *tmp = NULL;
	_cli_info *pcli = NULL;
	_cli_info *new_list = NULL;

	if(!file) return NULL;
	if((fp = fopen(file, "r")) == NULL) {
		perror("open");
		return NULL;
	}

	memset(buf, 0, sizeof(buf));
	while(fgets(buf, sizeof(buf), fp)) {
		//format: ipaddr name passwd	
		tmp = (_cli_info *)malloc(sizeof(_cli_info));
		if(tmp == NULL) {
			perror("Malloc");
			exit(-1);
		}
		sscanf(buf, "%s %s %s\n", tmp->ipaddr, tmp->name, tmp->passwd);
		tmp->cli_id = num++;
		tmp->next = NULL;
		//printf("cli_info: %d %s %s %s %d\n", tmp->cli_id, tmp->name, tmp->passwd, tmp->ipaddr, (int)tmp->cli_pth);
		if(pcli) {
			pcli->next = tmp;	
			pcli = pcli->next;
		} else
			pcli = tmp;

		if(!new_list)
			new_list = pcli;
	//	init_virtual_dev(tmp->cli_id, tmp->ipaddr);
	}

	fclose(fp);

	return new_list;
}

void usage(char *cmd)
{
	printf("Usage: %s [ncflh]\n", cmd);
	printf("\t -n: set the number of testing clients\n");
	printf("\t -c: number of request of every client\n");
	printf("\t -f: set the file, which include the user:passwd\n");
	printf("\t -l: set the connection of http to keep-alive, default is close\n");
	printf("\t -h: print usage\n");
}

int main(int argc, char *argv[]) 
{
	int i, ret = -1; 

	if(argc <= 1) {
		usage(argv[0]);
		return -1;
	}
	
	//http connection default is close
	strcpy(connection, "Close");
	while((ret = getopt(argc, argv, ":n:f:c:h:l")) != -1) {
		switch(ret) {
			case 'f':	/* read the file, include the username and passwd*/
				cli_list = read_user_file(optarg);
				break;
			case 'n':	/* set the number of clients*/
				cli_num = atoi(optarg);
				break;
			case 'c':
				request_num = atoi(optarg);
				break;
			case 'l':
				link_state = 1; 
				strcpy(connection, "keep-alive");
				break;
			case 'h':
				;
			default:
				usage(argv[0]);
				return -1;
		}
	}
	
	if(!cli_list)
		cli_list = default_cli_list(0);
	get_route_info();
	_cli_info *tmp = cli_list;
	//create the testing clients
	for(i = 0; i < cli_num || tmp; i++, tmp = tmp->next) {
		if(!tmp) tmp = default_cli_list(i);
		pthread_create(&tmp->cli_pth, NULL, init_client, (void *)tmp);
		pthread_join(tmp->cli_pth, NULL);
	}
	
//	destory();
	return 0;
}
