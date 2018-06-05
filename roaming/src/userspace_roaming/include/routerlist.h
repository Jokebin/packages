#ifndef __ROUTERLIST_H__
#define __ROUTERLIST_H__

#include <libev/ev.h>
#include <asm/types.h>
#include "msg_proto.h"
#include "list.h"

#define HOPS_DIR		"/tmp/roaming/hops"
#define OLDER_ROUTERS 	"/tmp/roaming/older_router"

typedef struct router_ctx {
	int hops;
	u8 mac[ETH_DEC_LEN];
	u8 ip6[IP6_BUF];
} rinfo_t;

struct router_hashtable {
	struct hlist_head *table;
	u32 size;
};

struct router_hashnode {
	struct ev_timer w;
	struct {
		u8 msgid;
		u8 index;
		u32 client_num;
	} client_info;
	struct hlist_node hnode;
	struct router_ctx router;
};

struct router_del_timer {
	struct ev_timer w;
	u8 mac[ETH_DEC_LEN];
};

void router_hashtable_destory();
void router_hashtable_init(u32 size);
void router_hashtable_print();

void router_del(u8 *mac);
void router_add(u8 *mac, u8 *addr, int hops);
void router_update(u8 *mac, u8 *addr, int hops);
struct router_hashnode *router_find(u8 *mac);
void router_del_timer_start(u8 *rmac);
void router_del_timer_stop(ev_timer *w);
void router_hops(void);

#endif
