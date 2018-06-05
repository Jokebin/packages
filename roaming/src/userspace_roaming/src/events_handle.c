#include <libev/ev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <netdb.h>

#include "log.h"
#include "routerlist.h"
#include "clientlist.h"
#include "netsock.h"
#include "usock.h"
#include "tools.h"
#include "command.h"
#include "roaming.h"
#include "zyc_roaming.h"

static void hostap_msg_cb(char *msgbuf);
static void client_msg_cb(char *cmd);
static int msg_check(void *msgbuf, int len);
static void cli_num_msg(u8 *ip6, u8 *mac, u32 num, u8 msgid);
static void roaming_msg(rinfo_t *r, cinfo_t *c, u8 msgtype, u8 forward, u8 multicast);

extern struct zyc_roaming_parm zyc_roaming_parms;
extern struct router_hashtable rhash;
extern struct client_hashtable chash;
extern rinfo_t *local_router;
extern rinfo_t *gw_router;
extern u8 local_mac[ETH_DEC_LEN];

static void roaming_rule_set(u8 *rmac, u8 *cip, u8 local)
{
	struct client_entry client;

	memset(&client, 0, sizeof(client));
	client.if_local = local;
	memcpy(client.rmac, rmac, sizeof(client.rmac));
	memcpy(client.ip.cip, cip, sizeof(client.ip.cip));
	zyc_roaming_sendmsg(&client, sizeof(client), ZYC_ROAMING_CLIENT_ADD);
}

static void roaming_rule_del(u8 *cip)
{
	struct client_entry client;
	memset(&client, 0, sizeof(client));
	memcpy(client.ip.cip, cip, sizeof(client.ip.cip));
	zyc_roaming_sendmsg(&client, sizeof(client), ZYC_ROAMING_CLIENT_DEL);
}

void del_gw_router_cb(EV_P_ ev_timer *w, int e)
{
	int i;
	struct hlist_node *n;
	struct client_hashnode *ch;
	struct router_del_timer *t = container_of(w, struct router_del_timer, w);
	struct router_hashnode *r = router_find(t->mac);

	LOG(LOG_DEBUG, "GW: "MAC_FMT" changed to non-gw, delete related roaming rules", MAC_STR(t->mac));
	if(r == NULL) {
		/*
		 * this gw disapeared, clean roaming rules of clients
		 * using this gw
		 * */
		for(i = 0; i < chash.size; i++) {
			hlist_for_each_entry(ch, n, &chash.table[i], hnode) {
				if(!memcmp(t->mac, ch->client.rmac, sizeof(t->mac))) {
					ch->type = UNKNOWN_USER;
					memset(ch->client.rmac, 0, sizeof(local_mac));
					roaming_rule_del(ch->client.ip.cip);
				}
			}
		}
	}

	router_del_timer_stop(w);
}

void new_gw_router_cb(EV_P_ ev_timer *w, int e)
{
	struct router_hashnode *r = (struct router_hashnode *)
								container_of(w, struct router_hashnode, w);

	u8 msgbuf[128];
	message *msg = (message *)msgbuf;
	msg_sync_clients_req_t *req = (msg_sync_clients_req_t *)(msg + 1);
	u32 msglen = sizeof(message) + sizeof(msg_sync_clients_req_t);

	msg->len = htonl(msglen);
	msg->type = MSG_SYNC_CLIENTS_REQ;

	/* send get client num request */
	if(!r->client_info.msgid) {
		LOG(LOG_INFO, "Start receiving clients info");
		req->msgid = 0;
		memcpy(req->mac, local_mac, sizeof(req->mac));
		ucast_sock_send(r->router.ip6, msgbuf, msglen);
	} else if(r->client_info.index && r->client_info.client_num) {
		/* send get client data request */
		req->msgid = r->client_info.msgid;
		req->index = r->client_info.index;
		memcpy(req->mac, local_mac, sizeof(req->mac));
		ucast_sock_send(r->router.ip6, msgbuf, msglen);
	} else {
		LOG(LOG_INFO, "Finished receiving clients info");
		ev_timer_stop(EV_DEFAULT, w);
		return;
	}

	w->repeat = 2.0;
	ev_timer_again(EV_DEFAULT, w);
}

