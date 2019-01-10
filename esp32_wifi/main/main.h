#ifndef __MAIN_H__
#define __MAIN_H__

#define SENSOR_OUT1			GPIO_NUM_5
#define SENSOR_OUT2			GPIO_NUM_18
#define SENSOR_OUT3			GPIO_NUM_19
#define SENSOR_OUT4			GPIO_NUM_35
#define SENSOR_OUT5			GPIO_NUM_32
#define SENSOR_OUT6			GPIO_NUM_33
#define SENSOR_OUT7			GPIO_NUM_14
#define SENSOR_OUT8			GPIO_NUM_12
#define SENSOR_OUT9			GPIO_NUM_13
#define SENSOR_OUT10		GPIO_NUM_25
#define SENSOR_OUT11		GPIO_NUM_26
#define SENSOR_OUT12		GPIO_NUM_27

#define SENSOR_OUT1_SEL			(1ULL<<SENSOR_OUT1)
#define SENSOR_OUT2_SEL			(1ULL<<SENSOR_OUT2)
#define SENSOR_OUT3_SEL			(1ULL<<SENSOR_OUT3)
#define SENSOR_OUT4_SEL			((uint64_t)((uint64_t)1<<SENSOR_OUT4))
#define SENSOR_OUT5_SEL			((uint64_t)((uint64_t)1<<SENSOR_OUT5))
#define SENSOR_OUT6_SEL			((uint64_t)((uint64_t)1<<SENSOR_OUT6))
#define SENSOR_OUT7_SEL			(1ULL<<SENSOR_OUT7)
#define SENSOR_OUT8_SEL			(1ULL<<SENSOR_OUT8)
#define SENSOR_OUT9_SEL			(1ULL<<SENSOR_OUT9)
#define SENSOR_OUT10_SEL		(1ULL<<SENSOR_OUT10)
#define SENSOR_OUT11_SEL		(1ULL<<SENSOR_OUT11)
#define SENSOR_OUT12_SEL		(1ULL<<SENSOR_OUT12)

#define SENSOR_SEL_ALL1		(SENSOR_OUT1_SEL|SENSOR_OUT2_SEL|SENSOR_OUT3_SEL|SENSOR_OUT7_SEL \
							|SENSOR_OUT8_SEL|SENSOR_OUT9_SEL|SENSOR_OUT10_SEL|SENSOR_OUT11_SEL|SENSOR_OUT12_SEL)

#define SENSOR_SEL_ALL2		(SENSOR_OUT4_SEL|SENSOR_OUT5_SEL|SENSOR_OUT6_SEL)

#define SENSOR_NUM	9

#define SENSOR_POWER		GPIO_NUM_15
#define SENSOR_POWER_SEL	(1ULL<<SENSOR_POWER)

#define SENSOR_LED			GPIO_NUM_22
#define SENSOR_LED_SEL		(1ULL<<SENSOR_LED)

//#define ESP_WIFI_SSID		"BGY-Robot"
#define ESP_WIFI_SSID		"HUAWEI-ESPWIFI"
#define ESP_WIFI_PASS		"bgy2018@"
//#define ESP_WIFI_SSID		"TP-LINK_yungui"
//#define ESP_WIFI_PASS		"88888888"
#define PLC_COMMAND			"GET"

#ifndef BIT
#define BIT(x) (1 << (x))
#endif

#define LISTEN_PORT			4998
#define TCP_SERVER_SELECT_SEC	5
#define TCP_SERVER_SELECT_USEC	0

#define CONFIG_PORT	19998
struct esp_msg {
	short serport;
	char ssid[32];
	char psword[32];
};
typedef struct esp_msg esp_msg_t;

struct msg_resp {
	char status;
};
typedef struct msg_resp msg_resp_t;

#endif
