#ifndef __ESP_CONFIG_H__
#define __ESP_CONFIG_H__

#define IP_SIZE 16
#define BROADCAST_PORT	19998
#define SERVER_PORT		19999

struct esp_msg {
	short serport;
	char ssid[32];
	char psword[32];
};

struct msg_resp {
	char status;
};

#define MAC_FMT		"%02x%02x%02x%02x%02x%02x"
#define UNCHAR(c)	((unsigned char)(c))
#define MAC_STR(m)	UNCHAR(m[0]),UNCHAR(m[1]),UNCHAR(m[2]),UNCHAR(m[3]),UNCHAR(m[4]),UNCHAR(m[5])
#define MAC_SIZE	18

#endif