void client_get_timer_cb(EV_P_ ev_timer *w, int e)
{
	u8 cmdbuf[128];
	struct client_hashnode *ch;
	struct client_get_timer *timer = container_of(w, struct client_get_timer, w);

	/* client update cycle is 30s = 2*15 */
	if(timer->trys == 15)
		client_get_timer_stop(w);

	ch = client_find(timer->cmac);
	if(!ch) {
		snprintf(cmdbuf, 128, "%s", CLIENT_UPDATE_SCRIPTS);
		do_command(cmdbuf);

		timer->trys++;
		w->repeat = 2.0;
		ev_timer_again(EV_DEFAULT, w);
		return;
	}

	if(ch->type == UNKNOWN_USER) {
		if(gw_router != NULL) {
			ch->type = ROAMING_USER;
			memcpy(ch->client.rmac, gw_router->mac, sizeof(ch->client.rmac));
			roaming_msg(gw_router, &ch->client, MSG_ROAMING, 1, 0);
			roaming_rule_set(ch->client.rmac, ch->client.ip.cip, 1);

		} else if(local_router != NULL) {
			ch->type = LOCAL_USER;
			memcpy(ch->client.rmac, local_mac, sizeof(ch->client.rmac));
			roaming_msg(local_router, &ch->client, MSG_ROAMING_CLEAN, 0, 1);
			roaming_rule_del(ch->client.ip.cip);
		}
	}

	client_get_timer_stop(w);
}

void router_timer_cb(EV_P_ ev_timer *w, int e)
{
	router_hops();

	w->repeat = 30.0;
	ev_timer_again(EV_DEFAULT, w);
}

void client_timer_cb(EV_P_ ev_timer *w, int e)
{
	client_update_from_filesystem(chash.state);

	w->repeat = 30.0;
	ev_timer_again(EV_DEFAULT, w);
}

void client_periodic_update_cb(EV_P_ ev_periodic *w, int e)
{
	client_sync_with_filesystem();
}

void unix_msg_cb(EV_P_ ev_io *io, int e)
{
	char *rbuf;
	message *msg;
	int recvlen;

	int rcvfd = -1;

	recvlen = unix_recv(io->fd, &rcvfd, &rbuf);

	// only check the length if right
	if(!msg_check(rbuf, recvlen))
		return;

	msg = (message *)rbuf;

	switch(msg->type) {
		case UMSG_HOSTAPD:
			hostap_msg_cb((char *)(msg + 1));
			break;
		case UMSG_CLI_CMD:
			client_msg_cb((char *)(msg + 1));
			break;
		default:
			LOG(LOG_WARNING, "Unknown msg type");
			break;
	}

ret:
	if(rcvfd != -1)
		close(rcvfd);
}

