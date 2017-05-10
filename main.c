#include "led.h"
#include "stdout_redirect.h"
#include "esp/uart.h"
#include "megawifi.h"
#include <stdio.h>

#ifndef DEBUG_UART
#define DEBUG_UART 1
#endif

long UartWriteFunction(struct _reent *r, int fd, const char *ptr, int len ) {
    for(int i = 0; i < len; i++) {
        /* Auto convert CR to CRLF, ignore other LFs (compatible with Espressif SDK behaviour) */
        if(ptr[i] == '\r')
            continue;
        if(ptr[i] == '\n')
            uart_putc(DEBUG_UART, '\r');
        uart_putc(DEBUG_UART, ptr[i]);
    }
    return len;
}

void user_init(void) {
	// Set callback for stdout to be redirected to the UART used for debug
	set_write_stdout(UartWriteFunction);
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
