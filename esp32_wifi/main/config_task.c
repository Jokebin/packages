#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event_loop.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"

#include "config_task.h"

#define TAG	"CONFIG"

/****************************************************************************
 * OTA	HANDLE
****************************************************************************/
#define OTA_BUF_SIZE 1400
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

static esp_err_t esp_udp_ota(char *server, short port)
{
	int sockfd = -1;
	uint8_t mac[6];
	struct sockaddr_in saddr, raddr;
	struct timeval timeout = {5, 0};
	socklen_t sklen = sizeof(raddr);
	
	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if(sockfd < 0) {
		ESP_LOGE(TAG, "Failed to create socket, Error %d", errno);
		return ESP_FAIL;
	}

	if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout))) {
		ESP_LOGE(TAG, "Failed to set recv timeout, Error %d", errno);
		return ESP_FAIL;
	}

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);

	if(inet_pton(AF_INET, server, &saddr.sin_addr) != 1) {
		close(sockfd);
		ESP_LOGE(TAG, "invalid server address, config failed!!");
		return ESP_FAIL;
	}

    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = NULL;
    ESP_LOGI(TAG, "Starting OTA...");

    update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Passive OTA partition not found");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, update_partition->address);

    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed, error=%d", err);
        return err;
    }
    ESP_LOGI(TAG, "esp_ota_begin succeeded");
    ESP_LOGI(TAG, "Please Wait. This may take time");

    esp_err_t ota_write_err = ESP_OK;
    char *upgrade_data_buf = (char *)malloc(OTA_BUF_SIZE);
    if (!upgrade_data_buf) {
        ESP_LOGE(TAG, "Couldn't allocate memory to upgrade data buffer");
        return ESP_ERR_NO_MEM;
    }

	int cnts = 0;
    int binary_file_len = 0;
	esp_update_t *update = (esp_update_t *)upgrade_data_buf;

	bool request_need = true;
	struct esp_update_request update_request;
	update_request.type = T_DATA;
	update_request.len = htonl(OTA_BUF_SIZE - sizeof(esp_update_t) + 1);
    while (1) {
		// send request
		update_request.index = htonl(binary_file_len);
		if(request_need) {
			cnts = sendto(sockfd, &update_request, sizeof(update_request), 0, (struct sockaddr *)&saddr, sklen);
			if(cnts <= 0) {
        	    ESP_LOGI(TAG, "send failed, retry");
				break;
			}
		}

		vTaskDelay(pdMS_TO_TICKS(2));
        cnts = recvfrom(sockfd, upgrade_data_buf, OTA_BUF_SIZE, 0, (struct sockaddr *)&raddr, &sklen);

		if(cnts < 0 && errno == EAGAIN) {
			if(request_need != true)
				request_need = true;
			continue;
		} else if(cnts < 0) { 
			ESP_LOGI(TAG, "recvfrom failed!");
			break;
		} else if(0 == cnts) {
			continue;
		}

		// recved retransmission package, recv again
		if(ntohl(update->index) == binary_file_len - ntohl(update->len)) {
			request_need = false;
			continue;
		}

		request_need = true;

		if(update->index != update_request.index) {
			ESP_LOGI(TAG, "%d != %d, index not match, retry!", ntohl(update->index), ntohl(update_request.index));
			continue;
		}

		//ESP_LOGI(TAG, "%s:%d, recvd: %d, update_index: %d, update_len: %d, update_remain: %d",
		//		__func__, __LINE__, cnts, ntohl(update->index), ntohl(update->len), ntohl(update->remain));

		cnts = cnts - sizeof(*update) + 1;
		if(ntohl(update->len) != cnts) {
			ESP_LOGI(TAG, "recvd data imcomplete, retry!");
			continue;
		}

        ota_write_err = esp_ota_write(update_handle, (const void *)&update->data[0], cnts);
        if (ota_write_err != ESP_OK) {
			break;
		}
		binary_file_len += cnts;
		ESP_LOGD(TAG, "Written image length %d", binary_file_len);

		if(ntohl(update->remain) == 0) {
			ESP_LOGI(TAG, "total data recved!");
			break;
		}
    }
    free(upgrade_data_buf);
    ESP_LOGD(TAG, "Total binary data length writen: %d", binary_file_len);

	esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);

	char ret_buf[sizeof(struct esp_update_request) + 18];
	struct esp_update_request *request = (struct esp_update_request *)ret_buf;

	memset(ret_buf, 0, sizeof(ret_buf));
	snprintf(&ret_buf[sizeof(struct esp_update_request)], 17, "%02x:%02x:%02x:%02x:%02x:%02x",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    esp_err_t ota_end_err = esp_ota_end(update_handle);
    if (ota_write_err != ESP_OK) {
        ESP_LOGE(TAG, "Error: esp_ota_write failed! err=0x%d", err);
		err = ota_write_err;
		goto errout;
    } else if (ota_end_err != ESP_OK) {
        ESP_LOGE(TAG, "Error: esp_ota_end failed! err=0x%d. Image is invalid", ota_end_err);
		err = ota_end_err;
		goto errout;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed! err=0x%d", err);
		goto errout;
    }

	request->type = T_OK;
	sendto(sockfd, ret_buf, sizeof(ret_buf), 0, (struct sockaddr *)&saddr, sklen);
    ESP_LOGI(TAG, "esp_ota_set_boot_partition succeeded"); 

    return ESP_OK;

errout:
	if(sockfd != -1) {
		request->type = T_FAIL;
		sendto(sockfd, ret_buf, sizeof(ret_buf), 0, (struct sockaddr *)&saddr, sklen);
		close(sockfd);
	}

	return err;
}

