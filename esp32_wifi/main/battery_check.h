#ifndef __BATTERY_CHECK_H__
#define __BATTERY_CHECK_H__

uint32_t battery_voltage();
void battery_check_task(void *pvParameters);

#endif
