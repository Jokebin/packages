#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <asm/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

#include "log.h"
#include "clientlist.h"
#include "roaming.h"
#include "command.h"
#include "tools.h"

#define HASH_MAC(m)	(&m[2])

struct client_hashtable chash;

// macaddress[2~5]
static u32 client_hash_ptr(const void *ptr)
{
	u32 *hash = (u32 *)ptr;
	return *hash;
}

static u32 client_hash_bucket(u32 hash, u32 size)
{
	return hash % size;
}

void client_add(u8 *cmac, u8 *ip, u8 *rmac, u8 type, char state)
{
	u32 hash;
	struct client_hashnode *n;

	n = (struct client_hashnode *)safe_malloc(sizeof(struct client_hashnode));

	memcpy(n->client.cmac, cmac, sizeof(n->client.cmac));
	memcpy(n->client.ip.cip, ip, sizeof(n->client.ip.cip));

	if(rmac != NULL)
		memcpy(n->client.rmac, rmac, sizeof(n->client.rmac));
	n->type = type;
	n->state = state;

	chash.nums++;

	hash = client_hash_ptr(HASH_MAC(cmac));
	hash = client_hash_bucket(hash, chash.size);

	hlist_add_head(&n->hnode, &chash.table[hash]);
}

void client_del(u8 *mac)
{
	u32 hash;
	struct client_hashnode *n;

	if(!(n = client_find(mac)))
		return;

	chash.nums--;
	hlist_del(&n->hnode);
	free(n);
}

struct client_hashnode *client_find(u8 *mac)
{
	struct hlist_node *n;
	struct client_hashnode *ch;

	u32 hash = client_hash_ptr(HASH_MAC(mac));
	hash = client_hash_bucket(hash, chash.size);

	hlist_for_each_entry(ch, n, &chash.table[hash], hnode) {
		if(!strncmp(ch->client.cmac, mac, sizeof(ch->client.cmac)))
			return ch;
	}

	return NULL;
}

void client_update(u8 *cmac, u8 *ip, u8 *rmac, u8 type, char state)
{
	struct client_hashnode *n;

	n = client_find(cmac);
	if(!n) {
		client_add(cmac, ip, rmac, type, state);
	} else {
		if(rmac != NULL)
			memcpy(n->client.rmac, rmac, sizeof(n->client.rmac));

		memcpy(n->client.ip.cip, ip, sizeof(n->client.ip.cip));
		if(type != UNKNOWN_USER)
			n->type = type;

		n->state = state;
	}
}

void client_hashtable_print()
{
	int i;
	struct hlist_node *n;
	struct client_hashnode *ch;

	LOG(LOG_INFO, "****************client nums: %d********************", chash.nums);
	for(i = 0; i < chash.size; i++) {
		hlist_for_each_entry(ch, n, &chash.table[i], hnode) {
			LOG(LOG_INFO, "%d: client: "MAC_FMT", ip: %s, gw: "MAC_FMT", type: %d", \
					i, MAC_STR(ch->client.cmac), dec_to_ipstr(ch->client.ip.cip), \
					MAC_STR(ch->client.rmac), ch->type);
		}
	}
	LOG(LOG_INFO, "******************************************************");
}

void client_hashtable_init(u32 size)
{
	int i;

	chash.size = size;
	chash.state = STATE_FACTOR;
	chash.nums = 0;
	chash.table = (struct hlist_head *)safe_malloc(sizeof(struct hlist_head) * chash.size);

	for(i = 0; i < chash.size; i++)
		INIT_HLIST_HEAD(&chash.table[i]);
}

void client_get_timer_start(u8 *cmac)
{
	struct client_get_timer *timer;

	timer = (struct client_get_timer *)malloc(sizeof(*timer));
	if(timer == NULL) {
		LOG(LOG_ERR, "malloc for client_get_timer failed: %s", strerror(errno));
		exit(-1);
	}

	memcpy(timer->cmac, cmac, sizeof(timer->cmac));
	timer->trys = 0;
	ev_timer_init(&timer->w, client_get_timer_cb, 2.0, 0.0);
	ev_timer_start(EV_DEFAULT, &timer->w);
}

void client_get_timer_stop(ev_timer *w)
{
	struct client_get_timer *timer = container_of(w, struct client_get_timer, w);

	ev_timer_stop(EV_DEFAULT, w);

	free(timer);
}

void client_hashtable_destory()
{
	int i;
	struct hlist_node *n;
	struct client_hashnode *ch;

	for(i = 0; i < chash.size; i++) {
		hlist_for_each_entry(ch, n, &chash.table[i], hnode) {
			hlist_del(&ch->hnode);
			free(ch);
		}
	}

	if(chash.table)
		free(chash.table);
}

void client_sync_with_filesystem()
{
	int i;
	struct hlist_node *n;
	struct client_hashnode *ch;

	LOG(LOG_INFO, "Starting sync clients with filesystem !");
	chash.state = reverse(chash.state);
	client_update_from_filesystem(chash.state);

	for(i = 0; i < chash.size; i++) {
		hlist_for_each_entry(ch, n, &chash.table[i], hnode) {
			if(ch->state != chash.state) {
				hlist_del(&ch->hnode);
				free(ch);
				chash.nums--;
			}
		}
	}
}

void client_update_from_filesystem(char state)
{
	struct stat st;
	if(stat(CLIENTS_DIR, &st) || !S_ISDIR(st.st_mode))
		return;

	DIR *dir = NULL;
	struct dirent *pdir = NULL;
	u8 filename[128];

	FILE *fp = NULL;
	u8 *tmp, *pos = NULL;
	u8 readbuf[128];
	u8 ip[IP4_BUF];
	u8 flags;

	if(NULL == (dir = opendir(CLIENTS_DIR)))
		return;

	while((pdir = readdir(dir)) != NULL) {
		if(!strcmp(pdir->d_name, ".") || !strcmp(pdir->d_name, ".."))
			continue;

		flags = 0;
		memset(filename, 0, sizeof(filename));
		snprintf(filename, sizeof(filename), "%s/%s", CLIENTS_DIR, pdir->d_name);

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
			if(!strcmp(readbuf, "ip")) {
				strncpy(ip, pos, strlen(pos));
				ip[strlen(pos)] = '\0';
				flags++;
			}
		}

		if(1 == flags)
			client_update(macstr_to_dec(pdir->d_name), ipstr_to_dec(ip), NULL, UNKNOWN_USER, state);


		fclose(fp);
	}

	if(dir != NULL)
		closedir(dir);
}
