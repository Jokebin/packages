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
#include <sys/stat.h>

#include <linux/if.h>
#include <sys/ioctl.h>

#include <pthread.h>

#include <time.h>
#include <getopt.h>
#include "esp_config.h"

static char port_flag = 0;
static char iface_flag = 0;
static char file_flag = 0;
static char mode_flag = 0;

static char iface[32];
static char filename[128];
static char local_ip[16];
static short port = SERVER_PORT;
static int image_siz = 0;
static char *image_buf = NULL;

LIST_HEAD(config_list);

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

int read_image(char *file)
{
	FILE *fp = NULL;
	char rbuf[1024];
	char *p = NULL;
	struct stat st;
	int cnts = 0;
	int index = 0;

	if(NULL == file)
		return -1;

	if(stat(file, &st)) {
		printf("File %s is invalid!\n", file);
		return -1;
	}

	if(!S_ISREG(st.st_mode)) {
		printf("File %s is not a regular file!\n", file);
		return -1;
	}

	image_siz = st.st_size;
	image_buf = (char *)malloc(image_siz);
	if(NULL == image_buf) {
		perror("malloc for image_buf");
		return -1;
	}
	memset(image_buf, 0, image_siz);

	fp = fopen(file, "r");
	if(NULL == fp) {
		perror("fopen");
		goto err;
	}

	while((cnts = fread(&image_buf[index], 1, 1024, fp))) {
		index += cnts;

		if(feof(fp)) {
			break;
		}

		if(ferror(fp)) {
			perror("fread");
			fclose(fp);
			break;
		}
	}

	printf("filesiz %d bytes, total read %d bytes.\n", image_siz, index);

	fclose(fp);
	return 0;
err:
	free(image_buf);
}

int parse_file(char *file)
{
	FILE *fp = NULL;
	char rbuf[1024];
	int ret = 0, i;
	char *p = NULL;
	char tmac[18];
	struct stat st;
	esp_conf_t config;
	struct list_t *node = NULL;
	struct list_t *pos = NULL;

	if(NULL == file)
		return -1;

	if(stat(file, &st)) {
		printf("File %s is invalid!\n", file);
		return -1;
	}

	if(!S_ISREG(st.st_mode)) {
		printf("File %s is not a regular file!\n", file);
		return -1;
	}

	fp = fopen(file, "r");
	if(NULL == fp) {
		perror("fopen");
		return -1;
	}

	while(fgets(rbuf, sizeof(rbuf), fp) != NULL) {
		i = 0;
		while(' ' == rbuf[i])
			i++;

		p = &rbuf[i];
		if('#' == p[0])
			continue;

		ret = sscanf(p, "%s\t%s\t%s\t%s\t%hd\t%hd\t%hd", tmac, config.ssid, config.psword,
				config.serip, &config.serport, &config.sensors, &config.battery);

		if(EOF == ret) {
			perror("sscanf");
			break;
		}

		if(ret < 7)
			continue;

		node = (struct list_t *)malloc(sizeof(struct list_t));
		if(NULL == node) {
			perror("malloc");
			ret = -1;
			break;
		}

		memcpy(node->mac, tmac, strlen(tmac));
		memcpy(&node->info, &config, sizeof(config));
		//printf("mac: %s, ssid: %s, psword: %s, serip: %s, serport: %d, sensors: %d, battery: %d\n",
		//		node->mac, node->info.ssid, node->info.psword, node->info.serip, node->info.serport,
		//		node->info.sensors, node->info.battery);

		list_for_each_entry(pos, &config_list, list) {
			if(!memcmp(pos->mac, node->mac, sizeof(pos->mac))) {
				list_del(&pos->list);
				free(pos);
				break;
			}
		}

		list_add_tail(&node->list, &config_list);
	}

	list_for_each_entry(pos, &config_list, list) {
		printf("mac: %s, ssid: %s, psword: %s, serip: %s, serport: %d, sensors: %d, battery: %d\n",
				pos->mac, pos->info.ssid, pos->info.psword, pos->info.serip, pos->info.serport,
				pos->info.sensors, pos->info.battery);
	}

	fclose(fp);

	return 0;
}

