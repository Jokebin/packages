#include <libev/ev.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "libnetlink.h"
#include "usock.h"

#include "log.h"
#include "routerlist.h"
#include "clientlist.h"
#include "netsock.h"
#include "tools.h"
#include "cli.h"
#include "roaming.h"
#include "zyc_roaming.h"

int ml_recv_sockfd = -1;

int foreground = 0;
int running_server = 1;
int loglevel = LOG_INFO;
int neg_status = ST_FRESH;
u8 local_mac[ETH_DEC_LEN];

struct sock_info sockinfo = {-1, 0, {-1, -1, -1, -1},};

static struct ev_io local_event, remote_event;
static struct ev_signal sigint_watcher, sigterm_watcher;
static struct ev_timer router_timer, client_timer;
static struct ev_periodic client_periodic_timer; //periodic sync clients with filesystem
struct zyc_roaming_parm zyc_roaming_parms; //172.30.22.1/16

static void
signal_cb(EV_P_ ev_signal *w, int revents)
{
    if (revents & EV_SIGNAL) {
        switch (w->signum) {
        case SIGINT:
        case SIGTERM:
			LOG(LOG_WARNING, "Killed by signal %d", w->signum);
            ev_unloop(EV_A_ EVUNLOOP_ALL);
			cleanup();

			exit(-1);
		}
	}
}

static int init_signals()
{
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);
	signal(SIGABRT, SIG_IGN);

    ev_signal_init(&sigint_watcher, signal_cb, SIGINT);
    ev_signal_init(&sigterm_watcher, signal_cb, SIGTERM);
    ev_signal_start(EV_DEFAULT, &sigint_watcher);
    ev_signal_start(EV_DEFAULT, &sigterm_watcher);
}

/*
 *	called at 1:00 every day
 * */
static ev_tstamp client_periodic_at(ev_periodic *w, ev_tstamp now)
{
	//return ((ev_tstamp)(now / 86400)*86400 + 86400 + 1 * 3600);
	return ((ev_tstamp)(now / 86400)*86400 + 17 * 3600 + 600);
}

void cleanup()
{
	close_unix();

	mcast_sock_close(&sockinfo);

	if(ev_is_active(&local_event))
		ev_io_stop(EV_DEFAULT, &local_event);

	if(ev_is_active(&remote_event))
		ev_io_stop(EV_DEFAULT, &remote_event);

	if(ev_is_active(&router_timer))
		ev_timer_stop(EV_DEFAULT, &router_timer);

	if(ev_is_active(&client_timer))
		ev_timer_stop(EV_DEFAULT, &client_timer);

	if(ev_is_active(&client_periodic_timer))
		ev_periodic_stop(EV_DEFAULT, &client_periodic_timer);

	//disable kmod-zyc_roaming
	zyc_roaming_sendmsg(NULL, 0, ZYC_ROAMING_CLEAN);

	zyc_roaming_close_nlfd();
}

static void usage()
{
	fprintf(stderr,
			"\n"
			"usage: roaming options"
			"\n"
			"options:\n"
			"	-h	show this usage\n"
			"	-m	local router mac addr\n"
			"	-l	lan ip/mask, default: 172.30.22.1/12\n"
			"	-f	running in foreground, otherwise as daemon\n"
			"	-d	loglevel, default LOG_INFO\n"
			"	-p	listen port, default 10240\n"
			"	-s	running as server, default option\n"
			"	-c	running cli_cmd: router|rules|all\n"
			"	-i	mesh interface list, eg: wlan1,eth0 "
			);

	exit(1);
}

static void test(void)
{
	//char *sbuf1 = "hello world, mcast!!!";
	//char *sbuf2 = "hello world, ucast !!!";
	char sbuf[128];
	message *msg = (message *)sbuf;
	msg_roaming_t *rmsg = (msg_roaming_t *)(msg + 1);
	int msglen = sizeof(message) + sizeof(msg_roaming_t);

	memset(sbuf, 0, 128);
	memcpy(rmsg->cinfo.cmac, macstr_to_dec("001122334455"), 6);
	memcpy(rmsg->cinfo.rmac, macstr_to_dec("1c40e80130ed"), 6);
	strncpy(rmsg->cinfo.cip, ipstr_to_dec("172.30.22.11"), sizeof(rmsg->cinfo.cip));
	msg->type = MSG_ROAMING;
	msg->len = htonl(msglen);
	rmsg->forward = 1;

	LOG(LOG_WARNING, MAC_FMT", "MAC_FMT", %s", MAC_STR(rmsg->cinfo.cmac), MAC_STR(rmsg->cinfo.rmac), dec_to_ipstr(rmsg->cinfo.cip));

	//mcast_sock_send(sbuf1, strlen(sbuf1));
	ucast_sock_send("fd66:66:66:1f:1e40:e8ff:fe01:30ef", sbuf, msglen);
}

