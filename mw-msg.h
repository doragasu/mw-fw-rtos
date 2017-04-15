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

#define MW_FACT_RESET_MAGIC	0xFEAA5501	

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
#define MW_MSG_MAX_BUFLEN	1440
//#define MW_MSG_MAX_BUFLEN	1376

#define MW_CMD_MAX_BUFLEN	(MW_MSG_MAX_BUFLEN - 4)

/** \addtogroup MwApi MwState Possible states of the system state machine.
 *  \{ */
typedef enum {
	MW_ST_INIT = 0,		///< Initialization state.
	MW_ST_IDLE,			///< Idle state, until connected to an AP.
	MW_ST_AP_JOIN,		///< Trying to join an access point.
	MW_ST_SCAN,			///< Scanning access points.
	MW_ST_READY,		///< Connected to The Internet.
	MW_ST_TRANSPARENT,	///< Transparent communication state.
	MW_ST_MAX			///< Limit number for state machine.
} MwState;
/** \} */

/// TCP/UDP address message
typedef struct {
	char dst_port[6];
	char src_port[6];
	uint8_t channel;
	char data[MW_CMD_MAX_BUFLEN - 6 - 6 - 1];
} MwMsgInAddr;

//typedef struct {
//	uint32_t reserved;
//	uint16_t port;
//	uint8_t ch;
//} MwMsgBind;

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

/// SNTP and timezone configuration
typedef struct {
	uint16_t upDelay;
	int8_t tz;
	uint8_t dst;
	char servers[MW_CMD_MAX_BUFLEN - 4];
} MwMsgSntpCfg;

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

typedef struct {
	uint32_t reserved;
	uint16_t port;
	uint8_t  channel;
} MwMsgBind;

/** \addtogroup MwApi MwSockStat Socket status.
 *  \{ */
typedef enum {
	MW_SOCK_NONE = 0,	///< Unused socket.
	MW_SOCK_TCP_LISTEN,	///< Socket bound and listening.
	MW_SOCK_TCP_EST,	///< TCP socket, connection established.
	MW_SOCK_UDP_READY	///< UDP socket ready for sending/receiving
} MwSockStat;
/** \} */

/** \addtogroup MwApi MwMsgSysStat System status
 *  \{ */
typedef union {
	uint32_t st_flags;
	struct {
		MwState sys_stat:8;		///< System status
		uint8_t online:1;		///< Module is connected to the Internet
		uint8_t cfg_ok:1;		///< Configuration OK
		uint8_t dt_ok:1;		///< Date and time synchronized at least once
		uint16_t reserved:5;	///< Reserved flags
		uint16_t ch_ev:16;		///< Channel flags with the pending event
	};
} MwMsgSysStat;
/** \} */

///** \addtogroup MwApi MwSysStat System status
// *  \{ */
//typedef struct {
//	MwState sys_stat:8;		///< System status
//	uint8_t pending:1;		///< Another event is pending
//	uint8_t online:1;		///< Module is connected to the Internet
//	uint8_t dt_ok:1;		///< Date and time synchronized at least once
//	uint8_t cfg_ok:1;		///< Configuration OK
//	uint8_t ch_ev:1;		///< Channel event available
//	uint8_t ch:4;			///< Channel with the pending event
//} MwSysStat;

/** \} *//** \addtogroup MwApi MwCmd Command sent to system FSM
 *  \{ */
typedef struct {
	uint16_t cmd;		///< Command code
	uint16_t datalen;	///< Data length
	// If datalen is nonzero, additional command data goes here until
	// filling datalen bytes.
	union {
		uint8_t ch;		// Channel number for channel related requests
		uint8_t data[MW_CMD_MAX_BUFLEN];
		uint32_t dwData[MW_CMD_MAX_BUFLEN / sizeof(uint32_t)];
		MwMsgInAddr inAddr;
		MwMsgApCfg apCfg;
		MwMsgIpCfg ipCfg;
		MwMsgSntpCfg sntpCfg;
		MwMsgDateTime datetime;
		MwMsgFlashData flData;
		MwMsgFlashRange flRange;
		MwMsgBind bind;
		MwMsgSysStat sysStat;
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