void *update_handle(void *arg)
{
	int cnts = 0;
	int err = -1;
	int sockfd = -1;
	char rbuf[1410];
	char tbuf[sizeof(struct esp_update_request) + 18];
	struct sockaddr_in saddr, raddr;
	socklen_t socklen = sizeof(struct sockaddr);
	struct esp_update_request *request = (struct esp_update_request *)tbuf;
	esp_update_t *update = (esp_update_t *)rbuf;

	if(NULL == arg)
		pthread_exit(0);

	char *ip = (char *)arg;

	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if(sockfd < 0) {
		perror("socket");
		goto err;
	}

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);
	saddr.sin_addr.s_addr = inet_addr(ip);

	if(bind(sockfd, (struct sockaddr *)&saddr, socklen) < 0) {
		perror("bind");
		goto err;
	}

	int request_len = 0;
	int request_index = 0;
	int update_len = 0;
	int update_remain = 0;

	while(1) {
		err = recvfrom(sockfd, request, sizeof(tbuf), 0, (struct sockaddr *)&raddr, &socklen);
		if(err <= 0) {
			perror("recvfrom");
			continue;
		}

		switch(request->type) {
			case T_DATA:
				break;
			case T_OK:
				printf("%s: %s update succeed!\n", &tbuf[sizeof(*request)], inet_ntoa(raddr.sin_addr));
				continue;
			case T_FAIL:
				printf("%s: %s update failed!\n", &tbuf[sizeof(*request)], inet_ntoa(raddr.sin_addr));
				continue;
			default:
				continue;
		}

		request->len = ntohl(request->len);
		request->index = ntohl(request->index);

		if(request->len + request->index >= image_siz) {
			update_len = image_siz - request->index;
			update_remain = 0;
		} else {
			update_len = request->len;
			update_remain = image_siz - request->index - request->len;
		}
		//printf("request-> index = %d, len = %d\n", request->index, request->len);
		//printf("update: index = %d, len = %d, remain = %d\n", request->index, update_len, update_remain);

		cnts = update_len + sizeof(*update) - 1;

		update->len = htonl(update_len);
		update->index = htonl(request->index);
		update->remain = htonl(update_remain);

		memcpy(&update->data[0], &image_buf[request->index], update_len);
		err = sendto(sockfd, rbuf, cnts, 0, (struct sockaddr *)&raddr, socklen);
		if(err <= 0) {
			perror("sendto");
			continue;
		}
	}

err:
	if(sockfd > 0) {
		close(sockfd);
	}
	pthread_exit(0);
}

void *config_handle(void *arg)
{
	int err = -1;
	int sockfd = -1;
	char rbuf[32];
	struct sockaddr_in saddr, raddr;
	socklen_t socklen = sizeof(struct sockaddr);
	struct esp_conf_request *request = (struct esp_conf_request *)rbuf;
	struct list_t *pos;
	esp_conf_t config;

	if(NULL == arg)
		pthread_exit(0);

	char *ip = (char *)arg;

	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if(sockfd < 0) {
		perror("socket");
		goto err;
	}

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);
	saddr.sin_addr.s_addr = inet_addr(ip);

	if(bind(sockfd, (struct sockaddr *)&saddr, socklen) < 0) {
		perror("bind");
		goto err;
	}

	while(1) {
		printf("11");
		err = recvfrom(sockfd, rbuf, sizeof(rbuf)-1, 0, (struct sockaddr *)&raddr, &socklen);
		if(err <= 0) {
			perror("recvfrom");
			continue;
		}

		printf("22");

		rbuf[err] = '\0';
		list_for_each_entry(pos, &config_list, list) {
			if(!memcmp(pos->mac, request->mac, strlen(pos->mac))) {
				memset(&config, 0, sizeof(config));
				memcpy(config.ssid, pos->info.ssid, sizeof(config.ssid));
				memcpy(config.psword, pos->info.psword, sizeof(config.psword));
				memcpy(config.serip, pos->info.serip, sizeof(config.serip));
				config.serport = htons(pos->info.serport);
				config.sensors = htons(pos->info.sensors);
				config.battery = htons(pos->info.battery);

				err = sendto(sockfd, &config, sizeof(config), 0, (struct sockaddr *)&raddr, socklen);
				if(err <= 0) {
					printf("send config to %s:%s failed, err: %s\n", pos->mac, inet_ntoa(raddr.sin_addr), strerror(errno));
				} else {
					printf("Node %s:%s is ok!\n", pos->mac, inet_ntoa(raddr.sin_addr));
				}
				break;
			}
		}
	}

