#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <linux/if.h>
#include <sys/ioctl.h>

#include <pthread.h>

#include <time.h>
#include <getopt.h>
#include "esp_config.h"

static char iface_flag = 0;
static char ssid_flag = 0;
static char psword_flag = 0;
static char config_flag = 0;
static int magic_id = 0;

static char iface[32];
static char ssid[32];
static char psword[32];
static char config[32];

/**
 * 1. get local ip, and caculated broadcast address
 * 2. broadcast to announce config-server is online
 * 3. waiting clients online, and get client list
 * 4. update clients config
 */

int get_local_mac(const char *eth_inf, char *mac)
{
	struct ifreq ifr;
	int sk;

	bzero(&ifr, sizeof(ifr));

	if((sk = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket failed!");
		exit(1);
	}

	strncpy(ifr.ifr_name, eth_inf, strlen(eth_inf));

	if(ioctl(sk, SIOCGIFHWADDR, &ifr) < 0) {
		perror("socket failed!");
		close(sk);
		exit(1);
	}

	snprintf(mac, MAC_SIZE, MAC_FMT, MAC_STR(ifr.ifr_hwaddr.sa_data));

	close(sk);

	return 0;
}

int get_local_ip(const char *eth_inf, char *ip, char *brip)
{
	struct ifreq ifr;
	struct sockaddr_in *saddr;
	int sk;

	bzero(&ifr, sizeof(ifr));

	if((sk = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket failed!");
		exit(1);
	}

	strncpy(ifr.ifr_name, eth_inf, strlen(eth_inf));

	if(ioctl(sk, SIOCGIFADDR, &ifr) < 0) {
		perror("get ipaddr failed!");
		close(sk);
		exit(1);
	}

	saddr = (struct sockaddr_in *)&ifr.ifr_addr;
	snprintf(ip, IP_SIZE, "%s", inet_ntoa(saddr->sin_addr));

	if(ioctl(sk, SIOCGIFBRDADDR, &ifr) < 0) {
		perror("get broadcast addr failed!");
		close(sk);
		exit(1);
	}

	saddr = (struct sockaddr_in *)&ifr.ifr_addr;
	snprintf(brip, IP_SIZE, "%s", inet_ntoa(saddr->sin_addr));

	close(sk);

	return 0;
}

void *broadcast_msg(void *arg)
{
	int broadcast_fd = -1;
	struct esp_msg msg;
	struct sockaddr_in saddr;

	if(NULL == arg)
		return;

	char *broadcast_ip = (char *)arg;

	bzero(&msg, sizeof(msg));
	msg.serport = htons(SERVER_PORT);
	strncpy(msg.ssid, ssid, strlen(ssid));
	strncpy(msg.psword, psword, strlen(psword));
	msg.magic_id = htonl(magic_id);

	// init broadcast socket
	if((broadcast_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)	{
		perror("create broadcast socket failed!");
		return;
	}

	int optval = 1;
	if(setsockopt(broadcast_fd, SOL_SOCKET, SO_BROADCAST, (void *)&optval, sizeof(optval))) {
		perror("setsockopt failed!");
		close(broadcast_fd);
		return;
	}

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(SERVER_PORT);
#if 0
	saddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
#else
	if(inet_pton(AF_INET, broadcast_ip, &saddr.sin_addr) != 1) {
		perror("inet_pton failed!");
		close(broadcast_fd);
		return;
	}
#endif

	while(1) {
		if(-1 == sendto(broadcast_fd, &msg, sizeof(msg), 0, (struct sockaddr *)&saddr, sizeof(struct sockaddr))) {
			perror("sendto broadcast msg failed!");
			close(broadcast_fd);
			return;
		}

		sleep(1);
	}

	close(broadcast_fd);
}

static struct option long_options[] = {
	{"iface", required_argument, NULL, 'i'},
	{"ssid", required_argument, NULL, 's'},
	{"psword", required_argument, NULL, 'p'},
	{"config", required_argument, NULL, 'c'},
	{"help", no_argument, NULL, 'h'},
};

static void usage(char *cmd)
{
	printf("Usage: %s --iface xxx --ssid xxx --psword xxx\n", cmd);
	printf("\t iface: interface connected the same network with esp-module\n");
	printf("\t ssid: new ssid to set\n");
	printf("\t psword: password of the ssid\n");
	printf("\t config: config file, not supported\n");
	printf("\t help|h: print this message\n");
}

int main(int argc, char **argv)
{
	char mac[18];
	char ip[16];
	char broadcast_ip[16];
	
	int opt = 0;
	while((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
		switch(opt) {
			case 'i':
				iface_flag = 1;
				strncpy(iface, optarg, sizeof(iface));
				break;

			case 's':
				ssid_flag = 1;
				strncpy(ssid, optarg, sizeof(ssid));
				break;

			case 'p':
				psword_flag = 1;
				strncpy(psword, optarg, sizeof(psword));
				break;

			case 'c':
				config_flag = 1;
				strncpy(config, optarg, sizeof(config));
				break;

			case 'h':
			default:
				usage(argv[0]);
				break;
		}
	}

	if(!iface_flag || !ssid_flag || !psword_flag) {
		usage(argv[0]);
		exit(1);
	}

	srand((int)time(NULL));
	magic_id = rand();

	printf("Iface: %s, ssid: %s, psword: %s, config: %s, magic: %d\n",
			(iface_flag?iface:""), (ssid_flag?ssid:""), (psword_flag?psword:""),
			(config_flag?config:""), magic_id);

	get_local_mac(iface, mac);
	get_local_ip(iface, ip, broadcast_ip);
	printf("Mac: %s, ip: %s, broadcast: %s\n", mac, ip, broadcast_ip);

	pthread_t broadcast_pthread;
	if(pthread_create(&broadcast_pthread, NULL, broadcast_msg, broadcast_ip) != 0) {
		perror("pthread_create");
		return 1;
	}

	//broadcast_msg(ip, "255.255.255.255");
	return 0;
}
