#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/skbuff.h>
#include <net/tcp.h>
#include <net/route.h>
#include <net/ip6_route.h>
#include <net/ip6_tunnel.h>
#include <linux/inetdevice.h>
#include <linux/netfilter_bridge.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_conntrack_acct.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_conntrack_core.h>

#include <linux/vmalloc.h>
#include <linux/spinlock_types.h>
#include <linux/list_nulls.h>
#include <linux/rculist_nulls.h>

#include "zyc_roaming.h"
#include "zyc_backport.h"

MODULE_AUTHOR("ZYC");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ZYC ROAMING");

static char *roaming_devfilter = "bmx";
static char *roaming_localdev = "br-lan";

module_param_named(dev_filter, roaming_devfilter, charp, 0644);
module_param_named(localdev, roaming_localdev, charp, 0644);
MODULE_PARM_DESC(dev_filter, "Device filter/Name prefix");
MODULE_PARM_DESC(localdev, "Local dev, default br-lan");

#define ROUTER_KEY(m)	((m[2] << 24)|(m[3] << 16)|(m[4] << 8)|m[5])
#define HASH_MASK htonl(0x0000FFFF)

DEFINE_SPINLOCK(zyc_roaming_clock);
DEFINE_SPINLOCK(zyc_roaming_rlock);

static bool zyc_roaming_enabled __read_mostly;
static void zyc_roaming_client_del(u32 ip);

struct zyc_roaming_ctx roaming_ctx __read_mostly = {
	-1,
	NULL,
	{0xAC1E0000, 0xFFFF0000},
};

struct zyc_roaming_hashtable client_hashtable, router_hashtable;

static u32 zyc_roaming_send_to_user(u8 *msg, u32 msglen, u8 msgtype);
static void zyc_roaming_router_upto_user(struct router_entry *r, u8 action);
static struct zyc_roaming_router_hashnode *zyc_roaming_router_find(u8 *router);

static inline void zyc_roaming_ip6Tmac(struct in6_addr *ip6, u8 *r)
{
	r[0] = ip6->in6_u.u6_addr8[8];
	r[1] = ip6->in6_u.u6_addr8[9];
	r[2] = ip6->in6_u.u6_addr8[10];
	r[3] = ip6->in6_u.u6_addr8[13];
	r[4] = ip6->in6_u.u6_addr8[14];
	r[5] = ip6->in6_u.u6_addr8[15];
	r[0] &= 0xFD;
}

static void zyc_roaming_strTmac(const char *str, u8 *mac)
{
	u8 s = 0, t = 0, i = 0;

	while(1) {
		if(*str >= '0' && *str <= '9') {
			t = *str - '0';
		} else if(*str >= 'a' && *str <= 'f') {
			t = *str - 'a' + 10;
		} else if(*str >= 'A' && *str <= 'F') {
			t = *str - 'A' + 10;
		}

		s = s*16 + t;
		if(s > 255) {
			break;
		}

		if(!(++i % 2)) {
			mac[i/2 - 1] = s;
			s = 0;
		}

		if(*str == '\0' || (i/2 -1) == 5)
			break;
		str++;
	}
}

static inline void *zyc_roaming_kmalloc(size_t size, gfp_t flags)
{
	if(size > 0) {
		size = ALIGN(size, 4);
		return kmalloc(size, flags);
	} else {
		return NULL;
	}
}

static inline bool zyc_roaming_ip_valid(u32 ip)
{
	return (!((roaming_ctx.parms.ip_prefix ^ ip) & roaming_ctx.parms.ip_mask)) \
		&& (roaming_ctx.parms.ip_prefix != ip);
}

static u32 zyc_roaming_hash(const u32 ip)
{
	union {
		u8 a[4];
		u32 i;
	} hash;

	hash.i = ip & HASH_MASK;
	return (u32)(hash.a[0]<<24 | hash.a[1]<<16 | hash.a[2]<<8 | hash.a[3]);
}

static u32 zyc_roaming_hash_bucket(u32 hash, u32 size)
{
	return (hash % size);
}

static void *zyc_roaming_alloc_hashtable(unsigned int *sizep, int nulls)
{
	struct hlist_nulls_head *hash;
	unsigned int nr_slots, i;
	size_t sz;

	BUILD_BUG_ON(sizeof(struct hlist_nulls_head) != sizeof(struct hlist_head));
	nr_slots = *sizep = roundup(*sizep, PAGE_SIZE / sizeof(struct hlist_nulls_head));
	sz = nr_slots * sizeof(struct hlist_nulls_head);
	hash = (void *)__get_free_pages(GFP_KERNEL | __GFP_NOWARN | __GFP_ZERO,
			get_order(sz));
	if (!hash) {
		pr_warn("zyc_roaming: falling back to vmalloc.\n");
		hash = vzalloc(sz);
	}

	if (hash && nulls)
		for (i = 0; i < nr_slots; i++)
			INIT_HLIST_NULLS_HEAD(&hash[i], i);

	return hash;
}

