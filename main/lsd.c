/************************************************************************//**
 * \brief  Local Symmetric Data-link. Implements an extremely simple
 *         protocol to link two full-duplex devices, multiplexing the
 *         data link.
 *
 * \author Jesus Alonso (doragasu)
 * \date   2016
 * \todo   Implement UART RTS/CTS handshaking.
 * \todo   Currently LsdSend() blocks polling the UART Fifo. An implemen-
 *         tation using interrupts/DMA should be in the high priority list.
 * \todo   Proper implementation of error handling.
 ****************************************************************************/
#include <string.h>
#include <unistd.h>

#include "lsd.h"
#include "mw-msg.h"
#include "util.h"

#include <driver/uart.h>
#include <semphr.h>

/// Number of buffer frames available
#define LSD_BUF_FRAMES			2

/// Start of data in the buffer (skips STX and LEN fields).
#define LSD_BUF_DATA_START 		3

/// Threshold of the RX FIFO to generate RTS signal
#define LSD_RX_RTS_THR	(128 - 16)

/** \addtogroup lsd LsdState Allowed states for reception state machine.
 *  \{ */
typedef enum {
	LSD_ST_IDLE = 0,		///< Currently inactive
	LSD_ST_STX_WAIT,		///< Waiting for STX
	LSD_ST_CH_LENH_RECV,		///< Receiving channel and length (high bits)
	LSD_ST_LEN_RECV,		///< Receiving frame length
	LSD_ST_DATA_RECV,		///< Receiving data length
	LSD_ST_ETX_RECV,		///< Receiving ETX
	LSD_ST_MAX			///< Number of states
} LsdState;
/** \} */

/** \addtogroup lsd LsdData Local data required by the module.
 *  \{ */
typedef struct {
	MwMsgBuf rx[LSD_BUF_FRAMES];	///< Reception buffers.
	SemaphoreHandle_t sem;		///< Semaphore to control buffers
	LsdState rxs;			///< Reception state
	uint8_t en[LSD_MAX_CH];		///< Channel enable
	uint16_t pos;			///< Position in current buffer
	uint8_t current;		///< Current buffer in use
} LsdData;
/** \} */

/*
 * Private prototypes
 */
void LsdRecvTsk(void *pvParameters);

/// Module data
static LsdData d;

/************************************************************************//**
 * Module initialization. Call this function before any other one in this
 * module.
 ****************************************************************************/
void LsdInit(QueueHandle_t q) {
	uart_config_t lsd_uart = {
		.baud_rate = LSD_UART_BR,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS,
		.rx_flow_ctrl_thresh = 122
	};
	// Set variables to default values
	memset(&d, 0, sizeof(LsdData));
	d.rxs = LSD_ST_STX_WAIT;
	// Create semaphore used to handle receive buffers
	d.sem = xSemaphoreCreateCounting(LSD_BUF_FRAMES, LSD_BUF_FRAMES);
	// Create receive task
	xTaskCreate(LsdRecvTsk, "LSDR", 1024, q, LSD_RECV_PRIO, NULL);
	// Configure UART
	ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &lsd_uart));
//	ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, 1, 3, 15, 13));
	ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL));
}

/************************************************************************//**
 * Enables a channel to start reception and be able to send data.
 *
 * \param[in] ch Channel number.
 *
 * \return A pointer to an empty TX buffer, or NULL if no buffer is
 *         available.
 ****************************************************************************/
int LsdChEnable(uint8_t ch) {
	if (ch >= LSD_MAX_CH) return LSD_ERROR;

	d.en[ch] = TRUE;
	return LSD_OK;
}

/************************************************************************//**
 * Disables a channel to stop reception and prohibit sending data.
 *
 * \param[in] ch Channel number.
 *
 * \return A pointer to an empty TX buffer, or NULL if no buffer is
 *         available.
 ****************************************************************************/