static void update_handle(char *server, short port)
{
	ESP_LOGI(TAG, "ESP UPDATE SERVER IS %s:%d", server, port);

    esp_err_t ret = esp_udp_ota(server, port);
    if (ret == ESP_OK) {
    	ESP_LOGI(TAG, "Finishing OTA...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Firmware Upgrades Failed");
    }
}
/****************************************************************************/

static void config_handle(char *server, short port)
{
	ESP_LOGI(TAG, "ESP CONFIG SERVER IS %s:%d", server, port);

	int ret = -1;
	int sockfd = -1;
	int serfd = -1;
	char buf[sizeof(esp_conf_t) + 1];
	struct sockaddr_in saddr;
	uint8_t mac[6];
	struct esp_conf_request request;
	nvs_handle config_handle;
	esp_conf_t *config = NULL;

	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if(sockfd < 0) {
		ESP_LOGE(TAG, "Failed to create socket, Error %d", errno);
		return;
	}

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);

	if(inet_pton(AF_INET, server, &saddr.sin_addr) != 1) {
		close(sockfd);
		ESP_LOGE(TAG, "invalid server address, config failed!!");
		return;
	}

	esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
	snprintf(request.mac, sizeof(request.mac), "%02x:%02x:%02x:%02x:%02x:%02x",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	request.mac[sizeof(request.mac)] = '\0';
	ret = sendto(sockfd, &request, sizeof(request), 0, (struct sockaddr *)&saddr, sizeof(saddr));
	if(ret < 0 || !ret) {
		ESP_LOGE(TAG, "send config request to %s failed, config failed!!", server);
		goto out;
	}

	memset(buf, 0, sizeof(buf));

	config = (esp_conf_t *)buf;

	ret = recv(sockfd, buf, sizeof(buf), 0);
	if(ret < 0 || !ret) {
		ESP_LOGE(TAG, "recv conf info from %s failed, config failed!!", server);
	} else {
		config->serport = ntohs(config->serport);
		config->sensors = ntohs(config->sensors);
		config->battery = ntohs(config->battery);
		ESP_LOGI(TAG, "ssid: %s, psword: %s, serip: %s, serport: %d, sensors: %d, battery: %d",
				config->ssid, config->psword, config->serip, config->serport, config->sensors, config->battery);

		ret = nvs_open(CONFIG_NVS_NS, NVS_READWRITE, &config_handle);
		if(ret != ESP_OK) {
			ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(ret));
		} else {
			nvs_set_str(config_handle, "ssid", config->ssid);
			nvs_set_str(config_handle, "psword", config->psword);
			nvs_set_str(config_handle, "serip", config->serip);
			nvs_set_i16(config_handle, "serport", config->serport);
			nvs_set_i16(config_handle, "sensors", config->sensors);
			nvs_set_i16(config_handle, "battery", config->battery);

			nvs_commit(config_handle);
			nvs_close(config_handle);

			esp_restart();
		}
	}

out:
	close(sockfd);
	close(serfd);
}

static void config_task(void *pvParameters)
{
	int err = -1;
	int len = -1;
	int sockfd = -1;
	struct sockaddr_in saddr, raddr;
	char rbuf[512];
	esp_cmd_t *pcmd = (esp_cmd_t *)rbuf;

	socklen_t sklen = sizeof(struct sockaddr_in);

	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if(sockfd < 0) {
		ESP_LOGE(TAG, "Failed to create socket, Error %d", errno);
		return;
	}

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(CONFIG_PORT);
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);

	err = bind(sockfd, (struct sockaddr *)&saddr, sizeof(struct sockaddr));
	if(err < 0) {
		ESP_LOGE(TAG, "Failed to bind socket, Error %d", errno);
		goto err;
	}

	while(1) {
		len = recvfrom(sockfd, rbuf, sizeof(rbuf)-1, 0, (struct sockaddr *)&raddr, &sklen);
		if(-1 == len || !len)
			break;

		rbuf[len] = '\0';

		if(len < sizeof(esp_cmd_t))
			continue;

		switch(pcmd->cmd) {
			case CMD_UPDATE:
				update_handle(pcmd->server, ntohs(pcmd->port));
				break;
			case CMD_CONFIG:
				config_handle(pcmd->server, ntohs(pcmd->port));
				break;
			default:
				break;
		}
		vTaskDelay(pdMS_TO_TICKS(100));
	}

err:
	close(sockfd);
	vTaskDelete(NULL);
}

void config_task_init()
{
	xTaskCreate(config_task, "config_task", 8192, NULL, 5, NULL);
}
