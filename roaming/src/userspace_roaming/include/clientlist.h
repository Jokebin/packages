#ifndef __CLIENTLIST_H__
#define __CLIENTLIST_H__

#include <libev/ev.h>
#include "msg_proto.h"
#include "list.h"

#define CLIENTS_DIR				"/tmp/roaming/clients"
#define CLIENT_UPDATE_SCRIPTS	"/etc/roaming/ip.sh"
#define STATE_FACTOR			1
#define reverse(x)				(-x)

enum {
	LOCAL_USER = 10,
	ROAMING_USER,
	UNKNOWN_USER,
};

typedef struct client_ctx {
	union {
		u8 cip[IP4_DEC_LEN];
		int i;
	} ip;
	u8 rmac[ETH_DEC_LEN];
	u8 cmac[ETH_DEC_LEN];
} cinfo_t;

struct client_hashtable {
	struct hlist_head *table;
	u32 size;
	u32 nums;
	char state;
};

struct client_get_timer {
	ev_timer w;
	u8 trys;
	u8 cmac[ETH_DEC_LEN];
};

struct client_hashnode {
	struct hlist_node hnode;
	struct client_ctx client;
	char state;
	u8 type;
};

void client_hashtable_destory();
void client_hashtable_init(u32 size);
void client_hashtable_print();

void client_del(u8 *mac);
void client_add(u8 *cmac, u8 *ip, u8 *rmac, u8 type, char state);
void client_update(u8 *cmac, u8 *ip, u8 *rmac, u8 type, char state);
struct client_hashnode *client_find(u8 *mac);

void client_get_timer_start(u8 *cmac);
void client_get_timer_stop(ev_timer *w);
void client_sync_with_filesystem();
void client_update_from_filesystem(char state);

#endif