static struct zyc_roaming_client_hashnode *zyc_roaming_client_find(u32 ip)
{
	u32 hash;
	struct hlist_nulls_node *n;
	struct zyc_roaming_client_hashnode *h, *rc = NULL;

	hash = zyc_roaming_hash_bucket(zyc_roaming_hash(ip), client_hashtable.size);
	spin_lock_bh(&zyc_roaming_clock);
	hlist_nulls_for_each_entry_rcu(h, n, &client_hashtable.table[hash], hnnode) {
		if(h->client.cli.ip.i == ip) {
			rc = h;
			break;
		}
	}
	spin_unlock_bh(&zyc_roaming_clock);

	return rc;
}

static void zyc_roaming_client_expire(unsigned long arg)
{
	struct zyc_roaming_client_hashnode *ch = (struct zyc_roaming_client_hashnode *)arg;
	if(!spin_trylock_bh(&zyc_roaming_clock)) {
		mod_timer(&ch->client.expire_timer, jiffies + 10*HZ);
		return;
	}

	pr_info("Client: %pI4, rmac: %pM timeout\n", \
			&ch->client.cli.ip.i, ch->client.cli.rmac);

	hlist_nulls_del_rcu(&ch->hnnode);
	del_timer(&ch->client.expire_timer);
	kfree(ch);

	spin_unlock_bh(&zyc_roaming_clock);
}

static struct net_device *zyc_roaming_client_outdev(u32 cip, u8 local)
{
	struct zyc_roaming_client_hashnode *c;
	struct zyc_roaming_router_hashnode *r;

	if(!zyc_roaming_ip_valid(cip))
		return NULL;

	c = zyc_roaming_client_find(cip);

	if(!c)
		return NULL;

	/* found client */
	if(local) {
		spin_lock_bh(&zyc_roaming_clock);
		c->client.cli.if_local = local;

		if(c->type == AUTO_ADD) {
			hlist_nulls_del_rcu(&c->hnnode);
			del_timer(&c->client.expire_timer);
			kfree(c);
			spin_unlock_bh(&zyc_roaming_clock);
			return NULL;
		}
		spin_unlock_bh(&zyc_roaming_clock);
	} else if(c->client.cli.if_local) {
		return NULL;
	}

	/* local=1,c->type=USER_ADD or local=0,if_local=0 */
	spin_lock_bh(&zyc_roaming_clock);
	c->client.cli.if_local = local;
	mod_timer(&c->client.expire_timer, jiffies + 60*HZ);
	spin_unlock_bh(&zyc_roaming_clock);

	if((r = zyc_roaming_router_find(c->client.cli.rmac)) != NULL)
		return r->router->dev;

	return NULL;
}

static void zyc_roaming_client_add(u32 ip, u8 *r, u8 local, u8 type)
{
	u32 hash = 0;
	struct zyc_roaming_client_hashnode *hnnode = NULL;

	if(r == NULL)
		return;

	hnnode = zyc_roaming_kmalloc(sizeof(struct zyc_roaming_client_hashnode), GFP_KERNEL);
	if(!hnnode) {
		pr_warn("zyc_roaming: falling back to kmalloc for zyc_roaming_client_hashnode.\n");
		return;
	}

	memset(hnnode, 0, sizeof(*hnnode));
	hnnode->type = type;
	hnnode->client.cli.ip.i = ip;
	hnnode->client.cli.if_local = local;
	memcpy(hnnode->client.cli.rmac, r, ETH_ALEN);

	// setup timer
	init_timer(&hnnode->client.expire_timer);
	hnnode->client.expire_timer.function = zyc_roaming_client_expire;
	hnnode->client.expire_timer.data = (unsigned long)hnnode;
	hnnode->client.expire_timer.expires = jiffies + 60*HZ;
	add_timer(&hnnode->client.expire_timer);

	hash = zyc_roaming_hash_bucket(zyc_roaming_hash(ip), client_hashtable.size);
	pr_debug("zyc_roaming_client_add, ip:%pI4, mac:%pm, type:%d, hash:%u\n", \
			hnnode->client.cli.ip.cip, hnnode->client.cli.rmac, hnnode->type, hash);

	spin_lock_bh(&zyc_roaming_clock);
	hlist_nulls_add_head_rcu(&hnnode->hnnode, &client_hashtable.table[hash]);
	spin_unlock_bh(&zyc_roaming_clock);
}