err:
	if(sockfd > 0) {
		close(sockfd);
	}
	pthread_exit(0);
}

void *broadcast_handle(void *arg)
{
	int retval = 0;
	int broadcast_fd = -1;
	struct sockaddr_in saddr;
	esp_cmd_t cmd;

	if(NULL == arg)
		return NULL;

	char *broadcast_ip = (char *)arg;

	bzero(&cmd, sizeof(cmd));
	cmd.cmd = mode_flag;
	strncpy(cmd.server, local_ip, sizeof(cmd.server));
	cmd.port = htons(port);

	// init broadcast socket
	if((broadcast_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)	{
		perror("create broadcast socket failed!");
		pthread_exit((void *)-1);
	}

	int optval = 1;
	if(setsockopt(broadcast_fd, SOL_SOCKET, SO_BROADCAST, (void *)&optval, sizeof(optval))) {
		perror("setsockopt failed!");
		close(broadcast_fd);
		pthread_exit((void *)-1);
	}

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(CLIENT_PORT);
#if 0
	saddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
#else
	if(inet_pton(AF_INET, broadcast_ip, &saddr.sin_addr) != 1) {
		perror("inet_pton failed!");
		close(broadcast_fd);
		pthread_exit((void *)-1);
	}
#endif

	while(1) {
		if(-1 == sendto(broadcast_fd, &cmd, sizeof(cmd), 0, (struct sockaddr *)&saddr, sizeof(struct sockaddr))) {
			perror("broadcast sendto failed!");
			close(broadcast_fd);
			pthread_exit((void *)-1);
		}

		sleep(1);
	}

	close(broadcast_fd);
	pthread_exit((void *)0);
}

void *unicast_handle(void *arg)
{
	int i = 0;
	int retval = 0;
	int ufd = -1;
	int prefix_len = 0;
	int suffix_len = 0;
	char ipbuf[16];
	char *p = NULL;
	struct sockaddr_in saddr;
	esp_cmd_t cmd;

	if(NULL == arg)
		return NULL;

	bzero(ipbuf, sizeof(ipbuf));
	strncpy(ipbuf, (char *)arg, sizeof(ipbuf));
	p = strrchr(ipbuf, '.');

	if(NULL == p) {
		printf("invalid ip: %s\n", p);
		pthread_exit((void *)-1);
	}
	p++;
	*p = '\0';
	prefix_len = p - &ipbuf[0];
	suffix_len = sizeof(ipbuf) - prefix_len;

	bzero(&cmd, sizeof(cmd));
	cmd.cmd = mode_flag;
	strncpy(cmd.server, local_ip, sizeof(cmd.server));
	cmd.port = htons(port);

	// init broadcast socket
	if((ufd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)	{
		perror("create unicast socket failed!");
		pthread_exit((void *)-1);
	}

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(CLIENT_PORT);

	while(1) {
		for(i=2; i<255; i++) {
			snprintf(p, suffix_len, "%d", i++);

			bzero(&saddr.sin_addr, sizeof(saddr.sin_addr));
			if(inet_pton(AF_INET, ipbuf, &saddr.sin_addr) != 1) {
				perror("inet_pton failed!");
				close(ufd);
				pthread_exit((void *)-1);
			}

			printf("send msg to %s\n", ipbuf);

			if(-1 == sendto(ufd, &cmd, sizeof(cmd), 0, (struct sockaddr *)&saddr, sizeof(struct sockaddr))) {
				perror("broadcast sendto failed!");
				close(ufd);
				pthread_exit((void *)-1);
			}
			bzero(p, suffix_len);
		}
		sleep(5);
	}
}

static struct option long_options[] = {
	{"iface", required_argument, NULL, 'i'},
	{"mode", required_argument, NULL, 'm'},
	{"file", required_argument, NULL, 'f'},
	{"port", required_argument, NULL, 'p'},
	{"help", no_argument, NULL, 'h'},
};

static void usage(char *cmd)
{
	printf("Usage: %s --mode update|config <--file xxx|--port xxx>\n", cmd);
	printf("\t --iface: interface connected with esp nodes\n");
	printf("\t --mode: update->update image, config->change configuration\n");
	printf("\t --file: pathname, configfile while mode is config, imagefile while mode is update\n");
	printf("\t --port: listen port of update or config, default 19999\n");
	printf("\t --help|-h: print this message\n");
	printf("\t configfile format: mac\tssid\tpsword\tserip\tserport\tsensors\tbattery\n");
}

int main(int argc, char **argv)
{
	int *retval;
	char broadcast_ip[16];
	void *(*handle)(void *arg) = NULL;

	int opt = 0;
	while((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
		switch(opt) {
			case 'i':
				iface_flag = 1;
				strncpy(iface, optarg, sizeof(iface));
				break;

			case 'm':
				if(!strncmp(optarg, "update", strlen(optarg)))
					mode_flag = CMD_UPDATE;
				else
					mode_flag = CMD_CONFIG;
				break;

			case 'f':
				file_flag = 1;
				strncpy(filename, optarg, sizeof(filename) - 1);
				filename[strlen(optarg)] = '\0';
				break;

			case 'p':
				port_flag = 1;
				port = atoi(optarg);
				break;
			case 'h':
			default:
				usage(argv[0]);
				return 0;
				break;
		}
	}

	if(!mode_flag) {
		usage(argv[0]);
		exit(1);
	}

	if(CMD_UPDATE == mode_flag) {
		printf("Mode: update..., port: %d\n", port);
		if(file_flag) {
			printf("image file: %s\n", filename);
			if(-1 == read_image(filename))
				exit(-1);

			handle = update_handle;
		} else {
			usage(argv[0]);
			exit(1);
		}
	} else if(CMD_CONFIG == mode_flag) {
		printf("Mode: config..., port: %d\n", port);
		if(file_flag) {
			printf("config file: %s\n", filename);
			if(-1 == parse_file(filename))
				exit(-1);

			handle = config_handle;
		} else {
			usage(argv[0]);
			exit(1);
		}
	}

	get_local_ip(iface, local_ip, broadcast_ip);
	printf("Local ip: %s, broadcast ip: %s\n", local_ip, broadcast_ip);

	char br_pthreadflag = 0;
	pthread_t broadcast_pthread;
#if 1
	if(pthread_create(&broadcast_pthread, NULL, broadcast_handle, broadcast_ip) != 0) {
		perror("pthread_create");
		return 1;
	} else {
		br_pthreadflag = 1;
	}
#endif

	char uc_pthreadflag = 0;
	pthread_t unicast_pthread;
#if 0
	if(pthread_create(&unicast_pthread, NULL, unicast_handle, local_ip) != 0) {
		perror("pthread_create");
		return 1;
	} else {
		uc_pthreadflag = 1;
	}
#endif

	char ser_pthreadflag = 0;
	pthread_t server_pthread;
	if(pthread_create(&server_pthread, NULL, handle, local_ip) != 0) {
		perror("pthread_create");
	} else {
		ser_pthreadflag = 1;
	}

	if(br_pthreadflag)
		pthread_join(broadcast_pthread, (void **)&retval);

	if(uc_pthreadflag)
		pthread_join(unicast_pthread, (void **)&retval);

	if(ser_pthreadflag)
		pthread_join(server_pthread, (void **)&retval);

	return 0;
}
