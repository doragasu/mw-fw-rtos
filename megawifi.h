/************************************************************************//**
 * \brief MeGaWiFi application programming interface.
 *
 * \Author Jesus Alonso (doragasu)
 * \date   2015
 * \defgroup MwApi MeGaWiFi application programming interface.
 * \{
 ****************************************************************************/

#include <stdint.h>
#include "mw-msg.h"

/// Major firmware version
#define MW_FW_VERSION_MAJOR	0
/// Minor firmware version
#define MW_FW_VERSION_MINOR	1
/// Firmware variant, "std" for standard version
#define MW_FW_VARIANT	"std"

/// Maximum SSID length (including '\0').
#define MW_SSID_MAXLEN		32
/// Maximum password length (including '\0').
#define MW_PASS_MAXLEN		64
/// Maximum length of an NTP pool URI (including '\0').
#define MW_NTP_POOL_MAXLEN	80
/// Number of AP configurations stored to nvflash.
#define MW_NUM_AP_CFGS		3
/// Number of DSN servers supported per AP configuration.
#define MW_NUM_DNS_SERVERS	2
/// Length of the FSM queue
#define MW_FSM_QUEUE_LEN	8
/// Maximum number of simultaneous TCP connections
#define MW_MAX_SOCK			3

/// Length of the flash chip (4 megabytes for the ESP-12 modules.
#define FLASH_LENGTH		(4*1024*1024)
/// Start of the system configuration area (the last three 4 KiB sectors of
/// the flash, 0x3FD for a 4 MiB flash chip).
#define MW_CFG_FLASH_SEQ	((FLASH_LENGTH>>12)-3)

/// Default NTP server 0
#define MW_SNTP_SERV_0		"0.pool.ntp.org"
/// Default NTP server 1
#define MW_SNTP_SERV_1		"1.pool.ntp.org"
/// Default NTP server 2
#define MW_SNTP_SERV_2		"2.pool.ntp.org"

/// Stack size (in elements) for FSM task
#define MW_FSM_STACK_LEN	1024

/// Stack size (in elements) for SOCK task
#define MW_SOCK_STACK_LEN	1024

/// Stack size (in elements) for WPOLL task
#define MW_WPOLL_STACK_LEN	1024

/// Control channel used for command interpreter
#define MW_CTRL_CH			0

/// Priority for the FSM task
#define MW_FSM_PRIO			1

/// Priority for the SOCK task
#define MW_SOCK_PRIO		2

/// Priority for the WPOLL task
#define MW_WPOLL_PRIO		1

/** \addtogroup MwApi RetCodes Return values for functions of this module.
 *  \{ */
/// Operation completed successfully
#define MW_OK				0
/// Generic error code
#define MW_ERROR			-1
/// Command format error
#define MW_CMD_FMT_ERROR	-2
/// Unknown command code
#define MW_CMD_UNKNOWN		-3
/** \} */

/** \addtogroup MwApi Cmds Supported commands.
 *  \{ */
#define MW_CMD_OK			  0
#define MW_CMD_VERSION        1
#define MW_CMD_ECHO			  2
#define MW_CMD_AP_SCAN		  3
#define MW_CMD_AP_CFG		  4
#define MW_CMD_AP_CFG_GET     5
#define MW_CMD_IP_CFG		  6
#define MW_CMD_IP_CFG_GET	  7
#define MW_CMD_AP_JOIN		  8
#define MW_CMD_AP_LEAVE		  9
#define MW_CMD_TCP_CON		 10
#define MW_CMD_TCP_BIND		 11
#define MW_CMD_TCP_ACCEPT	 12
#define MW_CMD_TCP_STAT		 13
#define MW_CMD_TCP_DISC		 14
#define MW_CMD_UDP_SET		 15
#define MW_CMD_UDP_STAT		 16
#define MW_CMD_UDP_CLR		 17
#define MW_CMD_PING			 18
#define MW_CMD_SNTP_CFG		 19
#define MW_CMD_DATETIME		 20
#define MW_CMD_DT_SET        21
#define MW_CMD_FLASH_WRITE	 22
#define MW_CMD_FLASH_READ	 23
#define MW_CMD_ERROR		255

/** \} */

/** \addtogroup MwApi ApCfg Configuration needed to connect to an AP
 *  \{ */
typedef struct {
	char ssid[MW_SSID_MAXLEN];
	char pass[MW_PASS_MAXLEN];
} ApCfg;

/** \} */

/** \addtogroup MwApi MwCmd Command sent to system FSM
 *  \{ */
typedef struct {
	uint16_t cmd;		///< Command code
	uint16_t datalen;	///< Data length
	// If datalen is nonzero, additional command data goes here until
	// filling datalen bytes.
	union {
		uint8_t data[MW_MSG_MAX_BUFLEN];// Might need adjusting data length!
		MwMsgInAddr inAddr;
		uint8_t ch;		// Channel number for channel related requests
	};
} MwCmd;
/** \} */

/// Length of a command header (command and datalen fields)
#define MW_CMD_HEADLEN	(2 * sizeof(uint16_t))

/// Pointer to the command data
#define MW_CMD_DATA(pCmd)		(((uint8_t*)pCmd)+sizeof(MwCmd))


void MwInit(void);

/** \} */


