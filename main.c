#include "led.h"
#include "stdout_redirect.h"
#include "esp/uart.h"
#include "megawifi.h"
#include <stdio.h>
// FreeRTOS
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>

#include <lwip/sockets.h>

#ifndef FILE_DESCRIPTOR_OFFSET
#define FILE_DESCRIPTOR_OFFSET (LWIP_SOCKET_OFFSET + MEMP_NUM_NETCONN)
#if FILE_DESCRIPTOR_OFFSET > FD_SETSIZE
#error Too many lwip sockets for the FD_SETSIZE.
#endif
#endif

#define UART_COMM 	0
#define UART_DEBUG	1

ssize_t UartWriteFunction(struct _reent *r, int fd, const void *ptr, size_t len ) {
	(void)r;
	(void)fd;
	char *toWrite = (char*)ptr;
    for(size_t i = 0; i < len; i++) {
        if(toWrite[i] == '\r')
            continue;
        if(toWrite[i] == '\n')
            uart_putc(UART_DEBUG, '\r');
        uart_putc(UART_DEBUG, toWrite[i]);
    }
    return len;
}

// Supply our own _write_r syscall to handle writes to UART_COMM and UART_DEBUG
// Not the most standard implementation though. Maybe we should compare fd
// with r->_stdout->_file for UART_DEBUG writes, and reserve fd 3 for UART_COMM
// writes.
ssize_t _write_r(struct _reent *r, int fd, const void *ptr, size_t len)
{
	size_t i;
	if (UART_DEBUG == fd) {
		return UartWriteFunction(r, fd, ptr, len);
	}
	if (UART_COMM == fd) {
		for (i = 0; i < len; i++) uart_putc(fd, ((char*)ptr)[i]);
		return i;
	}
    if (fd >= LWIP_SOCKET_OFFSET) {
        return lwip_write(fd, ptr, len);
    }
    r->_errno = EBADF;
    return -1;
}

void user_init(void) {
	// Set GPIO2 pin as UART2 output
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_U1TXD_BK);
	// Set callback for stdout to be redirected to the UART used for debug
	set_write_stdout(&UartWriteFunction);
	// Initialize UART used for debugging
	uart_set_baud(UART_DEBUG, 115200);
	printf("\n=== MeGaWiFi firmware version %d.%d-%s ===\n",
			MW_FW_VERSION_MAJOR, MW_FW_VERSION_MINOR, MW_FW_VARIANT);
	printf("            doragasu, 2016\n\n");
	// Power the LED on
	LedInit();
	LedOn();
	// Initialize MeGaWiFi system and FSM
	MwInit();
	printf("Init done!\n");
}
