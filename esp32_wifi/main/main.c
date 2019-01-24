/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
   */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "rom/ets_sys.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "modbus_tcp.h"
#include "battery_check.h"
#include "config_task.h"

#include "main.h"

static uint8_t psensor[] = {SENSOR_OUT1, SENSOR_OUT2, SENSOR_OUT3,
									//SENSOR_OUT4, SENSOR_OUT5, SENSOR_OUT6,
									SENSOR_OUT7, SENSOR_OUT8, SENSOR_OUT9,
									SENSOR_OUT10, SENSOR_OUT11, SENSOR_OUT12};

static uint8_t senso_num = sizeof(psensor)/sizeof(uint8_t);

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t wifi_event_group;

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int IPV4_GOTIP_BIT = BIT0;
const int ESP_WIFI_EVENT_STOP = BIT1;

static const char *TAG = "BGY-Robot";
static uint8_t sensor_status[2] = {0x00, 0x00};

static void wait_for_ip(void);

esp_conf_t system_config;

#if 0
static void wifi_sta_staticip()
{
	tcpip_adapter_ip_info_t ip_info;

	tcpip_adapter_dhcpc_stop(ESP_IF_WIFI_STA);

	IP4_ADDR(&ip_info.ip, 10,110,20,58);
	IP4_ADDR(&ip_info.gw, 10,110,20,1);
	IP4_ADDR(&ip_info.netmask, 255,255,255,0);
	tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
}
#endif

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
	switch(event->event_id) {
		case SYSTEM_EVENT_STA_START:
			esp_wifi_connect();
			xEventGroupClearBits(wifi_event_group, ESP_WIFI_EVENT_STOP);
			break;
		case SYSTEM_EVENT_STA_GOT_IP:
			ESP_LOGI(TAG, "got ip:%s",
					ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
			xEventGroupSetBits(wifi_event_group, IPV4_GOTIP_BIT);
			break;
		case SYSTEM_EVENT_STA_DISCONNECTED:
			esp_wifi_connect();
			xEventGroupClearBits(wifi_event_group, IPV4_GOTIP_BIT);
			break;
		default:
			break;
	}
	return ESP_OK;
}

#if 0
static void wifi_reconfig(const char *ssid, const char *psword)
{
	wifi_config_t sta_config;

	bzero(&sta_config, sizeof(sta_config));
	strncpy((char *)sta_config.sta.ssid, ssid, strlen(ssid));
	strncpy((char *)sta_config.sta.password, psword, strlen(psword));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config) );
	ESP_ERROR_CHECK(esp_wifi_disconnect() );

	wait_for_ip();
}
#endif

static void wifi_init_sta()
{
	wifi_event_group = xEventGroupCreate();

	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	wifi_config_t sta_config;
	bzero(&sta_config, sizeof(sta_config));
	strncpy((char *)sta_config.sta.ssid, system_config.ssid, strlen(system_config.ssid));
	strncpy((char *)sta_config.sta.password, system_config.psword, strlen(system_config.psword));
	ESP_LOGI(TAG, "ssid %s, password %s", sta_config.sta.ssid, sta_config.sta.password);

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config) );
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE) );
	ESP_ERROR_CHECK(esp_wifi_start() );
	//wifi_sta_staticip();

	ESP_LOGI(TAG, "wifi_init_sta finished."); ESP_LOGI(TAG, "Starting connect to ap SSID:%s password:%s",
			system_config.ssid, system_config.psword);
}

static void wait_for_ip()
{
	ESP_LOGI(TAG, "Waiting for AP connection...");
	xEventGroupWaitBits(wifi_event_group, IPV4_GOTIP_BIT, false, true, portMAX_DELAY);
	ESP_LOGI(TAG, "Connected to AP");
}

void gpio_init_conf()
{
	gpio_config_t io_conf;

	// init sensor data gpio
	io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.pin_bit_mask = (SENSOR_SEL_ALL1 | SENSOR_SEL_ALL2);
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pull_up_en = 1;
	io_conf.pull_down_en = 0;
	gpio_config(&io_conf);

	// init sensor power gpio
	io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.pin_bit_mask = SENSOR_POWER_SEL|SENSOR_LED_SEL;
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pull_up_en = 0;
	io_conf.pull_down_en = 0;
	gpio_config(&io_conf);
}

/*
 * sbuf: sensor's status stored in sbuf 
 * one bit present on sensor: 1 near, 0 away
 * */
static void check_sensor_status(uint8_t *sbuf, int cnts)
{
	uint8_t i = 0;
	uint16_t status = 0x00;
	bool nearflag = false;
	bool ledon = false;

	// power on sensor
	gpio_set_level(SENSOR_POWER, 1);

	vTaskDelay(pdMS_TO_TICKS(1)/2);

	portENTER_CRITICAL(&mux);
	for(i = 0; i<senso_num && i<cnts*8; i++) {
		if(gpio_get_level(psensor[i])) {
			status &= ~BIT(i);
		} else {
			nearflag = true;
			status |= BIT(i);
		}

		// light on led
		if(nearflag && !ledon) {
			ledon = true;
			gpio_set_level(SENSOR_LED, 1);
		}
	}
	sbuf[0] = status >> 8;
	sbuf[1] = status & 0xFF;

	portEXIT_CRITICAL(&mux);

	// power off sensor
	gpio_set_level(SENSOR_POWER, 0);

	vTaskDelay(pdMS_TO_TICKS(50));
	// light off led
	if(ledon == true) {
		gpio_set_level(SENSOR_LED, 0);
	}
}

