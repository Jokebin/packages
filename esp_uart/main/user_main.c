/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
   */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/hw_timer.h"

#include "esp_log.h"
#include "esp_system.h"

#define GPIO_TXD		GPIO_NUM_0	//use gpio0-->D3 on board as TXD
#define GPIO_TXD_SEL	(1UL<<GPIO_TXD)
#define GPIO_RXD		GPIO_NUM_4	//use gpio4-->D2 on board as RXD
#define GPIO_RXD_SEL	(1UL<<GPIO_RXD)

#define GET_BIT(ch,bit)		(ch>>bit & 0x01)		
#define BAUDRATE		9600
#define TIME_CNT		(unsigned int)(1*1000000/9600)

static int rtx_flag = 0;	// rx or tx flag

void hw_timer_callback(void *arg)
{
	rtx_flag = 1;
}

void send_char(unsigned char ch)
{
	rtx_flag = 0;
	gpio_set_level(GPIO_TXD, 0);
    hw_timer_alarm_us(TIME_CNT, true);
	while(!rtx_flag);

	rtx_flag = 0;
	gpio_set_level(GPIO_TXD, 1);
	//gpio_set_level(GPIO_TXD, GET_BIT(ch, 0));
	printf("send = %d ", GET_BIT(ch, 0));
	while(!rtx_flag);

	rtx_flag = 0;
	gpio_set_level(GPIO_TXD, 1);
	//gpio_set_level(GPIO_TXD, GET_BIT(ch, 1));
	printf("%d ", GET_BIT(ch, 1));
	while(!rtx_flag);

	rtx_flag = 0;
	gpio_set_level(GPIO_TXD, 1);
	//gpio_set_level(GPIO_TXD, GET_BIT(ch, 2));
	printf("%d ", GET_BIT(ch, 2));
	while(!rtx_flag);

	rtx_flag = 0;
	gpio_set_level(GPIO_TXD, 0);
	//gpio_set_level(GPIO_TXD, GET_BIT(ch, 3));
	printf("%d ", GET_BIT(ch, 3));
	while(!rtx_flag);

	rtx_flag = 0;
	gpio_set_level(GPIO_TXD, 1);
	//gpio_set_level(GPIO_TXD, GET_BIT(ch, 4));
	printf("%d ", GET_BIT(ch, 4));
	while(!rtx_flag);

	rtx_flag = 0;
	gpio_set_level(GPIO_TXD, 1);
	//gpio_set_level(GPIO_TXD, GET_BIT(ch, 5));
	printf("%d ", GET_BIT(ch, 5));
	while(!rtx_flag);

	rtx_flag = 0;
	gpio_set_level(GPIO_TXD, 0);
	//gpio_set_level(GPIO_TXD, GET_BIT(ch, 6));
	printf("%d ", GET_BIT(ch, 6));
	while(!rtx_flag);

	rtx_flag = 0;
	gpio_set_level(GPIO_TXD, 0);
	//gpio_set_level(GPIO_TXD, GET_BIT(ch, 7));
	printf("%d\n", GET_BIT(ch, 7));
	while(!rtx_flag);

	rtx_flag = 0;
	gpio_set_level(GPIO_TXD, 1);
	while(!rtx_flag);

	hw_timer_disarm();
}

unsigned char recv_char()
{
	return 0;
}

static void virtual_uart_task(void *pvParameters)
{
	unsigned char ch = '7';
	while(1) {
		send_char(ch);
		vTaskDelay(1000/portTICK_RATE_MS);
	}
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
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = GPIO_TXD_SEL;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
	gpio_config(&io_conf);
	gpio_set_level(GPIO_TXD, 1);

    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = GPIO_RXD_SEL;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
	gpio_config(&io_conf);
	gpio_set_level(GPIO_RXD, 1);

    hw_timer_init(hw_timer_callback, NULL);

	xTaskCreate(virtual_uart_task, "virtual_task", 4096, NULL, 5, NULL);
}
