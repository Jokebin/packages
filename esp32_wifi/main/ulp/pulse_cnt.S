/* ULP Example: pulse counting

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.

   This file contains assembly code which runs on the ULP.

   ULP wakes up to run this code at a certain period, determined by the values
   in SENS_ULP_CP_SLEEP_CYCx_REG registers. On each wake up, the program checks
   the input on GPIO0. If the value is different from the previous one, the
   program "debounces" the input: on the next debounce_max_count wake ups,
   it expects to see the same value of input.
   If this condition holds true, the program increments edge_count and starts
   waiting for input signal polarity to change again.
   When the edge counter reaches certain value (set by the main program),
   this program running triggers a wake up from deep sleep.
*/

/* ULP assembly files are passed through C preprocessor first, so include directives
   and C macros may be used in these files 
 */
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"

	/* Define variables, which go into .bss section (zero-initialized data) */
	.bss

	/* high level time of pluse */
	.global high_cycles
high_cycles:
	.long 0

	/* low level time of pluse */
	.global low_cycles
low_cycles:
	.long 0

	.global sensor_status
sensor_status:
	.long 0

	/* Code goes into .text section */
	.text
	.global entry
entry:

	/*
	    sensor		| pluse		 | data
		sensor1:	| RTC_GPIO9	 | RTC_GPIO5
	 	sensor2: 	| RTC_GPIO16 | RTC_GPIO15
		sensor3:	| RTC_GPIO6	 | RTC_GPIO7
	 */

	move r3, sensor_status
	move r0, 0x0
	st r0, r3, 0

	WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + 13, 1, 1)
	WAIT 40000

	WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + 16, 1, 1)
	WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + 6, 1, 1)
	WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + 9, 1, 1)
	/*WAIT low_cycles*/
	WAIT 800

	READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S + 7, 1);
	lsh r1, r0, 1

	READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S + 15, 1);
	or r1, r1, r0

	lsh r1, r1, 1
	READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S + 5, 1);
	or r1, r1, r0

	WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_W1TC_S + 16, 1, 1)
	WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_W1TC_S + 6, 1, 1)
	WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_W1TC_S + 9, 1, 1)

	WAIT 40000
	WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_W1TC_S + 13, 1, 1)

	and r0, r1, 0x07
	jumpr read_done, 1, ge
	halt

	.global read_done
read_done:
	/* store sensor status */
	move r3, sensor_status
	st r1, r3, 0

	.global wake_up
wake_up:
	/* Check if the system can be woken up */
	READ_RTC_FIELD(RTC_CNTL_LOW_POWER_ST_REG, RTC_CNTL_RDY_FOR_WAKEUP)
	and r0, r0, 1
	jump wake_up, eq

	/* Wake up the SoC, end program */
	wake
	WRITE_RTC_FIELD(RTC_CNTL_STATE0_REG, RTC_CNTL_ULP_CP_SLP_TIMER_EN, 0)
	halt