void remote_msg_cb(EV_P_ ev_io *io, int e)
{
	int len = -1;
	char rbuf[1024] = {'\0'};
	char ip6addr[IP6_BUF] = {'\0'};
	u8 macbuf[ETH_DEC_LEN];
	message *msg = NULL;
	struct sockaddr_in6 saddr;

	socklen_t addrlen = sizeof(saddr);
	len = recvfrom(io->fd, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&saddr, &addrlen);
	if(-1 == len) {
		LOG(LOG_ERR, "udp_action recvfrom:%s", strerror(errno));
		return;
	}

	inet_ntop(AF_INET6, &saddr.sin6_addr, ip6addr, sizeof(ip6addr));

	// only check the msg len
	if(!msg_check(rbuf, len)) {
		LOG(LOG_ERR, "Invalid msglen, recvd %d, should be %d", len, ntohl(msg->len));
		return;
	}

	msg = (message *)rbuf;
	LOG(LOG_DEBUG, "Recvd msg from: %s, size: %d, type: %d", ip6addr, ntohl(msg->len), msg->type);

	switch(msg->type) {
		case MSG_ROAMING:
			{
				msg_roaming_t *rmsg = NULL;
				rmsg = (msg_roaming_t *)(msg + 1);

				if(!memcmp(rmsg->smac, local_mac, sizeof(local_mac)))
					return;
	
				if(local_router != NULL && 
						!memcmp(local_mac, rmsg->cinfo.rmac, sizeof(local_mac))) {

					if(rmsg->forward > 0) {
						rmsg->forward--;
						mcast_sock_send(rbuf, len);
					} else
						break;
	
					if(!memcmp(rmsg->cinfo.rmac, local_mac, sizeof(local_mac)))
						client_update(rmsg->cinfo.cmac, rmsg->cinfo.cip, rmsg->cinfo.rmac, LOCAL_USER, chash.state);
					else
						client_update(rmsg->cinfo.cmac, rmsg->cinfo.cip, rmsg->cinfo.rmac, ROAMING_USER, chash.state);
	
					/* setup roaming rules */
					roaming_rule_set(rmsg->smac, rmsg->cinfo.cip, 0);
				} else {
					client_update(rmsg->cinfo.cmac, rmsg->cinfo.cip, rmsg->cinfo.rmac, ROAMING_USER, chash.state);

					/* clean roaming rules */
					roaming_rule_del(rmsg->cinfo.cip);
				}
	
				LOG(LOG_INFO, "User "MAC_FMT" <%s> roaming to "MAC_FMT, MAC_STR(rmsg->cinfo.cmac), \
						dec_to_ipstr(rmsg->cinfo.cip), MAC_STR(rmsg->smac));
			}
			break;
		case MSG_ROAMING_SET:
			{
				msg_roaming_t *rmsg = NULL;
				rmsg = (msg_roaming_t *)(msg + 1);

				if(!memcmp(rmsg->smac, local_mac, sizeof(local_mac)))
					return;

				roaming_rule_set(rmsg->smac, rmsg->cinfo.cip, 0);
				client_update(rmsg->cinfo.cmac, rmsg->cinfo.cip, rmsg->cinfo.rmac, LOCAL_USER, chash.state);

				LOG(LOG_INFO, "User "MAC_FMT" <%s> roaming to "MAC_FMT", set roaming rules", \
						MAC_STR(rmsg->cinfo.cmac), dec_to_ipstr(rmsg->cinfo.cip), MAC_STR(rmsg->smac));
			}
			break;
		case MSG_ROAMING_CLEAN:
			{
				msg_roaming_t *rmsg = NULL;
				rmsg = (msg_roaming_t *)(msg + 1);

				if(!memcmp(rmsg->smac, local_mac, sizeof(local_mac)))
					return;

				roaming_rule_del(rmsg->cinfo.cip);
				client_update(rmsg->cinfo.cmac, rmsg->cinfo.cip, rmsg->cinfo.rmac, ROAMING_USER, chash.state);

				LOG(LOG_INFO, "User "MAC_FMT" <%s> roaming back to "MAC_FMT", clean roaming rules", \
						MAC_STR(rmsg->cinfo.cmac), dec_to_ipstr(rmsg->cinfo.cip), MAC_STR(rmsg->smac));
			}
			break;
		case MSG_SYNC_CLIENTS:
			{
				struct router_hashnode *r;
				msg_sync_client_num_t *nmsg  = (msg_sync_client_num_t *)(msg + 1);

				if(!(r = router_find(nmsg->mac)))
					break;
	
				if(!nmsg->msgid) {
					r->client_info.client_num = ntohl(nmsg->client_num);
					r->client_info.msgid = 1;
					r->client_info.index = 1;
					LOG(LOG_DEBUG, "Recvd clients num: %d from "MAC_FMT, r->client_info.client_num, MAC_STR(r->router.mac));
					break;
				} else {
					int bufsiz;
					struct client_info *pcli;
					u8 i, cnts, index;
					u32 remaining;
					msg_sync_client_info_t *resp = (msg_sync_client_info_t *)(msg + 1);

					if(resp->msgid != r->client_info.msgid) {
						LOG(LOG_WARNING, "Last msg \"msgid = %d\" dropped, retranslating", r->client_info.msgid);
						return;
					}

					cnts = resp->cnts;
					index = resp->index;
					pcli = &resp->cinfo;
					bufsiz = sizeof(rbuf);
					remaining = len - ((char *)pcli - rbuf);

					LOG(LOG_DEBUG, "Recvd %d clients info from %s, remaining:%d, len:%d, index:%d",
							cnts, ip6addr, remaining, len, index);

					for(i = 0; i < cnts; i++) {
						if(remaining < sizeof(*pcli)) {
							memcpy(rbuf, &rbuf[len - remaining], remaining);

							/* read from socket */
							len = recvfrom(io->fd, rbuf, bufsiz - remaining, 0, (struct sockaddr *)&saddr, &addrlen);
							if(-1 == len) {
								LOG(LOG_ERR, "udp_action recvfrom:%s", strerror(errno));
								return;
							}

							pcli = (struct client_info *)&rbuf[remaining];
							remaining = len - remaining;
						}

					if(!memcmp(pcli->rmac, local_mac, sizeof(local_mac)))
						client_update(pcli->cmac, pcli->cip, pcli->rmac, LOCAL_USER, chash.state);
					else
						client_update(pcli->cmac, pcli->cip, pcli->rmac, ROAMING_USER, chash.state);

						remaining -= sizeof(*pcli);
						pcli++;
					}

					if(!index)
						r->client_info.index = 0;
					else
						r->client_info.index = index + 1;

					r->client_info.msgid++;
				}
			}
			break;
		case MSG_SYNC_CLIENTS_REQ:
			{
				msg_sync_clients_req_t *sreq = (msg_sync_clients_req_t *)(msg + 1);

				if(!sreq->msgid) {
					cli_num_msg(ip6addr, local_mac, chash.nums, 0);
				} else {
					int i, cnts = 0;
					struct hlist_node *n;
					struct client_hashnode *ch;
					struct client_info *pcli;
					u32 msglen, bufsiz;
					msg_sync_client_info_t *resp;

					bufsiz = SEND_CLIENT_BUFSIZ;
					u8 *sbuf = (u8 *)malloc(bufsiz);
					if(!sbuf) {
						LOG(LOG_ERR, "malloc failed for sending clients info: %s", strerror(errno));
						return;
					}

					memset(sbuf, 0, SEND_CLIENT_BUFSIZ);
					msg = (message *)sbuf;
					msg->type = MSG_SYNC_CLIENTS;

					resp = (msg_sync_client_info_t *)(msg + 1);
					pcli = &resp->cinfo;
					msglen = (u8 *)pcli - sbuf;

					for(i = sreq->index - 1; i < chash.size && cnts < SEND_CLIENT_CNTS_ONCE; i++) {
						hlist_for_each_entry(ch, n, &chash.table[i], hnode) {
							/* only send clients using local router as gateway */
							if(memcmp(ch->client.rmac, local_mac, sizeof(local_mac)))
								continue;

							if(msglen + sizeof(struct client_info) > bufsiz) {
								bufsiz += sizeof(struct client_info ) * SEND_CLIENT_CNTS_ONCE/2;
								sbuf = (u8 *)realloc(sbuf, bufsiz);
								if(!sbuf) {
									LOG(LOG_ERR, "realloc failed for extending bufsize %d to %d: %s",\
											bufsiz, bufsiz - SEND_CLIENT_BUFSIZ/2, strerror(errno));

									free(msg);
									return;
								}
								pcli = (struct client_info *)&sbuf[msglen];
								memset(pcli, 0, bufsiz - msglen);
							}

							msglen += sizeof(struct client_info);
							memcpy(pcli->cip, ch->client.ip.cip, sizeof(pcli->cip));
							memcpy(pcli->cmac, ch->client.cmac, sizeof(pcli->cmac));
							memcpy(pcli->rmac, ch->client.rmac, sizeof(pcli->rmac));

							//LOG(LOG_DEBUG, ">>>>>>\t %d: rmac: "MAC_FMT, i, MAC_STR(pcli->cmac));
							//LOG(LOG_DEBUG, ">>>>>>\t ip:%s, router: "MAC_FMT, dec_to_ipstr(pcli->cip), MAC_STR(pcli->rmac));

							cnts++;
							pcli++;
						}
					}

					/* all clients have been sent */
					if(i == rhash.size)
						resp->index = 0;
					else
						resp->index = i;

					resp->cnts = cnts;
					resp->msgid = sreq->msgid;
					msg->len = htonl(msglen);
					memcpy(resp->mac, local_mac, sizeof(resp->mac));

					LOG(LOG_DEBUG, MAC_FMT" send %d clients info to %s", MAC_STR(local_mac), cnts, ip6addr);

					/* send msg */
					ucast_sock_send(ip6addr, sbuf, msglen);

					free(sbuf);
				}
			}
			break;
		default:
			break;
	}
}

