#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <linux/netlink.h>

#include "msg_proto.h"
#include "log.h"
#include "tools.h"
#include "routerlist.h"
#include "zyc_roaming.h"

int nl_skfd = -1;
struct sockaddr_nl src_nl, dst_nl;

int zyc_roaming_init_nlfd()
{
	if(nl_skfd != -1)
		return nl_skfd;

	nl_skfd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ZYC_ROAMING);
	if(nl_skfd < 0) {
		LOG(LOG_ERR, "init zyc_roaming_nlfd failed: %s", strerror(errno));
		goto error_out;
	}

	memset(&src_nl, 0, sizeof(src_nl));
	src_nl.nl_family = AF_NETLINK;
	src_nl.nl_pid = getpid();
	src_nl.nl_groups = 0;

	if(bind(nl_skfd, (struct sockaddr *)&src_nl, sizeof(src_nl)) != 0){
		LOG(LOG_ERR, "bind zyc_roaming_nlfd failed: %s", strerror(errno));
		goto error_after_socket;
	}

	memset(&dst_nl, 0, sizeof(dst_nl));
	dst_nl.nl_family = AF_NETLINK;
	dst_nl.nl_pid = 0;
	dst_nl.nl_groups = 0;

	return nl_skfd;
error_after_socket:
	close(nl_skfd);
error_out:
	return -1;
}

void zyc_roaming_close_nlfd()
{
	if(nl_skfd > 0) {
		close(nl_skfd);
		nl_skfd = -1;
	}
}

void zyc_roaming_readmsg(int fd)
{
	int len;
	char msgbuf[512];
	char buf[64];
	struct msghdr msg;
	struct nlmsghdr *nh = NULL;
	struct iovec iov = {msgbuf, sizeof(msgbuf)};
	struct zyc_roaming_rinfo_msg *router = NULL;

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = (void *)&dst_nl;
	msg.msg_namelen = sizeof(dst_nl);

	len = recvmsg(fd, &msg, 0);
	nh = (struct nlmsghdr *)msgbuf;

	for(nh = (struct nlmsghdr *)msgbuf; NLMSG_OK(nh, len); \
			nh = NLMSG_NEXT(nh, len)) {
		if(nh->nlmsg_type == NLMSG_DONE \
				|| nh->nlmsg_type == NLMSG_ERROR)
			break;

		switch(nh->nlmsg_type) {
			case ZYC_ROAMING_ROUTER_UPDATE:
				router = (struct zyc_roaming_rinfo_msg *)NLMSG_DATA(nh);
				break;
			case ZYC_ROAMING_ROUTER_DEL:
				router = (struct zyc_roaming_rinfo_msg *)NLMSG_DATA(nh);
				router_del(router->mac);
				break;
			default:
				break;
		}
	}
}

int zyc_roaming_sendmsg(void *msg, char msglen, int type)
{
	char msgbuf[256] = {'\0'};
	struct nlmsghdr *message = (struct nlmsghdr *)msgbuf;

	memset(message, '\0', sizeof(struct nlmsghdr));
	message->nlmsg_seq = 0;
	message->nlmsg_flags = 0;
	message->nlmsg_pid = getpid();
	message->nlmsg_type = type;
	message->nlmsg_len = NLMSG_SPACE(msglen);

	if(msglen)
		memcpy(NLMSG_DATA(message), (char *)msg, msglen);

	return sendto(nl_skfd, message, message->nlmsg_len, 0,(struct sockaddr *)&dst_nl, sizeof(dst_nl));
}

void nlink_msg_cb(EV_P_ ev_io *io, int e)
{
	zyc_roaming_readmsg(io->fd);
}
