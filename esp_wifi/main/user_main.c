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

#define GPIO_PIN5	5	//use gpio5-->D1 on board

//#define ESP_WIFI_SSID		"BGY-Robot"
//#define ESP_WIFI_PASS		"bgy2018@"
#define ESP_WIFI_SSID		"TP-LINK_yungui"
#define ESP_WIFI_PASS		"88888888"
#define PLC_COMMAND			"GET"

#define LISTEN_PORT			4998
#define TCP_SERVER_SELECT_SEC	5
#define TCP_SERVER_SELECT_USEC	0

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int IPV4_GOTIP_BIT = BIT0;

static const char *TAG = "BGY-Robot";

static void wifi_sta_staticip()
{
	tcpip_adapter_ip_info_t ip_info;

	tcpip_adapter_dhcpc_stop(ESP_IF_WIFI_STA);

	IP4_ADDR(&ip_info.ip, 192,168,3,101);
	IP4_ADDR(&ip_info.gw, 192,168,3,1);
	IP4_ADDR(&ip_info.netmask, 255,255,255,0);
	tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
	switch(event->event_id) {
		case SYSTEM_EVENT_STA_START:
			esp_wifi_connect();
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

void wifi_init_sta()
{
	wifi_event_group = xEventGroupCreate();

	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	wifi_config_t wifi_config = {
		.sta = {
			.ssid = ESP_WIFI_SSID,
			.password = ESP_WIFI_PASS
		},
	};

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE) );
	ESP_ERROR_CHECK(esp_wifi_start() );
	wifi_sta_staticip();

	ESP_LOGI(TAG, "wifi_init_sta finished.");
	ESP_LOGI(TAG, "Starting connect to ap SSID:%s password:%s",
			ESP_WIFI_SSID, ESP_WIFI_PASS);
}

static void wait_for_ip()
{
	ESP_LOGI(TAG, "Waiting for AP connection...");
	xEventGroupWaitBits(wifi_event_group, IPV4_GOTIP_BIT, false, true, portMAX_DELAY);
	ESP_LOGI(TAG, "Connected to AP");
}

void gpio_init_conf(gpio_num_t num)
{
	gpio_config_t io_conf;

	io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.pin_bit_mask = (1UL<<num);
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pull_up_en = 1;
	gpio_config(&io_conf);
}

/*
 * command[0]: 0/1 normal, 3 invalid command
 * */
static void handle_request(char *command, int len)
{
	if(!strncmp(command, PLC_COMMAND, len)) {
		if(gpio_get_level(GPIO_PIN5)) {
			usleep(10000);
			if(gpio_get_level(GPIO_PIN5)) {
				command[0] = '0';
			} else {
				command[0] = '1';
			}
		} else {
			usleep(10000);
			if(!gpio_get_level(GPIO_PIN5)) {
				command[0] = '1';
			} else {
				command[0] = '0';
			}
		}
	} else {
		command[0] = '3';
	}

	command[1] = '\0';
}

static void udp_server_task(void *pvParameters)
{
	char rx_buffer[128];
	char addr_str[128];

	while (1) {

		struct sockaddr_in destAddr;
		destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		destAddr.sin_family = AF_INET;
		destAddr.sin_port = htons(LISTEN_PORT);

		int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
		if (sock < 0) {
			ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
			continue;
		}
		ESP_LOGI(TAG, "Socket created");

		int err = bind(sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
		if (err < 0) {
			ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
			close(sock);
			continue;
		}
		ESP_LOGI(TAG, "Socket binded");

		while (1) {

			//ESP_LOGI(TAG, "Waiting for data");
			struct sockaddr_in sourceAddr;
			socklen_t socklen = sizeof(sourceAddr);
			int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&sourceAddr, &socklen);

			if (len < 0) {
				ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
				break;
			} else {
				inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);

				rx_buffer[len] = '\0';
				//ESP_LOGI(TAG, "UDP received %d bytes from %s: %s", len, addr_str, rx_buffer);

				handle_request(rx_buffer, len);

				int err = sendto(sock, rx_buffer, strlen(rx_buffer), 0, (struct sockaddr *)&sourceAddr, sizeof(sourceAddr));
				if (err < 0) {
					ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
					break;
				}
			}
		}

		if (sock != -1) {
			ESP_LOGE(TAG, "Shutting down socket and restarting...");
			shutdown(sock, 0);
			close(sock);
		}
	}
	vTaskDelete(NULL);
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

    ESP_ERROR_CHECK( nvs_flash_init() );

	gpio_init_conf(GPIO_PIN5);

	wifi_init_sta();
	wait_for_ip();

	xTaskCreate(udp_server_task, "udp_server", 4096, NULL, 5, NULL);
}