void udp_msg_cb(EV_P_ ev_io *io, int e)
{
	int len = -1;
	char rbuf[512] = {'\0'};

	len = recvfrom(io->fd, rbuf, sizeof(rbuf), 0, NULL, NULL);
	if(len == -1) {
		LOG(LOG_ERR, "recvfrom failed: [%d] %s", errno, strerror(errno));
		return;
	}

	msg_roaming_resp_t *rmsg_resp = NULL;
}

/* send clients num to asking router */
static void cli_num_msg(u8 *ip6, u8 *mac, u32 num, u8 msgid)
{
	char sbuf[128];
	u32 msglen;
	message *msg;
	msg_sync_client_num_t *nmsg;

	msg = (message *)sbuf;
	msglen = sizeof(message) + sizeof(msg_sync_client_num_t);
	msg->len = htonl(msglen);
	msg->type = MSG_SYNC_CLIENTS;

	nmsg = (msg_sync_client_num_t *)(msg + 1);
	nmsg->msgid = msgid;
	nmsg->client_num = htonl(num);
	memcpy(nmsg->mac, mac, sizeof(nmsg->mac));

	ucast_sock_send(ip6, sbuf, msglen);
}

static void roaming_msg(rinfo_t *r, cinfo_t *c, u8 msgtype, u8 forward, u8 multicast)
{
	char sbuf[128];
	u32 msglen;
	message *msg;
	msg_roaming_t *rmsg;

	msg = (message *)sbuf;
	rmsg = (msg_roaming_t *)(msg + 1);
	msglen = sizeof(message) + sizeof(msg_roaming_t);

	memcpy(rmsg->cinfo.cip, c->ip.cip, sizeof(rmsg->cinfo.cip));
	memcpy(rmsg->cinfo.cmac, c->cmac, sizeof(rmsg->cinfo.cmac));
	rmsg->forward = forward;
	msg->len = htonl(msglen);
	msg->type = msgtype;

	memcpy(rmsg->cinfo.rmac, r->mac, sizeof(rmsg->cinfo.rmac));
	memcpy(rmsg->smac, local_mac, sizeof(rmsg->smac));

	if(!multicast) {
		ucast_sock_send(r->ip6, sbuf, msglen);
	} else {
		mcast_sock_send(sbuf, msglen);
	}
}