static void zyc_roaming_client_del(u32 ip)
{
	struct zyc_roaming_client_hashnode *hnnode = NULL;

	hnnode = zyc_roaming_client_find(ip);
	if(!hnnode)
		return;

	spin_lock_bh(&zyc_roaming_clock);
	hlist_nulls_del_rcu(&hnnode->hnnode);
	del_timer(&hnnode->client.expire_timer);
	kfree(hnnode);
	spin_unlock_bh(&zyc_roaming_clock);
}

static void zyc_roaming_client_update2(u32 cip, u8 *r, u8 local, u8 type)
{
	struct zyc_roaming_client_hashnode *ch;

	ch = zyc_roaming_client_find(cip);
	if(!ch) {
		zyc_roaming_client_add(cip, r, local, type);
	} else {
		spin_lock_bh(&zyc_roaming_clock);
		if(r && memcmp(ch->client.cli.rmac, r, ETH_ALEN))
			memcpy(ch->client.cli.rmac, r, ETH_ALEN);

		ch->type = type;
		ch->client.cli.if_local = local;

		mod_timer(&ch->client.expire_timer, jiffies + 60*HZ);
		spin_unlock_bh(&zyc_roaming_clock);
	}
}

static struct zyc_roaming_router_hashnode *zyc_roaming_router_find(u8 *router)
{
	struct hlist_nulls_node *n;
	struct zyc_roaming_router_hashnode *h, *rc = NULL;

	u32 hash = zyc_roaming_hash_bucket(zyc_roaming_hash(ROUTER_KEY(router)), router_hashtable.size);

	spin_lock_bh(&zyc_roaming_rlock);
	hlist_nulls_for_each_entry_rcu(h, n, &router_hashtable.table[hash], hnnode) {
		if(!memcmp(h->router->mac, router, sizeof(h->router->mac))) {
			rc = h;
			/*pr_warn("zyc_roaming_router_find, mac: %pm, hash: %u, dev: %s\n", \
					rc->router->mac, hash, rc->router->dev->name);
					*/
			break;
		}
	}
	spin_unlock_bh(&zyc_roaming_rlock);

	return rc;
}

static u8 zyc_roaming_router_add(struct router_entry *router)
{
	u32 hash = 0;
	struct zyc_roaming_router_hashnode *hnnode = NULL;

	hnnode = zyc_roaming_kmalloc(sizeof(struct zyc_roaming_router_hashnode), GFP_KERNEL);
	if(!hnnode) {
		pr_warn("zyc_roaming: falling back to kmalloc for zyc_roaming_router_hashnode.\n");
		return -ENOMEM;
	}

	hnnode->router = router;
	hash = zyc_roaming_hash_bucket(zyc_roaming_hash(ROUTER_KEY(router->mac)), router_hashtable.size);
	pr_debug("zyc_roaming_router_add, mac: %pm, hash: %u\n", hnnode->router->mac, hash);

	spin_lock_bh(&zyc_roaming_rlock);
	hlist_nulls_add_head_rcu(&hnnode->hnnode, &router_hashtable.table[hash]);
	spin_unlock_bh(&zyc_roaming_rlock);

	return 0;
}

static void zyc_roaming_router_del2(u8 *router)
{
	struct zyc_roaming_router_hashnode *r;

	r = zyc_roaming_router_find(router);
	if(!r)
		return;

	spin_lock_bh(&zyc_roaming_rlock);
	hlist_nulls_del_rcu(&r->hnnode);
	spin_unlock_bh(&zyc_roaming_rlock);

	zyc_roaming_router_upto_user(r->router, ZYC_ROAMING_ROUTER_DEL);

	dev_put(r->router->dev);
	kfree(r->router);
	kfree(r);
}

static void zyc_roaming_router_del(struct in6_addr *ip6)
{
	u8 rmac[ETH_ALEN] = {'\0'};
	zyc_roaming_ip6Tmac(ip6, rmac);
	zyc_roaming_router_del2(rmac);
}

static void zyc_roaming_router_upto_user(struct router_entry *r, u8 action)
{
	u8 msgbuf[128] = {'\0'};
	struct ip6_tnl *t = NULL;
	struct zyc_roaming_rinfo_msg *msg;

	msg = (struct zyc_roaming_rinfo_msg *)msgbuf;

	t = (struct ip6_tnl *)netdev_priv(r->dev);
	memcpy(msg->mac, r->mac, ETH_ALEN);
	memcpy(msg->ipxaddr, &t->parms.raddr, sizeof(t->parms.raddr));
	memcpy(msg->name, &t->parms.name, sizeof(t->parms.name));

	zyc_roaming_send_to_user(msgbuf, sizeof(struct zyc_roaming_rinfo_msg), action);
}

