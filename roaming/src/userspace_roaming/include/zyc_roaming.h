#ifndef __ZYC_ROAMING_H__
#define __ZYC_ROAMING_H__

#define NETLINK_ZYC_ROAMING	25

struct client_entry {
	union {
		u8 cip[IP4_DEC_LEN];
		int i;
	} ip;
	u8 rmac[ETH_DEC_LEN];
	u8 if_local;
};

struct zyc_roaming_rinfo_msg {
	u8 name[IFNAMSIZ];
	u8 mac[ETH_DEC_LEN];
	u8 ipxaddr[IP6_DEC_LEN];
};

struct router_entry {
	u8 m8[ETH_DEC_LEN];
};

struct zyc_roaming_parm {
	u32 ip_prefix;
	u32 ip_mask;
};

enum {
	ZYC_ROAMING_INFO = 10,
	ZYC_ROAMING_CLIENT_ADD,
	ZYC_ROAMING_CLIENT_DEL,
	ZYC_ROAMING_CLIENT_FIND,
	ZYC_ROAMING_ROUTER_ADD,
	ZYC_ROAMING_ROUTER_DEL,
	ZYC_ROAMING_ROUTER_FIND,
	ZYC_ROAMING_ENABLE,
	ZYC_ROAMING_CLEAN,
	ZYC_ROAMING_ROUTER_UPDATE,
	ZYC_ROAMING_ROUTER_SYNC,
};

int zyc_roaming_init_nlfd();
void zyc_roaming_close_nlfd();
int zyc_roaming_sync_routerlist();
void nlink_msg_cb(EV_P_ ev_io *io, int e);
int zyc_roaming_sendmsg(void *msg, char msglen, int type);
void zyc_roaming_readmsg(int fd);

#endif
