#include "led.h"
#include "esp/uart.h"
#include "megawifi.h"
#include <stdio.h>

#ifndef DEBUG_UART
#define DEBUG_UART 1
#endif

void user_init(void) {
	// Initialize UART used for debugging
	uart_set_baud(DEBUG_UART, 115200);
	printf("=== MeGaWiFi firmware version %d.%d-%s ===\n",
			MW_FW_VERSION_MAJOR, MW_FW_VERSION_MINOR, MW_FW_VARIANT);
	printf("            doragasu, 2016\n\n");
	// Power the LED on
	LedInit();
	LedOn();
	// Initialize MeGaWiFi system and FSM
	MwInit();
}
