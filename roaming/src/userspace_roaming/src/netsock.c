#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "log.h"
#include "roaming.h"
#include "netsock.h"

static int ml_port = 10240;
static char *ml_addr = "ff02::02";
extern struct sock_info sockinfo;

static int sock_noblocking(int sock)
{
	int ret = 0;

	ret = fcntl(sock, F_GETFL, 0);
	if (ret < 0) {
		LOG(LOG_ERR, "failed to get file status flags: %s", strerror(errno));
		return ret;
	}

	ret = fcntl(sock, F_SETFL, ret | O_NONBLOCK);
	if (ret < 0) {
		LOG(LOG_ERR, "failed to set file status flags: %s", strerror(errno));
		return ret;
	}

	return 0;
}

void mcast_sock_close(struct sock_info *info)
{
	if(info->sockfd > 0)
		close(info->sockfd);
}

static int _mcast_sock_open(char *ipaddr, int port)
{
	int sock;
	int ret;
	struct sockaddr_in6 sin6;
	struct ipv6_mreq multicastreq;

	sock = socket(PF_INET6, SOCK_DGRAM, 0);
	if (sock  < 0) {
		LOG(LOG_ERR, "can't open socket: %s", strerror(errno));
		return -1;
	}

	/*memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	ifr.ifr_name[IFNAMSIZ - 1] = '\0';
	if (ioctl(sock, SIOCGIFINDEX, &ifr) == -1) {
		LOG(LOG_ERR, "can't get interface: %s", strerror(errno));
		goto err;
	}
	*/

	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_port = htons(port);
	sin6.sin6_family = AF_INET6;
	sin6.sin6_addr = in6addr_any;

	if (bind(sock, (struct sockaddr *)&sin6, sizeof(sin6)) < 0) {
		LOG(LOG_ERR, "can't bind: %s", strerror(errno));
		goto err;
	}

	multicastreq.ipv6mr_interface = 0;
	if(inet_pton(AF_INET6, ipaddr, &multicastreq.ipv6mr_multiaddr) != 1) {
		LOG(LOG_ERR, "inet_pton failed: %s", strerror(errno));
		goto err;
	}

	if(setsockopt(sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, (char *)&multicastreq, sizeof(multicastreq)) != 0) {
		LOG(LOG_ERR, "join multicast group failed: %s", strerror(errno));
		goto err;
	}

	if(sock_noblocking(sock))
		goto err;

	return sock;
err:
	close(sock);
	return -1;
}

static int _mcast_sock_send(char *ipaddr, int port,
		char *ifname, int *scope_id, char *sbuf, int bufsiz)
{
	int ret;
	int sock;
	int hops = 1;
	struct ifreq ifr;
	struct ipv6_mreq mreq6;
	struct sockaddr_in6 sin6;

	sock = socket(PF_INET6, SOCK_DGRAM, 0);
	if(sock < 0) {
		printf("socket failed: %s\n", strerror(errno));
		return -1;
	}

	if(*scope_id == -1) {
		memset(&ifr, 0, sizeof(ifr));
		strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
		ifr.ifr_name[IFNAMSIZ - 1] = '\0';
		if (ioctl(sock, SIOCGIFINDEX, &ifr) == -1) {
			LOG(LOG_ERR, "can't get interface: %s", strerror(errno));
			goto err;
		}

		*scope_id = ifr.ifr_ifindex;
	}

	sin6.sin6_family = AF_INET6;
	sin6.sin6_port = htons(port);
	sin6.sin6_scope_id = *scope_id;

	if(inet_pton(AF_INET6, ipaddr, &sin6.sin6_addr) != 1) {
		LOG(LOG_ERR, "inet_pton failed: %s", strerror(errno));
		goto err;
	}

	if(setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof(hops)) != 0) {
		LOG(LOG_ERR, "multcast hops setsocketopt failed: %s\n", strerror(errno));
		goto err;
	}

	mreq6.ipv6mr_interface = *scope_id;
	memcpy(&mreq6.ipv6mr_multiaddr, &sin6.sin6_addr, sizeof(struct in6_addr));

	if(setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_IF, (char *)scope_id, sizeof(*scope_id)) != 0) {
		LOG(LOG_ERR, "setsocketopt: set send interface failed:%s\n", strerror(errno));
		goto err;
	}

	if(setsockopt(sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq6, sizeof(mreq6)) < 0) {
		LOG(LOG_ERR, "setsocketopt: ipv6_add_membership failed %s\n", strerror(errno));
		goto err;
	}

	ret = sendto(sock, sbuf, bufsiz, 0, (struct sockaddr *)&sin6, sizeof(struct sockaddr_in6));
	if(ret < 0) {
		LOG(LOG_ERR, "sendto failed: %s\n", strerror(errno));
		goto err;
	}

	close(sock);

	return ret;
err:
	close(sock);
	return -1;
}

/*
 * ipaddr: ipv6 address
 * port: server port
 *
 * */
static int _ucast_sock_send(char *ipaddr, int port, char *sbuf, int bufsiz)
{
	int ret;
	int sock;
	struct sockaddr_in6 sin6;

	sock = socket(PF_INET6, SOCK_DGRAM, 0);
	if(sock < 0) {
		printf("socket failed: %s\n", strerror(errno));
		return -1;
	}

	sin6.sin6_family = AF_INET6;
	sin6.sin6_port = htons(port);

	if(inet_pton(AF_INET6, ipaddr, &sin6.sin6_addr) != 1) {
		LOG(LOG_ERR, "inet_pton <%s> failed: %s", ipaddr, strerror(errno));
		goto err;
	}

	ret = sendto(sock, sbuf, bufsiz, 0, (struct sockaddr *)&sin6, sizeof(struct sockaddr_in6));
	if(ret < 0) {
		LOG(LOG_ERR, "sendto failed: %s\n", strerror(errno));
		goto err;
	}

	close(sock);

	return sock;
err:
	close(sock);
	return -1;
}

void sock_port_init(int port)
{
	ml_port = port;
}

int mcast_sock_open(struct sock_info *info)
{
	if(!info)
		return -1;

	info->sockfd = _mcast_sock_open(ml_addr, ml_port);
	return info->sockfd;
}

int mcast_sock_send(char *sbuf, int bufsiz)
{
	int i, ret = -1;
	for(i = 0; i < sockinfo.cnts; i++) {
		ret = _mcast_sock_send(ml_addr, ml_port, sockinfo.interfaces[i], 
				&sockinfo.scope_id[i], sbuf, bufsiz);
		if(ret < 0)
			break;
	}

	return ret;
}

int ucast_sock_send(char *ipaddr, char *sbuf, int bufsiz)
{
	return _ucast_sock_send(ipaddr, ml_port, sbuf, bufsiz);
}