int LsdChDisable(uint8_t ch) {
	if (ch >= LSD_MAX_CH) return LSD_ERROR;

	d.en[ch] = FALSE;

	return LSD_OK;
}

/************************************************************************//**
 * Sends data through a previously enabled channel.
 *
 * \param[in] data Buffer to send.
 * \param[in] len  Length of the buffer to send.
 * \param[in] ch   Channel number to use for sending.
 *
 * \return -1 if there was an error, or the number of characterse sent
 * 		   otherwise.
 ****************************************************************************/
int LsdSend(const uint8_t *data, uint16_t len, uint8_t ch) {
	char scratch[3];

	if (len > MW_MSG_MAX_BUFLEN || ch >= LSD_MAX_CH) {
		LOGE("Invalid length (%d) or channel (%d).", len, ch);
		return -1;
	}
	if (!d.en[ch]) {
		LOGE("LsdSend: Channel %d not enabled.", ch);
		return 0;
	}

	LOGD("sending %d bytes", len);
	scratch[0] = LSD_STX_ETX;
	scratch[1] = (ch<<4) | (len>>8);
	scratch[2] = len & 0xFF;
	// Send STX, channel and length
	uart_write_bytes(LSD_UART, scratch, sizeof(scratch));
	// Send data payload
	uart_write_bytes(LSD_UART, (char*)data, len);
	// Send ETX
	uart_write_bytes(LSD_UART, scratch, 1);

	return len;
}

/************************************************************************//**
 * Starts sending data through a previously enabled channel. Once started,
 * you can send more additional data inside of the frame by issuing as
 * many LsdSplitNext() calls as needed, and end the frame by calling
 * LsdSplitEnd().
 *
 * \param[in] data  Buffer to send.
 * \param[in] len   Length of the data buffer to send.
 * \param[in] total Total length of the data to send using a split frame.
 * \param[in] ch    Channel number to use for sending.
 *
 * \return -1 if there was an error, or the number of characterse sent
 * 		   otherwise.
 ****************************************************************************/
int LsdSplitStart(uint8_t *data, uint16_t len,
		              uint16_t total, uint8_t ch) {
	char scratch[3];

	if (total > MW_MSG_MAX_BUFLEN || ch >= LSD_MAX_CH) return -1;
	if (!d.en[ch]) return 0;

	scratch[0] = LSD_STX_ETX;
	scratch[1] = (ch<<4) || (total>>8);
	scratch[2] = total & 0xFF;
	// Send STX, channel and length
	LOGD("sending header");
	uart_write_bytes(LSD_UART, scratch, sizeof(scratch));
	// Send data payload
	LOGD("sending %d bytes", len);
	if (len) {
		uart_write_bytes(LSD_UART, (char*)data, len);
	}
	return len;
}

/************************************************************************//**
 * Appends (sends) additional data to a frame previously started by an
 * LsdSplitStart() call.
 *
 * \param[in] data  Buffer to send.
 * \param[in] len   Length of the data buffer to send.
 *
 * \return -1 if there was an error, or the number of characterse sent
 * 		   otherwise.
 ****************************************************************************/
int LsdSplitNext(uint8_t *data, uint16_t len) {
	// send data
	LOGD("Sending %d bytes", len);
	uart_write_bytes(LSD_UART, (char*)data, len);
	return len;
}

/************************************************************************//**
 * Appends (sends) additional data to a frame previously started by an
 * LsdSplitStart() call, and finally ends the frame.
 *
 * \param[in] data  Buffer to send.
 * \param[in] len   Length of the data buffer to send.
 *
 * \return -1 if there was an error, or the number of characterse sent
 * 		   otherwise.
 ****************************************************************************/
int LsdSplitEnd(uint8_t *data, uint16_t len) {
	char scratch = LSD_STX_ETX;

	// Send data
	LOGD("Sending %d bytes", len);
	uart_write_bytes(LSD_UART, (char*)data, len);
	
	// Send ETX
	LOGD("Sending ETX");
	uart_write_bytes(LSD_UART, &scratch, 1);

	return len;
}