static void zyc_roaming_router_update(struct in6_addr *ip6, struct net_device *dev)
{
	u8 rmac[ETH_ALEN] = {'\0'};
	struct zyc_roaming_router_hashnode *h;
	struct router_entry *r;

	zyc_roaming_ip6Tmac(ip6, rmac);

	h = zyc_roaming_router_find(rmac);
	if(!h) {
		r = zyc_roaming_kmalloc(sizeof(struct router_entry), GFP_KERNEL);
		if(!r) {
			pr_warn("zyc_roaming alloc failed, no enough memory\n");
			return;
		}
		memcpy(r->mac, rmac, ETH_ALEN);
		r->dev = dev;

		if(!zyc_roaming_router_add(r))
			dev_hold(dev);

		zyc_roaming_router_upto_user(r, ZYC_ROAMING_ROUTER_UPDATE);
	} else if(h->router->dev != dev) {
		dev_put(h->router->dev);
		h->router->dev = dev;
		dev_hold(dev);
		zyc_roaming_router_upto_user(h->router, ZYC_ROAMING_ROUTER_UPDATE);
	}
}

static void zyc_roaming_free_hashtable(void *hash, unsigned int size)
{
	if (is_vmalloc_addr(hash))
		vfree(hash);
	else
		free_pages((unsigned long)hash,
				get_order(sizeof(struct hlist_head) * size));
}

static void zyc_roaming_print(void)
{
	u32 i;
	struct hlist_nulls_node *n;
	struct zyc_roaming_client_hashnode *h;
	struct zyc_roaming_router_hashnode *rh;

	spin_lock_bh(&zyc_roaming_clock);
	for(i = 0; i < client_hashtable.size; i++) {
		hlist_nulls_for_each_entry_rcu(h, n, &client_hashtable.table[i], hnnode) {
			pr_info("%d >>> ip: %pI4, rmac: %pM, local: %s, type: %s\n", \
					i, h->client.cli.ip.cip, h->client.cli.rmac, \
					(h->client.cli.if_local ? "true" : "false"), \
					(h->type == AUTO_ADD) ? "AUTO_ADD" : "USER_ADD");
		}
	}
	spin_unlock_bh(&zyc_roaming_clock);

	spin_lock_bh(&zyc_roaming_rlock);
	for(i = 0; i < router_hashtable.size; i++) {
		hlist_nulls_for_each_entry_rcu(rh, n, &router_hashtable.table[i], hnnode) {
			pr_info("%d >>> mac: %pm, devname: %s\n", i, rh->router->mac, rh->router->dev->name);
		}
	}
	spin_unlock_bh(&zyc_roaming_rlock);

	pr_info("ip_prefix: %pI4, ip_mask: %pI4\n", \
				&roaming_ctx.parms.ip_prefix, &roaming_ctx.parms.ip_mask);
}

static void zyc_roaming_cleanup(void)
{
	u32 i;
	struct zyc_roaming_client_hashnode *ch;

	// only cleanup the client_hashtable
	spin_lock_bh(&zyc_roaming_clock);
	for(i = 0; i < client_hashtable.size; i++) {
		while(!hlist_nulls_empty(&client_hashtable.table[i])) {
			ch = hlist_nulls_entry(client_hashtable.table[i].first,
					struct zyc_roaming_client_hashnode, hnnode);
			hlist_nulls_del_rcu(&ch->hnnode);
			del_timer(&ch->client.expire_timer);
			kfree(ch);
		}
	}
	spin_unlock_bh(&zyc_roaming_clock);
}

static u32 zyc_roaming_send_to_user(u8 *msg, u32 msglen, u8 msgtype)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;

	skb = alloc_skb(NLMSG_SPACE(msglen), GFP_ATOMIC);
	nlh = nlmsg_put(skb, 0, 0, msgtype, NLMSG_SPACE(msglen) - sizeof(struct nlmsghdr), 0);
	memcpy(NLMSG_DATA(nlh), msg, msglen);

	return netlink_unicast(roaming_ctx.netlink_fd, skb, roaming_ctx.netlink_pid, MSG_DONTWAIT);
}

