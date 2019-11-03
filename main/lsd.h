/************************************************************************//**
 * \brief  Local Symmetric Data-link. Implements an extremely simple
 *         protocol to link two full-duplex devices, multiplexing the
 *         data link.
 *
 * \author Jesus Alonso (doragasu)
 * \date   2016
 * \todo   Implement UART RTS/CTS handshaking.
 * \todo   Proper implementation of error handling.
 * \defgroup lsd Local Symmetric Data-link
 * \{
 ****************************************************************************/

/**
 * USAGE:
 * First initialize the module calling LsdInit().
 * Then enable at least one channel calling LsdEnable().
 *
 * To send data call LsdSend();
 *
 * Data is automatically received and forwarded to the FSM using the queue
 * set during initialization.
 *
 * Frame format is:
 *
 * STX : CH-LENH : LENL : DATA : ETX
 *
 * - STX and ETX are the start/end of transmission characters (1 byte each).
 * - CH-LENH is the channel number (first 4 bits) and the 4 high bits of the
 *   data length.
 * - LENL is the low 8 bits of the data length.
 * - DATA is the payload, of the previously specified length.
 */

#ifndef _LSD_H_
#define _LSD_H_

#include <stdint.h>

#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>

/// LSD UART baud rate
//#define LSD_UART_BR		(475625LU/2)
//#define LSD_UART_BR		(477500/2)
#define LSD_UART_BR		1500000
//#define LSD_UART_BR		500000
//#define LSD_UART_BR		750000
//#define LSD_UART_BR		115200

/** \addtogroup lsd ReturnCodes OK/Error codes returned by several functions.
 *  \{ */
/// Function completed successfully
#define LSD_OK				0
/// Generic error code
#define LSD_ERROR			-1
/// A framing error occurred. Possible data loss.
#define LSD_FRAMING_ERROR	-2
/** \} */

/// LSD frame overhead in bytes
#define LSD_OVERHEAD		4

/// Uart used for LSD
#define LSD_UART			0

/// Start/end of transmission character
#define LSD_STX_ETX		0x7E

/// Maximum number of available simultaneous channels
#define LSD_MAX_CH			4

/// Receive task priority
#define LSD_RECV_PRIO		2

/// Maximum data payload length
/// TODO IMPORTANT: This should be dynamically configurable!
//#define LSD_MAX_LEN		 4095
#define LSD_MAX_LEN		 CONFIG_TCP_MSS

/************************************************************************//**
 * Module initialization. Call this function before any other one in this
 * module.
 ****************************************************************************/
void LsdInit(QueueHandle_t q);

/************************************************************************//**
 * Enables a channel to start reception and be able to send data.
 *
 * \param[in] ch Channel number.
 *
 * \return A pointer to an empty TX buffer, or NULL if no buffer is
 *         available.
 ****************************************************************************/
int LsdChEnable(uint8_t ch);

/************************************************************************//**
 * Disables a channel to stop reception and prohibit sending data.
 *
 * \param[in] ch Channel number.
 *
 * \return A pointer to an empty TX buffer, or NULL if no buffer is
 *         available.
 ****************************************************************************/
int LsdChDisable(uint8_t ch);


/************************************************************************//**
 * Sends data through a previously enabled channel.
 *
 * \param[in] data Buffer to send.
 * \param[in] len  Length of the buffer to send.
 * \param[in] ch   Channel number to use.
 *
 * \return -1 if there was an error, or the number of characterse sent
 * 		   otherwise.
 ****************************************************************************/
int LsdSend(uint8_t *data, uint16_t len, uint8_t ch);

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
		              uint16_t total, uint8_t ch);

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
int LsdSplitNext(uint8_t *data, uint16_t len);

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
int LsdSplitEnd(uint8_t *data, uint16_t len);

/************************************************************************//**
 * Frees the oldest receive buffer. This function must be called each time
 * a buffer is processed to allow receiving a new frame.
 *
 * \warning Calling this function more times than buffers have been received,
 * will likely cause overruns!
 ****************************************************************************/
void LsdRxBufFree(void);

#endif /*_LSD_H_*/
/** \} */

