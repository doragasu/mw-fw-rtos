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
#define MW_FW_VERSION_MINOR	3
/// Firmware variant, "std" for standard version
#define MW_FW_VARIANT	"std"

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

/// Base flash address for the user to read/write data
#define MW_FLASH_USER_BASE_ADDR		0x80000
/// Base flash sector for the user to read/write data
#define MW_FLASH_USER_BASE_SECT		(MW_FLASH_USER_BASE_ADDR>>12)

/** \addtogroup MwApi Cmds Supported commands.
 *  \{ */
#define MW_CMD_OK				  0		///< OK command reply
#define MW_CMD_VERSION      	  1		///< Get firmware version
#define MW_CMD_ECHO				  2		///< Echo data
#define MW_CMD_AP_SCAN			  3		///< Scan for access points
#define MW_CMD_AP_CFG			  4		///< Configure access point
#define MW_CMD_AP_CFG_GET   	  5		///< Get access point configuration
#define MW_CMD_IP_CURRENT	      6		///< Get current IPv4 configuration
// Reserved slot
#define MW_CMD_IP_CFG			  8		///< Configure IPv4
#define MW_CMD_IP_CFG_GET		  9		///< Get IPv4 configuration
#define MW_CMD_DEF_AP_CFG		 10		///< Set default AP configuration
#define MW_CMD_DEF_AP_CFG_GET	 11		///< Get default AP configuration
#define MW_CMD_AP_JOIN			 12		///< Join access point
#define MW_CMD_AP_LEAVE			 13		///< Leave previously joined AP
#define MW_CMD_TCP_CON			 14		///< Connect TCP socket
#define MW_CMD_TCP_BIND			 15		///< Bind TCP socket to port
#define MW_CMD_TCP_ACCEPT		 16		///< Accept incomint TCP connection
#define MW_CMD_TCP_DISC			 17		///< Disconnect and free TCP socket
#define MW_CMD_UDP_SET			 18		///< Configure UDP socket
#define MW_CMD_UDP_CLR			 19		///< Clear and free UDP socket
#define MW_CMD_SOCK_STAT		 20		///< Get socket status
#define MW_CMD_PING				 21		///< Ping host
#define MW_CMD_SNTP_CFG			 22		///< Configure SNTP service
#define MW_CMD_SNTP_CFG_GET      23     ///< Get SNTP configuration
#define MW_CMD_DATETIME			 24		///< Get date and time
#define MW_CMD_DT_SET       	 25		///< Set date and time
#define MW_CMD_FLASH_WRITE		 26		///< Write to WiFi module flash
#define MW_CMD_FLASH_READ		 27		///< Read from WiFi module flash
#define MW_CMD_FLASH_ERASE		 28		///< Erase sector from WiFi flash
#define MW_CMD_FLASH_ID 		 29		///< Get WiFi flash chip identifiers
#define MW_CMD_SYS_STAT			 30		///< Get system status
#define MW_CMD_DEF_CFG_SET		 31		///< Set default configuration
#define MW_CMD_HRNG_GET			 32		///< Gets random numbers
#define MW_CMD_ERROR			255		///< Error command reply
/** \} */

/** \addtogroup MwApi ApCfg Configuration needed to connect to an AP
 *  \{ */
typedef struct {
	char ssid[MW_SSID_MAXLEN];
	char pass[MW_PASS_MAXLEN];
} ApCfg;
/** \} */

/// Length of a command header (command and datalen fields)
#define MW_CMD_HEADLEN	(2 * sizeof(uint16_t))

/// Pointer to the command data
#define MW_CMD_DATA(pCmd)		(((uint8_t*)pCmd)+sizeof(MwCmd))


void MwInit(void);

/** \} */


