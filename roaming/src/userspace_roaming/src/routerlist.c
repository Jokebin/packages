#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <asm/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>

#include "log.h"
#include "routerlist.h"
#include "roaming.h"
#include "command.h"
#include "tools.h"

#define HASH_MAC(m)	(&m[2])

struct router_hashtable rhash;

/* if local_router not null, local router is gw*/
rinfo_t *local_router = NULL;

/* if gw_router not null, gw_router is gw*/
rinfo_t *gw_router = NULL;

// macaddress[2~5]
static u32 router_hash_ptr(const void *ptr)
{
	u32 *hash = (u32 *)ptr;
	return *hash;
}

static u32 router_hash_bucket(u32 hash, u32 size)
{
	return hash % size;
}

void router_add(u8 *mac, u8 *addr, int hops)
{
	u32 hash;
	struct router_hashnode *n;

	n = (struct router_hashnode *)safe_malloc(sizeof(struct router_hashnode));

	n->router.hops = hops;
	memcpy(n->router.mac, mac, sizeof(n->router.mac));

	if(addr != NULL)
		strncpy(n->router.ip6, addr, sizeof(n->router.ip6));

	hash = router_hash_ptr(HASH_MAC(mac));
	hash = router_hash_bucket(hash, rhash.size);

	hlist_add_head(&n->hnode, &rhash.table[hash]);

	/* local router's hop is 0 */
	if(!hops) {
		local_router = &n->router;
		gw_router = NULL;
		return;
	}
	
	if(hops < 0) {
		gw_router = &n->router;
		local_router = NULL;
	}

	/* get clients from gw_router */
	ev_timer_init(&n->w, new_gw_router_cb, 2.0, 0.0);
	ev_timer_start(EV_DEFAULT, &n->w);
}

void router_del(u8 *mac)
{
	u32 hash;
	struct router_hashnode *n;

	if(!(n = router_find(mac)))
		return;

	if(local_router == &n->router)
		local_router = NULL;
	else if(gw_router == &n->router)
		gw_router = NULL;

	ev_timer_stop(EV_DEFAULT, &n->w);

	hlist_del(&n->hnode);
	free(n);

	router_del_timer_start(mac);
}

struct router_hashnode *router_find(u8 *mac)
{
	struct hlist_node *n;
	struct router_hashnode *ch;

	u32 hash = router_hash_ptr(HASH_MAC(mac));
	hash = router_hash_bucket(hash, rhash.size);

	hlist_for_each_entry(ch, n, &rhash.table[hash], hnode) {
		if(!strncmp(ch->router.mac, mac, sizeof(ch->router.mac)))
			return ch;
	}

	return NULL;
}

void router_update(u8 *mac, u8 *addr, int hops)
{
	struct router_hashnode *n;

	n = router_find(mac);
	if(!n)
		router_add(mac, addr, hops);
	else {
		if(hops < 0 && gw_router != &n->router) {
			gw_router = &n->router;
			local_router = NULL;

			/* get clients from gw_router */
			n->w.repeat = 2.0;
			ev_timer_again(EV_DEFAULT, &n->w);
		}

		n->router.hops = hops;
		if(addr != NULL && !strcmp(n->router.ip6, addr))
			strncpy(n->router.ip6, addr, sizeof(n->router.ip6));
	}
}

void router_hashtable_print()
{
	int i;
	struct hlist_node *n;
	struct router_hashnode *ch;

	LOG(LOG_INFO, "*********************router list**********************");
	for(i = 0; i < rhash.size; i++) {
		hlist_for_each_entry(ch, n, &rhash.table[i], hnode) {
			LOG(LOG_INFO, "%d: router: "MAC_FMT", hops: %d, addr: %s", \
					i, MAC_STR(ch->router.mac), ch->router.hops, \
					ch->router.ip6);
		}
	}
}