static void zyc_roaming_read_from_user(struct sk_buff *__skb)
{
	u32 i;
	struct sk_buff *skb;
	struct nlmsghdr *nlh = NULL;
	struct client_info client;
	struct router_entry router, *nrouter;
	struct hlist_nulls_node *hn;
	struct zyc_roaming_router_hashnode *rnode;

	skb = skb_get(__skb);

	if(skb->len < sizeof(struct nlmsghdr))
		goto out;

	nlh = (struct nlmsghdr *)skb->data;
	if((nlh->nlmsg_len < sizeof(struct nlmsghdr)) || (__skb->len < nlh->nlmsg_len))
		goto out;

	spin_lock_bh(&roaming_ctx.lock);

	roaming_ctx.netlink_pid = nlh->nlmsg_pid;
	switch(nlh->nlmsg_type) {
		case ZYC_ROAMING_ENABLE:
			memcpy(&roaming_ctx.parms, (struct zyc_roaming_parm *)(NLMSG_DATA(nlh)), sizeof(struct zyc_roaming_parm));
			roaming_ctx.parms.ip_prefix = roaming_ctx.parms.ip_prefix;
			roaming_ctx.parms.ip_mask = roaming_ctx.parms.ip_mask;
			zyc_roaming_enabled = true;
			break;
		case ZYC_ROAMING_INFO:
			zyc_roaming_print();
			break;
		case ZYC_ROAMING_CLIENT_ADD:
			memcpy(&client, (struct client_info *)(NLMSG_DATA(nlh)), sizeof(struct client_info));
			zyc_roaming_client_update2(client.ip.i, client.rmac, client.if_local, USER_ADD);
			break;
		case ZYC_ROAMING_CLIENT_DEL:
			memcpy(&client, (struct client_info *)(NLMSG_DATA(nlh)), sizeof(struct client_info));
			zyc_roaming_client_del(client.ip.i);
			break;
		case ZYC_ROAMING_CLIENT_FIND:
			memcpy(&client, (struct client_info *)(NLMSG_DATA(nlh)), sizeof(struct client_info));
			zyc_roaming_client_find(client.ip.i);
			break;
		case ZYC_ROAMING_ROUTER_ADD:
			memcpy(router.mac, (u8 *)(NLMSG_DATA(nlh)), sizeof(router.mac));
			if(!zyc_roaming_router_find(router.mac) && NULL != (nrouter = zyc_roaming_kmalloc(sizeof(struct router_entry), GFP_KERNEL))) {
				memcpy(nrouter->mac, router.mac, sizeof(router.mac));
				zyc_roaming_router_add(nrouter);
			}
			break;
		case ZYC_ROAMING_ROUTER_DEL:
			memcpy(router.mac, (u8 *)(NLMSG_DATA(nlh)), sizeof(router.mac));
			zyc_roaming_router_del2(router.mac);
			break;
		case ZYC_ROAMING_ROUTER_FIND:
			memcpy(router.mac, (u8 *)(NLMSG_DATA(nlh)), sizeof(router.mac));
			rnode = zyc_roaming_router_find(router.mac);
			if(rnode)
				zyc_roaming_router_upto_user(rnode->router, ZYC_ROAMING_ROUTER_UPDATE);
			break;
		case ZYC_ROAMING_ROUTER_SYNC:
			spin_lock_bh(&zyc_roaming_rlock);
			for(i = 0; i < router_hashtable.size; i++) {
				hlist_nulls_for_each_entry_rcu(rnode, hn, &router_hashtable.table[i], hnnode) {
					zyc_roaming_router_upto_user(rnode->router, ZYC_ROAMING_ROUTER_UPDATE);
				}
			}
			spin_unlock_bh(&zyc_roaming_rlock);
			break;
		case ZYC_ROAMING_CLEAN:
			zyc_roaming_enabled = false;
			zyc_roaming_cleanup();
			break;
		default:
			break;
	}

	spin_unlock_bh(&roaming_ctx.lock);

out:
	kfree_skb(skb);
}

static inline char zyc_roaming_target_device(const struct net_device *dev)
{
	return !strncmp(roaming_devfilter, dev->name, strlen(roaming_devfilter));
}

int zyc_roaming_device_event(struct notifier_block *nb,
		unsigned long action, void *data)
{
	struct net_device *dev = ZYC_DEV_EVENT_PTR(data);

	switch(action) {
		case NETDEV_UP:
			if(zyc_roaming_target_device(dev)) {
				pr_debug("Dev: %s, addr:%pI6 UP\n", dev->name, dev->dev_addr);
				zyc_roaming_router_update((struct in6_addr *)dev->dev_addr, dev);
			}
			break;
		case NETDEV_DOWN:
			if(zyc_roaming_target_device(dev)) {
				pr_debug("Dev: %s, addr:%pI6, DOWN\n", dev->name, dev->dev_addr);
				zyc_roaming_router_del((struct in6_addr *)dev->dev_addr);
			}
			break;
	}

	return NOTIFY_DONE;
}