static void hostap_msg_cb(char *msgbuf)
{
	hostap_msg_t *hmsg = NULL;
	struct client_hashnode *ch;
	struct client_entry *client;
	struct router_hashnode *rh;
	rinfo_t *router;

	u32 msglen;
	message *msg;
	char sbuf[128];

	if(NULL == msgbuf)
		return;

	hmsg = (hostap_msg_t *)msgbuf;
	ch = client_find(macstr_to_dec(hmsg->mac));

	memset(sbuf, 0, sizeof(sbuf));

	if(NULL == ch) {
		LOG(LOG_INFO, "New user %s connected, but no record for it,"\
				" start timer to get", hmsg->mac);
		/* set timer to read record for new user */
		client_get_timer_start(macstr_to_dec(hmsg->mac));
		return;
	} else if(ch->type == LOCAL_USER) {
		LOG(LOG_INFO, "Local user reconnected, mac: "MAC_FMT", ip:%s", \
				MAC_STR(ch->client.cmac), dec_to_ipstr(ch->client.ip.cip));

		roaming_rule_del(ch->client.ip.cip);

		/* clean roaming rules on other routers */
		memcpy(ch->client.rmac, local_mac, sizeof(ch->client.rmac));
		roaming_msg(local_router, &ch->client, MSG_ROAMING_CLEAN, 0, 1);
		roaming_rule_del(ch->client.ip.cip);
		return;
	} else if(ch->type == ROAMING_USER) {
		LOG(LOG_INFO, "Roaming user connected, mac: "MAC_FMT", ip:%s", \
				MAC_STR(ch->client.cmac), dec_to_ipstr(ch->client.ip.cip));

		rh = router_find(ch->client.rmac);

		if(rh != NULL) {
			roaming_rule_set(ch->client.rmac, ch->client.ip.cip, 1);
			roaming_msg(&rh->router, &ch->client, MSG_ROAMING_SET, 0, 0);
			return;
		}
	}

	LOG(LOG_INFO, "New user connected or previous gw disappeared, mac: "MAC_FMT", ip:%s", \
			MAC_STR(ch->client.cmac), dec_to_ipstr(ch->client.ip.cip));

	if(gw_router != NULL) {
		ch->type = ROAMING_USER;
		memcpy(ch->client.rmac, gw_router->mac, sizeof(ch->client.rmac));
		roaming_msg(gw_router, &ch->client, MSG_ROAMING, 1, 0);
		roaming_rule_set(ch->client.rmac, ch->client.ip.cip, 1);

	} else if(local_router != NULL) {
		ch->type = LOCAL_USER;
		memcpy(ch->client.rmac, local_mac, sizeof(ch->client.rmac));
		roaming_msg(local_router, &ch->client, MSG_ROAMING_CLEAN, 0, 1);
		roaming_rule_del(ch->client.ip.cip);
	}
}