static void self_check(void *pvParameters)
{
	while(1) {
		check_sensor_status(sensor_status, sizeof(sensor_status));
	}

	vTaskDelete(NULL);
}

static void plc_communicate_task(void *pvParameters)
{
	struct md_tcp_ctx ctx;
	uint8_t rbuf[32];
	uint8_t sbuf[4];
	uint8_t *pos;
	uint16_t voltage = 0;

	memset(&ctx, 0, sizeof(ctx));

	while(1) {
		if(md_tcp_init(&ctx, system_config.serip, system_config.serport)) {
			vTaskDelay(pdMS_TO_TICKS(100));
			continue;
		}

		while(1) {
			if(ctx.send(&ctx, MODBUS_SERVER_ID, MODBUS_READ_SINGLE_REGISTERS, system_config.sensors, 2, NULL) <= 0) {
				break;
			}

			vTaskDelay(pdMS_TO_TICKS(20));

			pos = ctx.recv(&ctx, MODBUS_SERVER_ID, MODBUS_READ_SINGLE_REGISTERS, rbuf, sizeof(rbuf));
			if(NULL == pos) {
				if(1 == rbuf[0])
					continue;
				else if(0 == rbuf[0])
					break;
			}

			// new cmd
			if(pos[0] & 0x80) {
				voltage = battery_voltage();
				portENTER_CRITICAL(&mux);
				sbuf[0] = sensor_status[0];
				sbuf[1] = sensor_status[1];
				sbuf[2] = (voltage >> 8) & 0x00FF;
				sbuf[3] = voltage & 0x00FF;
				portEXIT_CRITICAL(&mux);

				if(ctx.send(&ctx, MODBUS_SERVER_ID, MODBUS_WRITE_MULTIPLE_REGISTERS, system_config.sensors, sizeof(sbuf), sbuf) <= 0) {
					break;
				}

			} else {
				// no cmd or request not handled
				vTaskDelay(pdMS_TO_TICKS(20));
			}
		}

		ctx.destroy(&ctx);
		vTaskDelay(pdMS_TO_TICKS(100));
	}

	vTaskDelete(NULL);
}

static void system_config_init()
{
	nvs_handle handle;
	uint32_t ssid_len, psword_len, serip_len;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) { // OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

	memset(&system_config, 0, sizeof(system_config));

	err = nvs_open(CONFIG_NVS_NS, NVS_READONLY, &handle);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
	} else {
		ssid_len = sizeof(system_config.ssid);
		psword_len = sizeof(system_config.psword);
		serip_len = sizeof(system_config.serip);

		if(nvs_get_str(handle, "ssid", system_config.ssid, &ssid_len) != ESP_OK \
				|| nvs_get_str(handle, "psword", system_config.psword, &psword_len) != ESP_OK \
				|| nvs_get_str(handle, "serip", system_config.serip, &serip_len) != ESP_OK \
				|| nvs_get_i16(handle, "serport", &system_config.serport) != ESP_OK \
				|| nvs_get_i16(handle, "sensors", &system_config.sensors) != ESP_OK \
				|| nvs_get_i16(handle, "battery", &system_config.battery) != ESP_OK) {
			err = ESP_ERR_NVS_NOT_FOUND;
		}
	}

	if(err != ESP_OK) {
		strncpy(system_config.ssid, ESP_WIFI_SSID, strlen(ESP_WIFI_SSID));
		strncpy(system_config.psword, ESP_WIFI_PASS, strlen(ESP_WIFI_PASS));
		strncpy(system_config.serip, MODBUS_TCP_SERVER, strlen(MODBUS_TCP_SERVER));
		system_config.serport = MODBUS_TCP_PORT;
		system_config.sensors = MODBUS_SENSOR_ADDR;
		system_config.battery = MODBUS_VOLTAGE_ADDR;
	}

	ESP_LOGI(TAG, "ssid: %s, psword: %s, serip: %s, serport: %d, sensors: %d, battery: %d",
			system_config.ssid, system_config.psword, system_config.serip, system_config.serport,
			system_config.sensors, system_config.battery);
}

/******************************************************************************
 * FunctionName : app_main
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
 *******************************************************************************/
void app_main(void)
{
	printf("SDK version:%s\n", esp_get_idf_version());

	system_config_init();

	gpio_init_conf();

	wifi_init_sta();

	wait_for_ip();

	xTaskCreate(self_check, "self_check", 2048, NULL, 6, NULL);
	xTaskCreate(plc_communicate_task, "plc_communicate", 2048, NULL, 5, NULL);
	xTaskCreate(battery_check_task, "battery_check", 2048, NULL, 5, NULL);
	config_task_init();
}