static int zyc_roaming_dns_packet_check(struct sk_buff *skb)
{
	struct iphdr *iph;
	struct tcphdr *tcph;
	struct udphdr *udph;

	if(NULL == skb)
		return 0;

	iph = ip_hdr(skb);

	switch(iph->protocol) {
		case IPPROTO_TCP:
			tcph = tcp_hdr(skb);
			if(53 == ntohs(tcph->dest) || 53 == ntohs(tcph->source))
				return 1;
			break;
		case IPPROTO_UDP:
			udph = udp_hdr(skb);
			if(53 == ntohs(udph->dest) || 53 == ntohs(udph->source))
				return 1;
			break;
		default:
			return 0;
	}

	return 0;
}

static inline unsigned int zyc_roaming_tcp_optlen(const u_int8_t *opt, unsigned int offset)
{
	/* Beware zero-length options: make finite progress */
	if (opt[offset] <= TCPOPT_NOP || opt[offset+1] == 0)
		return 1;
	else
		return opt[offset+1];
}

/* copyed from xt_TCPMSS.c */
static int zyc_roaming_tcp_clamp_mss_to_pmtu(struct sk_buff *skb,
		const struct net_device *in,
		const struct net_device *out)
{
	u8 *opt;
	u16 newmss;
	__be16 oldval, newlen;
	struct tcphdr *tcph;
	struct flowi fl;
	struct rtable *rt = NULL;
	const struct nf_afinfo *ai;
	struct iphdr *iph = ip_hdr(skb);
	unsigned int tcplen, in_mtu, i, tcphoff, minlen, tcp_hdrlen;

	tcphoff = iph->ihl * 4;
	minlen = sizeof(struct iphdr) + sizeof(struct tcphdr);

	tcplen = skb->len - tcphoff;
	tcph = (struct tcphdr *)(skb_network_header(skb) + tcphoff);
	tcp_hdrlen = tcph->doff * 4;

	/* header cannot be larger than the packet */
	if(tcplen < tcp_hdrlen)
		return -1;

	/* only process tcp syn packets */
	if(!(((char *)tcph)[13] & TCPHDR_SYN))
		return 0;

	if(!skb_make_writable(skb, skb->len))
		return -1;

	/* get in_mtu */
	memset(&fl, 0, sizeof(fl));
	fl.u.ip4.daddr = iph->saddr;
	rcu_read_lock();
	ai = nf_get_afinfo(PF_INET);
	if(ai != NULL)
		ai->route(&init_net, (struct dst_entry **)&rt, &fl, false);
	rcu_read_unlock();

	if(rt != NULL) {
		in_mtu = dst_mtu(&rt->dst);
		dst_release(&rt->dst);
	}

	if(in_mtu <= minlen)
		return -1;

	newmss = min(out->mtu, in_mtu) - minlen;

	opt = (u_int8_t *)tcph;
	for (i = sizeof(struct tcphdr); i < tcp_hdrlen; i += zyc_roaming_tcp_optlen(opt, i)) {
		if (opt[i] == TCPOPT_MSS && opt[i+1] == TCPOLEN_MSS) {
			u_int16_t oldmss;

			oldmss = (opt[i+2] << 8) | opt[i+3];

			/* Never increase MSS, even when setting it, as
			 * doing so results in problems for hosts that rely
			 * on MSS being set correctly.
			 */
			if (oldmss <= newmss)
				return 0;

			opt[i+2] = (newmss & 0xff00) >> 8;
			opt[i+3] = newmss & 0x00ff;

			inet_proto_csum_replace2(&tcph->check, skb,
						 htons(oldmss), htons(newmss),
						 0);
			return 0;
		}
	}

	/* There is data after the header so the option can't be added
	   without moving it, and doing so may make the SYN packet
	   itself too large. Accept the packet unmodified instead. */
	if (tcplen > tcp_hdrlen)
		return 0;

	/*
	 * MSS Option not found ?! add it..
	 */
	if (skb_tailroom(skb) < TCPOLEN_MSS) {
		if (pskb_expand_head(skb, 0,
				     TCPOLEN_MSS - skb_tailroom(skb),
				     GFP_ATOMIC))
			return -1;
		tcph = (struct tcphdr *)(skb_network_header(skb) + tcphoff);
	}

	skb_put(skb, TCPOLEN_MSS);

	/*
	 * IPv4: RFC 1122 states "If an MSS option is not received at
	 * connection setup, TCP MUST assume a default send MSS of 536".
	 * Since no MSS was provided, we must use the default values
	 */
	newmss = min(newmss, (u16)536);

	opt = (u_int8_t *)tcph + sizeof(struct tcphdr);
	memmove(opt + TCPOLEN_MSS, opt, tcplen - sizeof(struct tcphdr));

	inet_proto_csum_replace2(&tcph->check, skb,
				 htons(tcplen), htons(tcplen + TCPOLEN_MSS), 1);
	opt[0] = TCPOPT_MSS;
	opt[1] = TCPOLEN_MSS;
	opt[2] = (newmss & 0xff00) >> 8;
	opt[3] = newmss & 0x00ff;

	inet_proto_csum_replace4(&tcph->check, skb, 0, *((__be32 *)opt), 0);

	oldval = ((__be16 *)tcph)[6];
	tcph->doff += TCPOLEN_MSS/4;
	inet_proto_csum_replace2(&tcph->check, skb,
				 oldval, ((__be16 *)tcph)[6], 0);

	iph = ip_hdr(skb);
	newlen = htons(ntohs(iph->tot_len) + TCPOLEN_MSS);
	csum_replace2(&iph->check, iph->tot_len, newlen);
	iph->tot_len = newlen;

	return 0;
}

