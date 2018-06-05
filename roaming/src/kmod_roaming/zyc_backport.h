/*
 * sfe_backport.h
 *	Shortcut forwarding engine compatible header file.
 *
 * Copyright (c) 2014-2016 The Linux Foundation. All rights reserved.
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
#define zyc_define_netfilter_hook(FN_NAME, HOOKNUM, OPS, SKB, IN, OUT, OKFN) \
static unsigned int FN_NAME(void *priv, \
			    struct sk_buff *SKB, \
			    const struct nf_hook_state *state)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0))
#define zyc_define_netfilter_hook(FN_NAME, HOOKNUM, OPS, SKB, IN, OUT, OKFN) \
static unsigned int FN_NAME(const struct nf_hook_ops *OPS, \
			    struct sk_buff *SKB, \
			    const struct net_device *IN, \
			    const struct net_device *OUT, \
			    int (*OKFN)(struct sk_buff *))
#else
#define zyc_define_netfilter_hook(FN_NAME, HOOKNUM, OPS, SKB, IN, OUT, OKFN) \
static unsigned int FN_NAME(unsigned int HOOKNUM, \
			    struct sk_buff *SKB, \
			    const struct net_device *IN, \
			    const struct net_device *OUT, \
			    int (*OKFN)(struct sk_buff *))
#endif

#define zyc_roaming_pre_routing_hook(HOOKNUM, OPS, SKB, IN, OUT, OKFN) \
	zyc_define_netfilter_hook(_zyc_roaming_pre_routing_hook, HOOKNUM, OPS, SKB, IN, OUT, OKFN)
#define zyc_roaming_local_out_hook(HOOKNUM, OPS, SKB, IN, OUT, OKFN) \
	zyc_define_netfilter_hook(_zyc_roaming_local_out_hook, HOOKNUM, OPS, SKB, IN, OUT, OKFN)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
#define ZYC_IPV4_NF_PRE_ROUTING_HOOK(fn) \
	{						\
		.hook = fn,				\
		.pf = NFPROTO_IPV4,			\
		.hooknum = NF_INET_PRE_ROUTING,	\
		.priority = NF_IP_PRI_NAT_DST + 1,	\
	}
#define ZYC_IPV4_NF_LOCAL_OUT_HOOK(fn) \
	{						\
		.hook = fn,				\
		.pf = NFPROTO_IPV4,			\
		.hooknum = NF_INET_LOCAL_OUT,	\
		.priority = NF_IP_PRI_CONNTRACK - 1, \
	}
#else
#define ZYC_IPV4_NF_PRE_ROUTING_HOOK(fn) \
	{						\
		.hook = fn,				\
		.owner = THIS_MODULE,			\
		.pf = NFPROTO_IPV4,			\
		.hooknum = NF_INET_PRE_ROUTING,	\
		.priority = NF_IP_PRI_NAT_DST + 1,	\
	}
#define ZYC_IPV4_NF_LOCAL_OUT_HOOK(fn) \
	{						\
		.hook = fn,				\
		.owner = THIS_MODULE,			\
		.pf = NFPROTO_IPV4,			\
		.hooknum = NF_INET_LOCAL_OUT,	\
		.priority = NF_IP_PRI_CONNTRACK - 1, \
	}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
#define ZYC_DEV_EVENT_PTR(PTR) netdev_notifier_info_to_dev(PTR)
#else
#define ZYC_DEV_EVENT_PTR(PTR) (struct net_device *)(PTR)
#endif