void router_hashtable_init(u32 size)
{
	int i;

	rhash.size = size;
	rhash.table = (struct hlist_head *)safe_malloc(sizeof(struct hlist_head) * rhash.size);

	for(i = 0; i < rhash.size; i++)
		INIT_HLIST_HEAD(&rhash.table[i]);
}

void router_hashtable_destory()
{
	int i;
	struct hlist_node *n;
	struct router_hashnode *ch;

	for(i = 0; i < rhash.size; i++) {
		hlist_for_each_entry(ch, n, &rhash.table[i], hnode) {
			hlist_del(&ch->hnode);
			free(ch);
		}
	}

	if(rhash.table)
		free(rhash.table);
}

void router_del_timer_start(u8 *rmac)
{
	struct router_del_timer *timer;

	timer = (struct router_del_timer *)malloc(sizeof(*timer));
	if(timer == NULL) {
		LOG(LOG_ERR, "malloc for router del timer failed: %s", strerror(errno));
		exit(-1);
	}

	memcpy(timer->mac, rmac, sizeof(timer->mac));
	ev_timer_init(&timer->w, del_gw_router_cb, 2.0, 0.0);
	ev_timer_start(EV_DEFAULT, &timer->w);
}

void router_del_timer_stop(ev_timer *w)
{
	struct router_del_timer *timer = container_of(w, struct router_del_timer, w);

	ev_timer_stop(EV_DEFAULT, w);
	free(timer);
}

void router_hops(void)
{
	struct stat st;
	if(stat(HOPS_DIR, &st) || !S_ISDIR(st.st_mode))
		return;

	DIR *dir = NULL;
	struct dirent *pdir = NULL;
	u8 filename[128];

	FILE *fp = NULL;
	u8 *pos = NULL;
	u8 *tmp = NULL;
	u8 readbuf[128];
	u8 ip6[IP6_BUF];
	char flags = 0;
	int hops = -1;

	if(NULL == (dir = opendir(HOPS_DIR)))
		return;

	while((pdir = readdir(dir)) != NULL) {
		if(!strcmp(pdir->d_name, ".") || !strcmp(pdir->d_name, ".."))
			continue;

		memset(filename, 0, sizeof(filename));
		snprintf(filename, sizeof(filename), "%s/%s", HOPS_DIR, pdir->d_name);

		if(NULL == (fp = fopen(filename, "r"))) {
			LOG(LOG_WARNING, "Fopen %s failed: %s", filename, strerror(errno));
			continue;
		}

		while(fgets(readbuf, sizeof(readbuf), fp) != NULL) {
			if(NULL == (pos = strchr(readbuf, ':')))
				continue;

			if((tmp = strchr(readbuf, '\n')) != NULL)
				*tmp = '\0';

			*pos++ = '\0';
			if(!strcmp(readbuf, "ip6")) {
				flags++;
				strncpy(ip6, pos, sizeof(ip6));
				ip6[strlen(pos)] = '\0';
			} else if(!strcmp(readbuf, "hops")) {
				flags++;
				hops = atoi(pos);
			}
		}

		if(flags != 2)
			continue;

		router_update(macstr_to_dec(pdir->d_name), ip6, hops);

		flags = 0;
		hops = -1;

		fclose(fp);
	}

	if(dir != NULL)
		closedir(dir);

	if(stat(OLDER_ROUTERS, &st) || !S_ISDIR(st.st_mode))
		return;

	if(NULL == (dir = opendir(OLDER_ROUTERS)))
		return;

	while((pdir = readdir(dir)) != NULL) {
		if(!strcmp(pdir->d_name, ".") || !strcmp(pdir->d_name, ".."))
			continue;

		memset(filename, 0, sizeof(filename));
		snprintf(filename, sizeof(filename), "%s/%s", OLDER_ROUTERS, pdir->d_name);
		if(remove(filename))
			continue;

		router_del(macstr_to_dec(pdir->d_name));
	}

	if(dir != NULL)
		closedir(dir);
}