zyc_roaming_pre_routing_hook(hooknum, ops, skb, in, out, okfn)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct in_device *in_dev;
	struct net_device *out_dev = NULL;
	struct zyc_roaming_client_hashnode *c = NULL;
	u8 rmac[ETH_ALEN] = {'\0'};

	/* zyc_roaming not enabled*/
	if(!zyc_roaming_enabled)
		return NF_ACCEPT;

	in_dev = __in_dev_get_rcu(in);
	if(!in_dev)
		return NF_ACCEPT;

	if(!strcmp("lo", in->name))
		return NF_ACCEPT;

	if(ipv4_is_loopback(iph->saddr) || \
			ipv4_is_loopback(iph->daddr) || \
			ipv4_is_lbcast(iph->daddr) || \
			ipv4_is_multicast(iph->daddr) || \
			ipv4_is_zeronet(iph->daddr))
		return NF_ACCEPT;

	if(zyc_roaming_target_device(in)) {

		if(zyc_roaming_ip_valid(iph->saddr)) {
			c = zyc_roaming_client_find(iph->saddr);
			if(!c) {
				zyc_roaming_strTmac(in->mac_patch, rmac);
				zyc_roaming_client_add(iph->saddr, rmac, 0, AUTO_ADD);
			} else if(c->client.cli.if_local) {
				spin_lock_bh(&zyc_roaming_clock);
				c->client.cli.if_local = 0;
				mod_timer(&c->client.expire_timer, jiffies + 60*HZ);
				spin_unlock_bh(&zyc_roaming_clock);
			}
			
			return NF_ACCEPT;
		}

		if(!zyc_roaming_ip_valid(iph->daddr))
			return NF_ACCEPT;

		out_dev = zyc_roaming_client_outdev(iph->daddr, 0);

	} else if(!strcmp(roaming_localdev, in->name)) {

		/* 
		 * only forward the dns-query packet
		 * while daddr is gwip, eg:172.30.22.1
		 * */
		if(!(iph->daddr ^ roaming_ctx.parms.ip_prefix) && !zyc_roaming_dns_packet_check(skb))
			return NF_ACCEPT;

		out_dev = zyc_roaming_client_outdev(iph->saddr, 1);

		/* clamp pmtu for tcp, only process non-fragment packets */
		if(out_dev && \
				iph->protocol == IPPROTO_TCP && \
				!(ntohs(iph->frag_off) & IP_OFFSET))
			if(zyc_roaming_tcp_clamp_mss_to_pmtu(skb, in, out_dev) != 0) {
				return NF_DROP;
			}
	} else {
		out_dev = zyc_roaming_client_outdev(iph->daddr, 0);
	}

	if(out_dev) {
		skb->dev = out_dev;
		skb->roaming_forwarded = 1;
	}

	return NF_ACCEPT;
}

zyc_roaming_local_out_hook(hooknum, ops, skb, in, out, okfn)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct net_device *out_dev;

	if(!zyc_roaming_enabled)
		return NF_ACCEPT;

	/* only handle local out packet */
	if (!skb->sk)
		return NF_ACCEPT;

	if(zyc_roaming_ip_valid(iph->daddr) && !zyc_roaming_dns_packet_check(skb))
		return NF_ACCEPT;

	out_dev = zyc_roaming_client_outdev(iph->daddr, 0);
	if(out_dev) {
		/*pr_info("Recvd pkg from local, sip:%pI4, dip:%pI4, sendto: %s\n", \
				&iph->saddr, &iph->daddr, out_dev->name);
		*/
		skb->dev = out_dev;
		skb->roaming_forwarded = 1;
	}

	return NF_ACCEPT;
}

static struct nf_hook_ops zyc_roaming_ops_prerouting[] __read_mostly = {
	ZYC_IPV4_NF_PRE_ROUTING_HOOK(_zyc_roaming_pre_routing_hook),
};

