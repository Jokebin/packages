/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
   */

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "rom/ets_sys.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "esp_system.h"


#define GPIO_PIN5	5	//use gpio5-->D1 on board

#define ESP_WIFI_SSID		"BGY-Robot"
#define ESP_WIFI_PASS		"bgy2018@"
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

#define MAX_CLIENTS	10
int clients[MAX_CLIENTS];

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
	ESP_ERROR_CHECK(esp_wifi_start() );

	ESP_LOGI(TAG, "wifi_init_sta finished.");
	ESP_LOGI(TAG, "connect to ap SSID:%s password:%s",
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

static int add_clients(int fd)
{
	int i = 0;
	for(; i<MAX_CLIENTS; i++) {
		if(clients[i] == -1) {
			clients[i] = fd;
			return 0;
		}
	} 

	return 1;
}

static void close_client(int *client)
{
	if(*client > 0) {
		shutdown(*client, 0);
		close(*client);
		*client = -1;
	}
}

/*
 * command[0]: 0/1 normal, 3 invalid command
 * */
static void handler_client_command(char *command, int len)
{
	if(!strncmp(command, PLC_COMMAND, len))
		command[0] = gpio_get_level(GPIO_PIN5);
	else
		command[0] = 3;

	command[1] = '\0';
}

static void tcp_server_task(void *pvParameters)
{
	char rx_buffer[128];
	char addr_str[128];
	int addr_family;
	int ip_protocol;

	//clear clients
	int i = 0;
	for(; i<MAX_CLIENTS; i++)
		clients[i] = -1;

	while (1) {

recovery:
		wait_for_ip();

		struct sockaddr_in destAddr;
		destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		destAddr.sin_family = AF_INET;
		destAddr.sin_port = htons(LISTEN_PORT);
		addr_family = AF_INET;
		ip_protocol = IPPROTO_IP;
		inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);

		int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
		if (listen_sock < 0) {
			ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
			break;
		}
		ESP_LOGI(TAG, "Socket created");

		int err = bind(listen_sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
		if (err != 0) {
			ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
			break;
		}
		ESP_LOGI(TAG, "Socket binded");

		err = listen(listen_sock, 1);
		if (err != 0) {
			ESP_LOGE(TAG, "Error occured during listen: errno %d", errno);
			break;
		}
		ESP_LOGI(TAG, "Socket listening");

		add_clients(listen_sock);

		fd_set fds;
		struct timeval tv;
		int max_fd = 0;

		while (1) {

			FD_ZERO(&fds);
			tv.tv_sec = TCP_SERVER_SELECT_SEC;
			tv.tv_usec = TCP_SERVER_SELECT_USEC;

			for(i=0; i<MAX_CLIENTS; i++) {
				if(clients[i] != -1) {
					FD_SET(clients[i], &fds);
					if(clients[i] > max_fd)
						max_fd = clients[i];
				}
			}

			int result = select(max_fd+1, &fds, NULL, NULL, &tv);

			switch(result) {
				case 0:
					// select timeout, no client connected
					break;
				case -1:
					ESP_LOGE(TAG, "Error occured during selecting: errno %d", errno);
					// close all socks, and wait for wifi connected
					for(i=0; i<MAX_CLIENTS; i++) {
						if(clients[i] != -1)
							close_client(&clients[i]);
					}
					goto recovery;
				default:
					for(i=0; i<MAX_CLIENTS; i++) {
						if(clients[i] == -1)
							continue;

						if(i == 0 && FD_ISSET(clients[i], &fds)) {
							int new_fd = accept(clients[i], NULL, NULL);
							if (add_clients(new_fd) == 1) {
								// clients buffer is full
								close(new_fd);
							}
							continue;
						}

						// recv msg from client
						if(FD_ISSET(clients[i], &fds)) {
							int len = recv(clients[i], rx_buffer, sizeof(rx_buffer) - 1, 0);
							// Error occured during receiving
							if (len < 0) {
								ESP_LOGE(TAG, "recv failed: errno %d", errno);
								close_client(&clients[i]);
								break;
							}
							// Connection closed
							else if (len == 0) {
								ESP_LOGI(TAG, "Connection closed");
								close_client(&clients[i]);
								break;
							}
							// Data received
							else {
								rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
								//ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);

								// handler client's command
								handler_client_command(rx_buffer, len);

								// response to client 
								int err = send(clients[i], rx_buffer, strlen(rx_buffer), 0);
								if (err < 0) {
									ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
									close_client(&clients[i]);
									break;
								}
							}
						}
					}
					break;
			}
		}

		if (listen_sock != -1) {
			ESP_LOGE(TAG, "Shutting down socket and restarting...");
			close_client(&listen_sock);
		}
	}
	vTaskDelete(NULL);
}

static void udp_server_task(void *pvParameters)
{
	char rx_buffer[128];
	char addr_str[128];
	int addr_family;
	int ip_protocol;

	while (1) {

recovery:
		wait_for_ip();

		struct sockaddr_in destAddr;
		destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		destAddr.sin_family = AF_INET;
		destAddr.sin_port = htons(LISTEN_PORT);
		addr_family = AF_INET;
		ip_protocol = IPPROTO_IP;
		inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);

		int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
		if (sock < 0) {
			ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
			goto recovery;
		}
		ESP_LOGI(TAG, "Socket created");

		int err = bind(sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
		if (err < 0) {
			ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
			close(sock);
			goto recovery;
		}
		ESP_LOGI(TAG, "Socket binded");

		while (1) {

			//ESP_LOGI(TAG, "Waiting for data");
			struct sockaddr_in sourceAddr;
			socklen_t socklen = sizeof(sourceAddr);
			int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&sourceAddr, &socklen);

			// Error occured during receiving
			if (len < 0) {
				ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
				close_client(&sock);
				goto recovery;
			}
			// Data received
			else {
				inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);

				rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string...
				//ESP_LOGI(TAG, "UDP received %d bytes from %s:", len, addr_str);
				//ESP_LOGI(TAG, "%s", rx_buffer);

				// handler client's command
				handler_client_command(rx_buffer, len);

				int err = sendto(sock, rx_buffer, strlen(rx_buffer), 0, (struct sockaddr *)&sourceAddr, sizeof(sourceAddr));
				if (err < 0) {
					ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
					close_client(&sock);
					goto recovery;
				}
			}
		}

		if (sock != -1) {
			ESP_LOGE(TAG, "Shutting down socket and restarting...");
			close_client(&sock);
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

	gpio_init_conf(GPIO_PIN5);

	wifi_init_sta();

	xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);
	xTaskCreate(udp_server_task, "udp_server", 4096, NULL, 5, NULL);
}
