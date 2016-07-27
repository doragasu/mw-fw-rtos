/************************************************************************//**
 * \brief  WiFi command parser. Parses commands directed to the WiFi module
 *         and executes actions related to the commands and the module
 *         state.
 *
 * \author Jesus Alonso (doragasu)
 * \date   2015
 * \defgroup wifi-cmd WiFi commands implementation.
 * \{
 ****************************************************************************/

#ifndef _WIFI_CMD_H_
#define _WIFI_CMD_H_

#include <stdint.h>

/** \addtogroup wifi-cmd WiCmd Supported WiFi module commands.
 *  \{ */
typedef enum {
	WICMD_CMD_NONE = 0,			///< Do nothing.
	WICMD_CMD_ECHO,				///< Echo payload.
	WICMD_CMD_SCAN,				///< Scan APs.
	WICMD_CMD_AP_SET,			///< Configure AP.
	WICMD_CMD_AP_JOIN,			///< Join AP.
	WICMD_CMD_AP_LEAVE,			///< Leave joined AP.
	WICMD_CMD_IP_CONF,			///< IP and DNS settings.
	WICMD_CMD_TCP_CON,			///< TCP socket connect.
	WICMD_CMD_TCP_CLOSE,		///< TCP socket close.
	WICMD_CMD_TCP_LISTEN,		///< TCP listen on port.
	WICMD_CMD_TCP_ACCEPT,		///< TCP accept incoming connection.
	WICMD_CMD_TCP_SEND,			///< TCP send data through socket.
//	WICMD_CMD_TCP_RECV,
	WICMD_CMD_TCP_TRANSP,		///< TCP transparent mode enable.
	WICMD_CMD_TCP_FREE,			///< TCP free closed socket.
	WICMD_CMD_UDP_ALLOC,		///< UDP socket allocate.
	WICMD_CMD_UDP_SEND,			///< UDP send datagram.
//	WICMD_CMD_UDP_RECV,
	WICMD_CMD_UDP_FREE,			///< UDP free socket.
	WICMD_CMD_UDP_TRANSP,		///< UDP transparent mode.
	WICMD_CMD_FLASH_READ,		///< Read module flash.
	WICMD_CMD_FLASH_WRITE,		///< Write to module flash.
	WICMD_CMD_SNTP_CONF,		///< Configure SNTP server pool.
	WICMD_CMD_SNTP_DATETIME,	///< Get date and time.
	WICMD_CMD_MAX
} WiCmd;
/** \} */

void WiCmdInit(void);

int WiCmdParse(uint8_t frame[], uint16_t length);

int WiCmdSendReply(uint8_t data[], uint16_t length);
#endif /*_WIFI_CMD_H_*/

/** \} */
