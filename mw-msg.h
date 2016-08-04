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
#include <lwip/ip_addr.h>

/// Maximum SSID length (including '\0').
#define MW_SSID_MAXLEN		32
/// Maximum password length (including '\0').
#define MW_PASS_MAXLEN		64

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

#define MW_CMD_MAX_BUFLEN	(MW_MSG_MAX_BUFLEN - 4)

/// TCP/UDP address message
typedef struct {
	char dst_port[6];
	char src_port[6];
	uint8_t channel;
	char data[MW_CMD_MAX_BUFLEN - 6 - 6 - 1];
} MwMsgInAddr;

/// AP configuration message
typedef struct {
	uint8_t cfgNum;
	char ssid[MW_SSID_MAXLEN];
	char pass[MW_PASS_MAXLEN];
} MwMsgApCfg;

/// IP configuration message
typedef struct {
	uint8_t cfgNum;
	uint8_t reserved[3];
	struct ip_info cfg;
	struct ip_addr dns1;
	struct ip_addr dns2;
} MwMsgIpCfg;

/// Date and time message
typedef struct {
	uint32_t dtBin[2];
	char dtStr[MW_CMD_MAX_BUFLEN - sizeof(uint64_t)];
} MwMsgDateTime;

typedef struct {
	uint32_t addr;
	uint8_t data[MW_CMD_MAX_BUFLEN - sizeof(uint32_t)];
} MwMsgFlashData;

typedef struct {
	uint32_t addr;
	uint16_t len;
} MwMsgFlashRange;

/** \addtogroup MwApi MwCmd Command sent to system FSM
 *  \{ */
typedef struct {
	uint16_t cmd;		///< Command code
	uint16_t datalen;	///< Data length
	// If datalen is nonzero, additional command data goes here until
	// filling datalen bytes.
	union {
		uint8_t ch;		// Channel number for channel related requests
		uint8_t data[MW_CMD_MAX_BUFLEN];
		MwMsgInAddr inAddr;
		MwMsgApCfg apCfg;
		MwMsgIpCfg ipCfg;
		MwMsgDateTime datetime;
		MwMsgFlashData flData;
		MwMsgFlashRange flRange;
		uint16_t flSect;	// Flash sector
		uint32_t flId;		// Flash IDs
		uint16_t rndLen;	// Length of the random buffer to fill
	};
} MwCmd;
/** \} */

typedef struct {
	union {
		uint8_t data[MW_MSG_MAX_BUFLEN];	///< Buffer raw data
		MwCmd cmd;							///< Command
	};
	uint16_t len;							///< Length of buffer contents
	uint8_t ch;								///< Channel associated with buffer
} MwMsgBuf;

/** \addtogroup MwApi MwFsmMsg Message parsed by the FSM
 *  \{ */
typedef struct {
	MwEvent e;			///< Message event.
	void *d;			///< Pointer to data related to event.
} MwFsmMsg;
/** \} */

#endif /*_MW_MSG_H_*/

/** \} */