static void client_msg_cb(char *cmd)
{
	if(!strncmp(cmd, "sync", 4)) {
		client_sync_with_filesystem();
	} else if(!strncmp(cmd, "enable", 6)) {
		zyc_roaming_sendmsg(&zyc_roaming_parms, sizeof(struct zyc_roaming_parm), ZYC_ROAMING_ENABLE);
	} else if(!strncmp(cmd, "disable", 7)) {
		zyc_roaming_sendmsg(NULL, 0, ZYC_ROAMING_CLEAN);
	} else {
		LOG(LOG_WARNING, "==================Displaying %s infomations=================", cmd);
		if(!strncmp(cmd, "router", 6)) {
			router_hashtable_print();
		} else {
			router_hashtable_print();
			client_hashtable_print();
			zyc_roaming_sendmsg(NULL, 0, ZYC_ROAMING_INFO);
		}
		LOG(LOG_WARNING, "============================================================");
	}
}

// check msg if valid
// return 0, invalid
// return 1, valid
static int msg_check(void *msgbuf, int len)
{
	if(msgbuf == NULL)
		return 0;

	message *msg = (message *)msgbuf;
	int msglen = ntohl(msg->len);

	if(msglen != len) {
		LOG(LOG_ERR, "Error msg, msg len is %d, but recvd %d", msglen, len);
		return 0;
	}

	return 1;
}