/************************************************************************//**
 * Frees the oldest receive buffer. This function must be called each time
 * a buffer is processed to allow receiving a new frame.
 *
 * \warning Calling this function more times than buffers have been received,
 * will likely cause overruns!
 ****************************************************************************/
void LsdRxBufFree(void) {
	// Just increment the receiver semaphore count. Might cause problems
	// if not properly used!
	xSemaphoreGive(d.sem);
}

// Macro to ease access to current reception buffer
#define RXB 	d.rx[d.current]
// Receive task
void LsdRecvTsk(void *pvParameters) {
	QueueHandle_t q = (QueueHandle_t)pvParameters;
	MwFsmMsg m;
	uint16_t pos = 0;
	bool receiving;
	uint8_t recv;


	while (1) {
		// Grab receive buffer semaphore
		xSemaphoreTake(d.sem, portMAX_DELAY);
		receiving = TRUE;
		while (receiving) {
			// Receive byte by byte
			// TODO: Optimize receive routine (requires modifying the interrupt
			// reception module to allow configuring FIFO triggers and to also
			// use timeout interrupts.
			if (uart_read_bytes(LSD_UART, &recv, 1, portMAX_DELAY)) {
				switch (d.rxs) {
					case LSD_ST_IDLE:			// Do nothing!
						break;
	
					case LSD_ST_STX_WAIT:		// Wait for STX to arrive
						if (LSD_STX_ETX == recv) {
							d.rxs = LSD_ST_CH_LENH_RECV;
						}
						break;
	
					case LSD_ST_CH_LENH_RECV:	// Receive CH and len high
						// Check special case: if we receive STX and pos == 0,
						// then this is the real STX (previous one was ETX from
						// previous frame!).
						if (!(LSD_STX_ETX == recv && 0 == pos)) {
							RXB.ch = recv>>4;
							RXB.len = (recv & 0x0F)<<8;
							// Sanity check (not exceding number of channels)
							if (RXB.ch >= LSD_MAX_CH) {
								d.rxs = LSD_ST_STX_WAIT;
								LOGE("invalid channel %" PRIu8, RXB.ch);
							}
							// Check channel is enabled
							else if (d.en[RXB.ch]) {
								d.rxs = LSD_ST_LEN_RECV;
							}
							else {
								d.rxs = LSD_ST_STX_WAIT;
								LOGE("Recv data on not enabled channel!");
							}
						}
						break;
	
					case LSD_ST_LEN_RECV:		// Receive len low
						RXB.len |= recv;
						// Sanity check (not exceeding maximum buffer length)
						if (RXB.len <= MW_MSG_MAX_BUFLEN) {
							pos = 0;
							d.rxs = LSD_ST_DATA_RECV;
						} else {
							LOGE("Recv length exceeds buffer length!");
							d.rxs = LSD_ST_STX_WAIT;
						}
						break;
	
					case LSD_ST_DATA_RECV:		// Receive payload
						RXB.data[pos++] = recv;
						if (pos >= RXB.len) d.rxs = LSD_ST_ETX_RECV;
						break;
	
					case LSD_ST_ETX_RECV:		// ETX should come here
						if (LSD_STX_ETX == recv) {
							// Send message to FSM and switch buffer
							m.e = MW_EV_SER_RX;
							m.d = d.rx + d.current;
							d.current ^= 1;
							// Set receiving to false, to grab a new buffer
							receiving = FALSE;
							xQueueSend(q, &m, portMAX_DELAY);
						} else {
						LOGE("Expecting ETX but not received!");
						}
						d.rxs = LSD_ST_STX_WAIT;
						break;
	
					default:
						// Code should never reach here!
						break;
				} // switch(d.rxs)
			} // if (uart_read_bytes(...))
		} // while(receiving)
	} // while(1)
}

