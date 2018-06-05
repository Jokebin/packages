#ifndef __TOOLS_H__
#define __TOOLS_H__

#include <netinet/in.h>
#include "msg_proto.h"

u8 *dec_to_ipstr(u8 *dec);
u8 *macstr_to_dec(u8 *str);
u8 *ipstr_to_dec(u8 *str);

u32 Hex2Int(u8 *hex, u32 cnt);
void _ip6Tomac(struct in6_addr *ip6, u8 *r);

#endif
