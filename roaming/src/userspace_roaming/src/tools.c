#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "log.h"
#include "roaming.h"
#include "tools.h"

void _ip6Tomac(struct in6_addr *ip6, u8 *r)
{
	r[0] = ip6->s6_addr[8];
	r[1] = ip6->s6_addr[9];
	r[2] = ip6->s6_addr[10];
	r[3] = ip6->s6_addr[13];
	r[4] = ip6->s6_addr[14];
	r[5] = ip6->s6_addr[15];
	r[0] &= 0xFD;
}

u8 *dec_to_ipstr(u8 *dec)
{
	static u8 str[MAC_BUF];

	memset(str, 0, sizeof(str));
	snprintf(str, IP4_BUF, IP4_FMT, IP4_STR(dec));
	return str;
}

u8 *ipstr_to_dec(u8 *str)
{
	int i = 0, t = 0;
	static u8 dec[4];

	memset(dec, 0, sizeof(dec));
	while(1) {
		if(*str == '.') {
			dec[i++] = t;
			t = 0;
		} else if(*str == '\0') {
			dec[i++] = t;
			break;
		} else {
			t = t*10 + *str - '0';
		}

		str++;

		if(t > 255) {
			LOG(LOG_WARNING, "invalid ip address: %c, %d, t:%d, i:%d", *str, __LINE__, t, i);
			return NULL;
		}
	}

	if(i != 4) {
		LOG(LOG_WARNING, "invalid ip address: %d, %s", __LINE__, str);
		return NULL;
	}

	return dec;
}

u8 *macstr_to_dec(u8 *str)
{
	u8 s = 0, t = 0, i = 0;
	static u8 buf[6];

	memset(buf, 0, 6);

	while(1) {
		if(*str >= '0' && *str <= '9') {
			t = *str - '0';
		} else if(*str >= 'a' && *str <= 'f') {
			t = *str - 'a' + 10;
		} else if(*str >= 'A' && *str <= 'F') {
			t = *str - 'A' + 10;
		} else if(*str == ':') {
			str++;
			continue;
		}

		s = s*16 + t;
		if(!(++i % 2)) {
			buf[i/2 - 1] = s;
			s = 0;
		}

		if(*str == '\0' || (i/2 -1) == 5)
			break;
		str++;
	}

	return buf;
}

u32 Hex2Int(u8 *hex, u32 cnt)
{
	int c = 0, m = 0;

	while(*hex && cnt-- > 0) {
		if('0' <= *hex && *hex <= '9') {
			m = *hex - '0';
		} else if('a' <= *hex && *hex <= 'f') {
			m = *hex - 'a' + 10;
		} else if('A' <= *hex && *hex <= 'F') {
			m = *hex - 'A' + 10;
		}

		c = c*16 + m;	
		hex++;
	}

	return c;
}
