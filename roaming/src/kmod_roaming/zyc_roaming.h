#ifndef __ZYC_ROAMING_H__
#define __ZYC_ROAMING_H__

#include <linux/version.h>

#define NETLINK_ZYC_ROAMING 25

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

enum {
	AUTO_ADD = 10,
	USER_ADD,
};

struct zyc_roaming_rinfo_msg {
	u8 name[IFNAMSIZ];
	u8 mac[ETH_ALEN];
	u8 ipxaddr[16];
};

struct router_entry {
	u8 mac[ETH_ALEN];
	struct net_device *dev;
};

struct client_info {
	union {
		u8 cip[4];
		u32 i;
	} ip;
	u8 rmac[ETH_ALEN];
	u8 if_local;
};

struct client_entry {
	struct client_info cli;
	struct timer_list expire_timer;
};

struct zyc_roaming_parm {
	u32 ip_prefix;
	u32 ip_mask;
};

struct zyc_roaming_ctx {
	u32 netlink_pid;
	struct sock *netlink_fd;
	struct zyc_roaming_parm parms;

	spinlock_t lock;		/* Lock for SMP correctness */
	/*
	 * Control state.
	 */
	struct kobject *sys_zyc_roaming;	/* sysfs linkage */

	/*
	 * Callback notifiers.
	 */
	struct notifier_block dev_notifier;	/* Device notifier */
};

struct zyc_roaming_hashtable {
	struct hlist_nulls_head *table;
	int size;
};

struct zyc_roaming_client_hashnode {
	struct hlist_nulls_node hnnode;
	struct client_entry client;
	u8 type;
};

struct zyc_roaming_router_hashnode {
	struct hlist_nulls_node hnnode;
	struct router_entry *router;
};

#endif
