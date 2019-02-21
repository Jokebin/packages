#ifndef __CONFIG_TASK_H__
#define __CONFIG_TASK_H__

#define CONFIG_NVS_NS	"config_info"
#define CONFIG_PORT	19998
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

typedef struct esp_cmd esp_cmd_t;
typedef struct esp_conf esp_conf_t;
typedef struct msg_resp msg_resp_t;
typedef struct esp_update esp_update_t;

void config_task_init();

#endif