static struct nf_hook_ops zyc_roaming_ops_local_out[] __read_mostly = {
	ZYC_IPV4_NF_LOCAL_OUT_HOOK(_zyc_roaming_local_out_hook),
};

static int __init zyc_roaming_init(void)
{
	int ret = 0;
	struct netlink_kernel_cfg cfg = {
		.input	= zyc_roaming_read_from_user,
	};

	pr_info("roaming init\n");

	roaming_ctx.sys_zyc_roaming = kobject_create_and_add("zyc_roaming", NULL);
	if(!roaming_ctx.sys_zyc_roaming) {
		pr_err("failed to register zyc_roaming\n");
		goto exit1;
	}

	// init netlink socket
	if(NULL == (roaming_ctx.netlink_fd = netlink_kernel_create(&init_net, NETLINK_ZYC_ROAMING, &cfg))) {
		ret = -1;
		goto exit2;
	}

	// init hashtable to store client info
	client_hashtable.size = 256;
	client_hashtable.table = zyc_roaming_alloc_hashtable(&client_hashtable.size, 1);
	if(!client_hashtable.table) {
		ret = -ENOMEM;
		goto exit3;
	}

	// init hashtable to store router info
	router_hashtable.size = 256;
	router_hashtable.table = zyc_roaming_alloc_hashtable(&router_hashtable.size, 1);
	if(!router_hashtable.table) {
		ret = -ENOMEM;
		goto exit4;
	}
	pr_info("Client hashtable_size:%u, Router hashtable_size:%u\n", client_hashtable.size, router_hashtable.size);

	roaming_ctx.dev_notifier.notifier_call = zyc_roaming_device_event;
	roaming_ctx.dev_notifier.priority = 1;
	register_netdevice_notifier(&roaming_ctx.dev_notifier);

	ret = nf_register_hooks(zyc_roaming_ops_prerouting, ARRAY_SIZE(zyc_roaming_ops_prerouting));
	if (ret < 0) {
		pr_err("can't register nf pre_routing hook: %d\n", ret);
		goto exit5;
	}

	ret = nf_register_hooks(zyc_roaming_ops_local_out, ARRAY_SIZE(zyc_roaming_ops_local_out));
	if (ret < 0) {
		pr_err("can't register nf local_out hook: %d\n", ret);
		goto exit6;
	}

	spin_lock_init(&roaming_ctx.lock);

	return ret;

exit6:
	nf_unregister_hooks(zyc_roaming_ops_prerouting, ARRAY_SIZE(zyc_roaming_ops_prerouting));
exit5:
	unregister_netdevice_notifier(&roaming_ctx.dev_notifier);
	zyc_roaming_free_hashtable(router_hashtable.table, router_hashtable.size);
exit4:
	zyc_roaming_free_hashtable(client_hashtable.table, client_hashtable.size);
exit3:
	netlink_kernel_release(roaming_ctx.netlink_fd);
exit2:
	kobject_put(roaming_ctx.sys_zyc_roaming);
exit1:
	return ret;
}

static void __exit zyc_roaming_clean(void)
{
	int i;
	struct zyc_roaming_router_hashnode *rh;

	if(roaming_ctx.netlink_fd != NULL) {
		netlink_kernel_release(roaming_ctx.netlink_fd);
		roaming_ctx.netlink_fd = NULL;
	}

	zyc_roaming_cleanup();

	spin_lock_bh(&zyc_roaming_rlock);
	for(i = 0; i < router_hashtable.size; i++) {
		while(!hlist_nulls_empty(&router_hashtable.table[i])) {
			rh = hlist_nulls_entry(router_hashtable.table[i].first,
					struct zyc_roaming_router_hashnode, hnnode);
			hlist_nulls_del_rcu(&rh->hnnode);
			dev_put(rh->router->dev);
			kfree(rh->router);
			kfree(rh);
		}
	}

	spin_unlock_bh(&zyc_roaming_rlock);
	zyc_roaming_free_hashtable(client_hashtable.table, client_hashtable.size);
	zyc_roaming_free_hashtable(router_hashtable.table, router_hashtable.size);

	kobject_put(roaming_ctx.sys_zyc_roaming);
	unregister_netdevice_notifier(&roaming_ctx.dev_notifier);
	nf_unregister_hooks(zyc_roaming_ops_prerouting, ARRAY_SIZE(zyc_roaming_ops_prerouting));
	nf_unregister_hooks(zyc_roaming_ops_local_out, ARRAY_SIZE(zyc_roaming_ops_local_out));
}

module_init(zyc_roaming_init);
module_exit(zyc_roaming_clean);
