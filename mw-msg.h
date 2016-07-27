/************************************************************************//**
 * \brief Message definitions for passing to megawifi FSM.
 *
 * \Author Jesus Alonso (doragasu)
 * \date   2016
 * \defgroup MwMsg Message definitions.
 * \{
 ****************************************************************************/

#ifndef _MW_MSG_H_
#define _MW_MSG_H_

#include <stdint.h>

/** \addtogroup MwApi MwEvent Events parsed by the system FSM.
 *  \{ */
/* TODO ADD UART EVENTS? MAYBE REMOVE SOCKET EVENTS? */
typedef enum {
	MW_EV_NONE = 0,		///< No event.
	MW_EV_INIT_DONE,	///< Initialization complete.
	MW_EV_WIFI,			///< WiFi events, excluding scan related ones.
	MW_EV_SCAN,			///< WiFi scan complete.
	MW_EV_SNTP,			///< SNTP configured.
	MW_EV_SER_RX,		///< Data reception from serial line.
	MW_EV_SER_TX,		///< Data transmission through serial line complete.
	MW_EV_TCP_CON,		///< TCP connection established.
	MW_EV_TCP_RECV,		///< Data received from TCP connection.
	MW_EV_TCP_SENT,		///< Data sent to peer on TCP connection.
	MW_EV_UDP_RECV,		///< Data received from UDP connection.
	MW_EV_CON_DISC,		///< TCP disconnection.
	MW_EV_CON_ERR,		///< TCP connection error.
	MW_EV_MAX			///< Number of total events.
} MwEvent;
/** \} */

/// Maximum buffer length (bytes)
#define MW_MSG_MAX_BUFLEN	512

typedef struct {
	uint8_t data[MW_MSG_MAX_BUFLEN];	///< Buffer data
	uint16_t len;						///< Length of buffer contents
	uint8_t ch;							///< Channel associated with buffer
} MwMsgBuf;

typedef struct {
	char dst_port[6];
	char src_port[6];
	uint8_t channel;
	char data[MW_MSG_MAX_BUFLEN];
} MwMsgInAddr;

/** \addtogroup MwApi MwFsmMsg Message parsed by the FSM
 *  \{ */
typedef struct {
	MwEvent e;			///< Message event.
	void *d;			///< Pointer to data related to event.
} MwFsmMsg;
/** \} */

#endif /*_MW_MSG_H_*/

/** \} */

