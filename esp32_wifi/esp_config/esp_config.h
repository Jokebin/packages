#ifndef __ESP_CONFIG_H__
#define __ESP_CONFIG_H__

#include "list.h"

#define IP_SIZE 16
#define CLIENT_PORT	19998
#define SERVER_PORT	19999

enum cmd_t {
	CMD_UPDATE = 1,
	CMD_CONFIG,
};

struct esp_cmd {
	enum cmd_t cmd;
	char server[64];
	short port;
};

struct data_item {
	char name;
	uint8_t len;
	char data[1];
};

enum request_t {
	T_DATA = 1,
	T_OK,
	T_FAIL,
};

struct esp_conf_request {
	char mac[18];
	enum request_t type;
};

struct esp_conf {
	char ssid[32];
	char psword[32];
	char serip[16];
	short serport;
	short sensors;	//sensors register addr
	short battery;	//battery status addr
};

struct esp_update_request {
	enum request_t type;
	int index;
	int len;
};

struct esp_update {
	int index;
	int len;
	int remain;
	char data[1];
};

struct msg_resp {
	char status;
};

enum status_t {
	ST_INIT = 1,
	ST_SUCCEED,
	ST_FAILED,
	ST_PROCESS,
};

struct list_t {
	struct list_head list;
	char mac[18];
	char ip[16];
	enum status_t status;
	struct esp_conf info;
};

typedef struct esp_cmd esp_cmd_t;
typedef struct esp_conf esp_conf_t;
typedef struct msg_resp msg_resp_t;
typedef struct esp_update esp_update_t;

#define MAC_FMT		"%02x%02x%02x%02x%02x%02x"
#define UNCHAR(c)	((unsigned char)(c))
#define MAC_STR(m)	UNCHAR(m[0]),UNCHAR(m[1]),UNCHAR(m[2]),UNCHAR(m[3]),UNCHAR(m[4]),UNCHAR(m[5])
#define MAC_SIZE	18

#endif
