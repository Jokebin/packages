/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
   */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_system.h" 

#define GPIO_INPUT_IO_5		5
#define GPIO_INPUT_PIN_SEL	(1UL<<GPIO_INPUT_IO_5)

#define BUF_SIZE (1024)

static xQueueHandle gpio_evt_queue = NULL;

static uint8_t poweron_hex[][10] = {
	{0x30, 0xcf, 0xf0, 0xf0, 0xf0, 0xf0, 0xc3, 0xa5, 0x69, 0x78},
	{0x30, 0xcf, 0xe1, 0xe1, 0xe1, 0xe1, 0xc3, 0xa5, 0x69, 0x78},
	{0x30, 0xcf, 0x0f, 0xc3, 0xf0, 0x78, 0x78, 0x78, 0x78, 0x78},
	{0x30, 0xcf, 0x0f, 0xc3, 0xf0, 0x78, 0x78, 0x78, 0x78, 0x78},
	{0x30, 0xcf, 0x0f, 0xc3, 0xf0, 0x78, 0x78, 0x78, 0x78, 0x78},
	{0x30, 0xcf, 0x0f, 0xc3, 0xf0, 0x78, 0x78, 0x78, 0x78, 0x78},
};
static uint8_t wait_startup_1[] = {0xb0, 0x4f, 0xf0, 0xb4, 0xf0, 0xf0, 0xf0, 0x96, 0xf0, 0xf0}; //poweron but not startup, send 5 times every 100ms
//static uint8_t not_start_2[] = {0xb0, 0x4f, 0xf0, 0xb4, 0xd2, 0xf0, 0x78, 0xf0, 0xf0, 0xf0}; //poweron but not startup, response 
static uint8_t startup[] = {0xb0, 0x4f, 0xf0, 0x3c, 0xf0, 0xf0, 0xf0, 0x96, 0xf0, 0xf0}; //startup, send 5 times
static uint8_t power500w_1[] = {0xb0, 0x4f, 0xa5, 0x78, 0x3c, 0xf0, 0xf0, 0x96, 0xf0, 0xf0}; //500w, send 1 time
static uint8_t power500w_2[] = {0xb0, 0x4f, 0xa5, 0x78, 0xb4, 0xf0, 0xf0, 0x96, 0xf0, 0xf0}; //500w, send 5 times
static uint8_t power500w_3[] = {0xb0, 0x4f, 0xa5, 0x78, 0xb4, 0xe1, 0xf0, 0x96, 0xf0, 0xf0}; //500w and fan on, send 10 times
static uint8_t power2100w[] = {0xb0, 0x4f, 0xc3, 0x69, 0xb4, 0xe1, 0xf0, 0x96, 0xf0, 0xf0}; //2100w, send 20s every 100ms
static uint8_t stop_output[] = {0xb0, 0x4f, 0xf0, 0xb4, 0xf0, 0xf0, 0xf0, 0x96, 0xf0, 0xf0}; //stop output, send 5 times
static uint8_t start_fan[] = {0xb0, 0x4f, 0xf0, 0xb4, 0xf0, 0xe1, 0xf0, 0x96, 0xf0, 0xf0}; //fan on, send 30s every 100ms
static uint8_t stop_fan[] = {0xb0, 0x4f, 0xf0, 0xb4, 0xf0, 0xf0, 0xf0, 0x96, 0xf0, 0xf0}; //fan on, send 30s every 100ms

static void wait_startup_func()
{
    // Configure a temporary buffer for the incoming data
    uint8_t *data = (uint8_t *) malloc(BUF_SIZE);
	uint32_t len = 0;
	uint8_t i = 0;
	while(1 && i<5) {
		len = uart_read_bytes(UART_NUM_0, data, BUF_SIZE, 20 / portTICK_RATE_MS);
		if(!len)
			continue;

		if(!memcmp(data, poweron_hex[i], sizeof(poweron_hex[i]))) {
			i++;
		}
	}
	vTaskDelay(60 / portTICK_RATE_MS);

	for(i=0; i<5; i++) {
		uart_write_bytes(UART_NUM_0, (const char *)wait_startup_1, sizeof(wait_startup_1));
		vTaskDelay(100 / portTICK_RATE_MS);
	}
}

static void control_cmd(uart_port_t uart, uint8_t *cmd, uint8_t size, uint16_t times, uint8_t intval)
{
	int i = 0;
	for(; i<times; i++) {
		uart_write_bytes(uart, (const char *)cmd, size);
		vTaskDelay(intval / portTICK_RATE_MS);
	}
}

static void control_task()
{
    // Configure parameters of an UART driver,
    // communication pins and install the driver
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, BUF_SIZE * 2, 0, 0, NULL);

	// Waiting tartup
	wait_startup_func();

	// Startup
	control_cmd(UART_NUM_0, startup, sizeof(startup), 5, 100);

	// Power500w_1
	control_cmd(UART_NUM_0, power500w_1, sizeof(power500w_1), 1, 100);
	// Power500w_2
	control_cmd(UART_NUM_0, power500w_2, sizeof(power500w_2), 5, 100);
	// Power500w_3
	control_cmd(UART_NUM_0, power500w_3, sizeof(power500w_3), 10, 100);
	
	// Power2100w and fan on 
	control_cmd(UART_NUM_0, power2100w, sizeof(power2100w), 200, 100);
	
	// Stop output
	control_cmd(UART_NUM_0, stop_output, sizeof(stop_output), 5, 100);
	
	// Fan on
	control_cmd(UART_NUM_0, start_fan, sizeof(start_fan), 300, 100);

	uint32_t io_num;
    while (1) {
		// Fan stop
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
			if(gpio_get_level(io_num) == 1) {
				control_cmd(UART_NUM_0, stop_fan, sizeof(stop_fan), 50, 100);
			}
		}
    }
}

static void gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
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

	gpio_config_t io_conf;
	io_conf.intr_type = GPIO_INTR_ANYEDGE;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 1;
	gpio_config(&io_conf);

	xTaskCreate(control_task, "control_task", 4096, NULL, 5, NULL);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_INPUT_IO_5, gpio_isr_handler, (void *) GPIO_INPUT_IO_5);
}
