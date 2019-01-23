#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

#include "modbus_tcp.h"
#include "battery_check.h"
#include "main.h"

#define DEFAULT_VREF	1089 
#define NO_OF_SAMPLES   64

static esp_adc_cal_characteristics_t *adc_chars;
static uint32_t voltage = 4000;
static const adc_channel_t channel = ADC_CHANNEL_6;	//GPIO34 if ADC1
static const adc_atten_t atten = ADC_ATTEN_DB_0;
static const adc_unit_t unit = ADC_UNIT_1;

void battery_check_task(void *pvParameters)
{
	// use adc1 to check battery voltage
	adc1_config_width(ADC_WIDTH_BIT_12);
	adc1_config_channel_atten(channel, atten);

    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));

    esp_adc_cal_characterize(unit, ADC_ATTEN_DB_0, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);

	uint8_t sbuf[2];
	struct md_tcp_ctx ctx;
	memset(&ctx, 0, sizeof(ctx));

	while(1) {
		if(md_tcp_init(&ctx, system_config.serip, system_config.serport)) {
			vTaskDelay(100/portTICK_RATE_MS);
			continue;
		}
		while(1) {
	        uint32_t adc_reading = 0;
	        //Multisampling
	        for (int i = 0; i < NO_OF_SAMPLES; i++) {
				adc_reading += adc1_get_raw((adc1_channel_t)channel);
	        }
	
	        adc_reading /= NO_OF_SAMPLES;
	        voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);

	        //printf("Raw: %d\tVoltage: %dmV\t", adc_reading, voltage);
			voltage = voltage * (10 + 3.3)/3.3;
			printf("Battery Voltage: %dmV\n", voltage);
	
			sbuf[0] = voltage >> 8;
			sbuf[1] = voltage & 0xFF;

			if(ctx.send(&ctx, MODBUS_SERVER_ID, MODBUS_WRITE_SINGLE_REGISTER, system_config.battery, sizeof(sbuf), sbuf) <= 0) {
				break;
			}
	        vTaskDelay(pdMS_TO_TICKS(1000));
		}

		ctx.destroy(&ctx);
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

	vTaskDelete(NULL);
}