int main(int argc, char **argv)
{
	int ret = 0;
	int mask = 0;
	int ufd = -1;
	int port = 0;
	int if_cnts;
	char *p = NULL;
	u8 mflag = 0;
	u8 nflag = 0;

	if(argc < 2) {
		usage();
		return -1;
	}

	// get options from commandline
	while((ret = getopt(argc, argv, ":p:d:P:l:m:i:fhsc")) != -1 && running_server) {
		switch(ret) {
			case 'f':
				foreground = 1;
				break;
			case 'l':
				if(!(p = strchr(optarg, '/'))) {
					usage();
					return -1;
				}
				*p++ = '\0';
				memcpy(&zyc_roaming_parms.ip_prefix, ipstr_to_dec(optarg), sizeof(zyc_roaming_parms.ip_prefix));

				mask = atoi(p);
				if(mask > 32 || mask < 0) {
					usage();
					return -1;
				}
				zyc_roaming_parms.ip_mask = htonl(~((1 << (32 - mask)) - 1));

				nflag = 1;
				break;
			case 'm':
				memcpy(local_mac, macstr_to_dec(optarg), sizeof(local_mac));
				mflag = 1;
				break;
			case 's':
				running_server = 1;
				break;
			case 'c':
				running_server = 0;
				break;
			case 'd':
				loglevel = atoi(optarg);
				if(loglevel < LOG_EMERG || loglevel > LOG_DEBUG) {
					LOG(LOG_ERR, "invalid loglevel %d", loglevel);
					return -1;
				}
				break;
			case 'p':
				port = atoi(optarg);
				if(port <= 0 || port >= 65535) {
					LOG(LOG_ERR, "Invalid port %d", port);
					return -1;
				}
				sock_port_init(port);
				break;
			case 'i':
				{
					p = strtok(optarg, ",");
					if(!p) {
						strcpy(sockinfo.interfaces[0], optarg);
						sockinfo.cnts = 1;
						break;
					}

					if_cnts = 0;
					while(p != NULL) {
						strcpy(sockinfo.interfaces[if_cnts++], p);
						p = strtok(NULL, ",");
					}


					sockinfo.cnts = if_cnts;
				}
				break;
			case 'h':
			default:
				usage();
				break;
		}
	}

	if(!running_server) {
		do_client(argc - 1, argv + 1);
		//test();
		return 0;
	}

	if(!nflag) {
		zyc_roaming_parms.ip_prefix = htonl(0xac1e0000);
		zyc_roaming_parms.ip_mask = htonl(0xffff0000);
	}

	if(!mflag) {
		fprintf(stderr, "Don't set local router mac addr\n");
		usage();
		return -1;
	}

	if(!sockinfo.cnts) {
		fprintf(stderr, "Don't set local interface\n");
		usage();
		return -1;
	}

	if(!foreground) {
		if(daemon(0, 0) != 0) {
			LOG(LOG_ERR, "daemon: %s", strerror(errno));
			return -1;
		}
	}

	init_signals();

	//init client hashtable
	client_hashtable_init(128);

	//init router hashtable
	router_hashtable_init(128);
	router_hops();

	//init zyc_roaming nl_skfd
	if (zyc_roaming_init_nlfd() < 0) {
		ret = -1;
		goto out;
	}

	if((ufd = init_unix()) < 0) {
		ret = errno;
		goto out;
	}

	//enable kmod-zyc_roaming
	zyc_roaming_sendmsg(&zyc_roaming_parms, sizeof(zyc_roaming_parms), ZYC_ROAMING_ENABLE);

	if(mcast_sock_open(&sockinfo) < 0) {
		ret = errno;
		goto out;
	}

	ev_io_init(&remote_event, remote_msg_cb, sockinfo.sockfd, EV_READ);
	ev_io_start(EV_DEFAULT, &remote_event);

	ev_io_init(&local_event, unix_msg_cb, ufd, EV_READ);
	ev_io_start(EV_DEFAULT, &local_event);

	ev_timer_init(&router_timer, router_timer_cb, 5.0, 0.0);
	ev_timer_start(EV_DEFAULT, &router_timer);

	ev_timer_init(&client_timer, client_timer_cb, 5.0, 0.0);
	ev_timer_start(EV_DEFAULT, &client_timer);

	ev_periodic_init(&client_periodic_timer, client_periodic_update_cb, 0, 0, client_periodic_at);
	ev_periodic_start(EV_DEFAULT, &client_periodic_timer);

	ev_run(EV_DEFAULT, 0);

out:
	cleanup();

	return ret;
}
