/************************************************************************//**
 * \brief MeGaWiFi application programming interface.
 *
 * \Author Jesus Alonso (idoragasu)
 * \date   2016
 ****************************************************************************/

// Newlib
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FreeRTOS
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>

// lwIP
#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>
#include <lwip/ip_addr.h>

// mbedtls
#include <mbedtls/md5.h>

// Time keeping
#include <time.h>

// Flash manipulation
//#include <spiflash.h>

#include "megawifi.h"
#include "lsd.h"
#include "util.h"
#include "led.h"

/// Configuration address, stored on the last sector 4 KiB of the first 512 KiB
#define MW_CFG_FLASH_ADDR	((512 - 4) * 1024)

/// Flash sector number where the configuration is stored
#define MW_CFG_FLASH_SECT	(MW_CFG_FLASH_ADDR>>12)

/** \addtogroup MwApi MwFdOps FD set operations (add/remove)
 *  \{ */
typedef enum {
	MW_FD_NONE = 0,		///< Do nothing
	MW_FD_ADD,			///< Add socket to the FD set
	MW_FD_REM			///< Remove socket from the FD set
} MwFdOps;

/// Commands allowed while in IDLE state
const static uint32_t idleCmdMask[2] = {
	(1<<MW_CMD_VERSION)               | (1<<MW_CMD_ECHO)                |
	(1<<MW_CMD_AP_SCAN)               | (1<<MW_CMD_AP_CFG)              |
	(1<<MW_CMD_AP_CFG_GET)            | (1<<MW_CMD_IP_CFG)              |
	(1<<MW_CMD_IP_CFG_GET)            | (1<<MW_CMD_DEF_AP_CFG)          |
	(1<<MW_CMD_DEF_AP_CFG_GET)        | (1<<MW_CMD_AP_JOIN)             |
	(1<<MW_CMD_SNTP_CFG)              | (1<<MW_CMD_SNTP_CFG_GET)        |
	(1<<MW_CMD_DATETIME)              | (1<<MW_CMD_DT_SET)              |
	(1<<MW_CMD_FLASH_WRITE)           | (1<<MW_CMD_FLASH_READ)          |
	(1<<MW_CMD_FLASH_ERASE)           | (1<<MW_CMD_FLASH_ID)            |
	(1<<MW_CMD_SYS_STAT)              | (1<<MW_CMD_DEF_CFG_SET),
	(1<<(MW_CMD_HRNG_GET - 32))       | (1<<(MW_CMD_BSSID_GET - 32))    |
	(1<<(MW_CMD_GAMERTAG_SET - 32))   | (1<<(MW_CMD_GAMERTAG_GET - 32)) |
	(1<<(MW_CMD_LOG - 32))            | (1<<(MW_CMD_FACTORY_RESET - 32))
};

/// Commands allowed while in READY state
const static uint32_t readyCmdMask[2] = {
	(1<<MW_CMD_VERSION)              | (1<<MW_CMD_ECHO)                 |
	(1<<MW_CMD_AP_CFG)               | (1<<MW_CMD_AP_CFG_GET)           |
	(1<<MW_CMD_IP_CURRENT)           | (1<<MW_CMD_IP_CFG)               |
	(1<<MW_CMD_IP_CFG_GET)           | (1<<MW_CMD_DEF_AP_CFG)           |
	(1<<MW_CMD_DEF_AP_CFG_GET)       | (1<<MW_CMD_AP_LEAVE)             |
	(1<<MW_CMD_TCP_CON)              | (1<<MW_CMD_TCP_BIND)             |
	(1<<MW_CMD_CLOSE)                | (1<<MW_CMD_UDP_SET)              |
	(1<<MW_CMD_SOCK_STAT)            | (1<<MW_CMD_PING)                 |
	(1<<MW_CMD_SNTP_CFG)             | (1<<MW_CMD_SNTP_CFG_GET)         |
	(1<<MW_CMD_DATETIME)             | (1<<MW_CMD_DT_SET)               |
	(1<<MW_CMD_FLASH_WRITE)          | (1<<MW_CMD_FLASH_READ)           |
	(1<<MW_CMD_FLASH_ERASE)          | (1<<MW_CMD_FLASH_ID)             |
	(1<<MW_CMD_SYS_STAT)             | (1<<MW_CMD_DEF_CFG_SET),
	(1<<(MW_CMD_HRNG_GET - 32))      | (1<<(MW_CMD_BSSID_GET - 32))     |
	(1<<(MW_CMD_GAMERTAG_SET - 32))  | (1<<(MW_CMD_GAMERTAG_GET - 32))  |
	(1<<(MW_CMD_LOG - 32))
};

/*
 * PRIVATE PROTOTYPES
 */
static void MwFsm(MwFsmMsg *msg);
void MwFsmTsk(void *pvParameters);
void MwFsmSockTsk(void *pvParameters);
void MwWiFiStatPollTsk(void *pvParameters);

/** \addtogroup MwApi MwNvCfg Configuration saved to non-volatile memory.
 *  \{ */
typedef struct {
	/// Access point configuration (SSID, password).
	ApCfg ap[MW_NUM_AP_CFGS];
	/// IPv4 (IP addr, mask, gateway). If IP=0.0.0.0, use DHCP.
	struct ip_info ip[MW_NUM_AP_CFGS];
	/// DNS configuration (when not using DHCP). 2 servers per AP config.
	ip_addr_t dns[MW_NUM_AP_CFGS][MW_NUM_DNS_SERVERS];
	/// NTP update delay (in seconds, greater than 15)
	uint16_t ntpUpDelay;
	/// Pool length for SNTP configuration
	uint16_t ntpPoolLen;
	/// Pool for SNTP configuration, up to 3 servers concatenaded and NULL
	/// separated. Two NULL characters mark end of pool.
	char ntpPool[MW_NTP_POOL_MAXLEN];
	/// Index of the configuration used on last connection (-1 for none).
	char defaultAp;
	/// Timezone to use with SNTP.
	int8_t timezone;
	/// One DST hour will be applied if set to 1
	uint8_t dst;
	/// Gamertag
	struct mw_gamertag gamertag[MW_NUM_GAMERTAGS];
	/// Checksum
	uint8_t md5[16];
} MwNvCfg;
/** \} */

/** \addtogroup MwApi MwData Module data needed to handle module status
 *  \todo Maybe we should add a semaphore to access data in this struct.
 *  \{ */
typedef struct {
	/// System status
	MwMsgSysStat s;
	/// Sockets associated with each channel. NOTE: the index to this array
	/// must be the channel number minus 1 (as channel 0 is the control
	/// channel and has no socket associated).
	int8_t sock[MW_MAX_SOCK];
	/// Socket status. As with sock[], index must be channel number - 1.
	MwSockStat ss[MW_MAX_SOCK];
	/// Channel associated with each socket (like sock[] but reversed). NOTE:
	/// An extra socket placeholder is reserved because of server sockets that
	/// might use a temporary additional socket during the accept() stage.
	int8_t chan[MW_MAX_SOCK + 1];
//	/// Timer used to update SNTP clock.
//	os_timer_t sntpTimer;
	/// FSM queue for event reception
	QueueHandle_t q;
	/// File descriptor set for select()
	fd_set fds;
	/// Maximum socket identifier value
	int fdMax;
	/// Address of the remote end, used in UDP sockets
	struct sockaddr_in raddr[MW_MAX_SOCK];
} MwData;
/** \} */

/*
 * Private local variables
 */
/// Configuration data
static MwNvCfg cfg;
/// Module static data
static MwData d;
/// Temporal data buffer for data forwarding
static uint8_t buf[LSD_MAX_LEN];
/// Global system status flags
MwMsgSysStat s;

// Prints data of a WiFi station
static void PrintStationData(struct sdk_bss_info *bss) {
	AUTH_MODE atmp;
	// Character strings related to supported authentication modes
	const char *authStr[AUTH_MAX + 1] = {
		"OPEN", "WEP", "WPA_PSK", "WPA2_PSK", "WPA_WPA2_PSK", "???"
	};


	atmp = MIN(bss->authmode, AUTH_MAX);
	LOGI("%s, %s, ch=%d, str=%d",
			bss->ssid, authStr[atmp], bss->channel, bss->rssi);
}

/// Prints a list of found scanned stations
static void MwBssListPrint(struct sdk_bss_info *bss) {
	// Traverse the bss list, ignoring first entry
	while (bss->next.stqe_next != NULL) {
		bss = bss->next.stqe_next;
		PrintStationData(bss);
	}
	LOGI("That's all!");
}

// Raises an event pending flag on requested channel
static void MwFsmRaiseChEvent(int ch) {
	if ((ch < 1) || (ch >= LSD_MAX_CH)) return;

	s.ch_ev |= 1<<ch;
}

// Clears an event pending flag on requested channel
static void MwFsmClearChEvent(int ch) {
	if ((ch < 1) || (ch >= LSD_MAX_CH)) return;

	s.ch_ev &= ~(1<<ch);
}

#ifdef _DEBUG_MSGS
#define MwDebBssListPrint(bss)	do{MwBssListPrint(bss);}while(0)
#else
#define MwDebBssListPrint(bss)
#endif

// Scan complete callback: called when a WiFi scan operation has finished.
static void ScanCompleteCb(struct sdk_bss_info *bss,
		                   sdk_scan_status_t status) {
	MwFsmMsg m;
	m.e = MW_EV_SCAN;
	uint16_t repLen, tmp;
	MwCmd *rep;
	struct sdk_bss_info *start = bss;
	uint8_t *data;
	// Number of found access points
	uint8_t aps;

	if (status == SCAN_OK) {
		MwDebBssListPrint(bss);
		repLen = tmp = aps = 0;
		while (bss->next.stqe_next != NULL) {
			bss = bss->next.stqe_next;
			tmp += 4 + strnlen((const char*)bss->ssid, 32);
			// Check we have not exceeded maximum length, truncate
			// output if exceeded.
			if (tmp <= (LSD_MAX_LEN - 5)) {
				repLen = tmp;
				aps++;
			}
			else break;
		}
		if (!(rep = (MwCmd*) malloc(repLen + 4))) {
			LOGE("ScanCompleteCb: malloc failed!");
			return;
		}
		// Fill in reply data
		bss = start;
		rep->cmd = MW_CMD_OK;
		rep->datalen = ByteSwapWord(repLen + 1);
		// First byte is the number of found APs
		data = rep->data;
		*data++ = aps;
		tmp = 0;
		while ((bss->next.stqe_next != NULL) && (tmp < repLen)) {
			bss = bss->next.stqe_next;
			data[0] = bss->authmode;
			data[1] = bss->channel;
			data[2] = bss->rssi;
			data[3] = strnlen((const char*)bss->ssid, 32);
			memcpy(data + 4, bss->ssid, data[3]);
			tmp += 4 + data[3];
			data += 4 + data[3];
		}
		m.d = rep;
	} else {
		// Scan failed, set null pointer for the FSM to notice.
		LOGE("Scan failed with error %d!", status);
		m.d = NULL;
		
	}
	xQueueSend(d.q, &m, portMAX_DELAY);
}

static int MwChannelCheck(int ch) {
	// Check channel is valid and not in use.
	if (ch >= LSD_MAX_CH) {
		LOGE("Requested unavailable channel %d", ch);
		return -1;
	}
	if (d.ss[ch - 1]) {
		LOGW("Requested already in-use channel %d", ch);
		return -1;
	}

	return 0;
}

static int MwDnsLookup(const char* addr, const char *port,
		struct addrinfo** addr_info) {
	int err;
	struct in_addr* raddr;
	const struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};

	err = getaddrinfo(addr, port, &hints, addr_info);

	if(err || !*addr_info) {
		LOGE("DNS lookup failure %d", err);
		if(*addr_info) {
			freeaddrinfo(*addr_info);
		}
		return -1;
	}
	// DNS lookup OK
	raddr = &((struct sockaddr_in *)(*addr_info)->ai_addr)->sin_addr;
	LOGI("DNS lookup succeeded. IP=%s", inet_ntoa(*raddr));

	return 0;
}

/// Establish a connection with a remote server
static int MwFsmTcpCon(MwMsgInAddr* addr) {
	struct addrinfo *res;
	int err;
	int s;

	LOGI("Con. ch %d to %s:%s", addr->channel, addr->data,
			addr->dst_port);

	err = MwChannelCheck(addr->channel);
	if (err) {
		return err;
	}

	// DNS lookup
	err = MwDnsLookup(addr->data, addr->dst_port, &res);
	if (err) {
		return err;
	}

	s = lwip_socket(res->ai_family, res->ai_socktype, 0);
	if(s < 0) {
		LOGE("... Failed to allocate socket.");
		freeaddrinfo(res);
		return -1;
	}

	LOGI("... allocated socket");

	if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
		lwip_close(s);
		freeaddrinfo(res);
		LOGE("... socket connect failed.");
		return -1;
	}

	LOGI("... connected sock %d on ch %d", s, addr->channel);
	freeaddrinfo(res);
	// Record socket number, type and mark channel as in use.
	d.sock[addr->channel - 1] = s;
	d.ss[addr->channel - 1] = MW_SOCK_TCP_EST;
	// Record channel number associated with socket
	d.chan[s - LWIP_SOCKET_OFFSET] = addr->channel;
	// Add socket to the FD set and update maximum socket valud
	FD_SET(s, &d.fds);
	d.fdMax = MAX(s, d.fdMax);

	// Enable LSD channel
	LsdChEnable(addr->channel);
	return s;
}

static int MwFsmTcpBind(MwMsgBind *b) {
	struct sockaddr_in saddr;
	socklen_t addrlen = sizeof(saddr);
	int serv;
	int optval = 1;
	uint16_t port;
	int err;

	err = MwChannelCheck(b->channel);
	if (err) {
		return err;
	}

	// Create socket, set options
	if ((serv = lwip_socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		LOGE("Could not create server socket!");
		return -1;
	}

	if (lwip_setsockopt(serv, SOL_SOCKET, SO_REUSEADDR, &optval,
				sizeof(int)) < 0) {
		lwip_close(serv);
		LOGE("setsockopt failed!");
		return -1;
	}

	// Fill in address information
	port = ByteSwapWord(b->port);
	memset((char*)&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_len = addrlen;
	saddr.sin_addr.s_addr = lwip_htonl(INADDR_ANY);
	saddr.sin_port = lwip_htons(port);

	// Bind to address
	if (lwip_bind(serv, (struct sockaddr*)&saddr, sizeof(saddr)) < -1) {
		lwip_close(serv);
		LOGE("Bind to port %d failed!", port);
		return -1;
	}

	// Listen for incoming connections
	if (lwip_listen(serv, MW_MAX_SOCK) < 0) {
		lwip_close(serv);
		LOGE("Listen to port %d failed!", port);
		return -1;
	}
	LOGE("Listening to port %d.", port);

	// Fill in channel data
	d.sock[b->channel - 1] = serv;
	d.chan[serv - LWIP_SOCKET_OFFSET] = b->channel;
	d.ss[b->channel - 1] = MW_SOCK_TCP_LISTEN;

	// Add listener to the FD set
	FD_SET(serv, &d.fds);
	d.fdMax = MAX(serv, d.fdMax);

	return 0;
}

/// Closes a socket on the specified channel
static void MwSockClose(int ch) {
	// TODO Might need to use a mutex to access socket variables.
	// Close socket, remove from file descriptor set and mark as unused
	int idx = ch - 1;

	lwip_close(d.sock[idx]);
	FD_CLR(d.sock[idx], &d.fds);
	// No channel associated with this socket
	d.chan[d.sock[idx] - LWIP_SOCKET_OFFSET] = -1;
	d.sock[idx] = -1; // No socket on this channel
	d.ss[idx] = MW_SOCK_NONE;
}

static int MwUdpSet(MwMsgInAddr* addr) {
	int err;
	int s;
	int idx;
	unsigned int local_port;
	unsigned int remote_port;
	struct addrinfo *raddr;

	err = MwChannelCheck(addr->channel);
	if (err) {
		return err;
	}
	idx = addr->channel - 1;

	local_port = atoi(addr->src_port);
	remote_port = atoi(addr->dst_port);

	if ((s = lwip_socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
		LOGE("Failed to create UDP socket");
		return -1;
	}

	if (!remote_port && addr->data[0]) {
		// Communication with remote peer
		LOGE("UDP ch %d, port %d to addr %s:%d.", addr->channel,
				local_port, addr->data, remote_port);

		err = MwDnsLookup(addr->data, addr->dst_port, &raddr);
		if (err) {
			lwip_close(s);
			return -1;
		}
		d.raddr[idx] = *((struct sockaddr_in*)raddr->ai_addr);
		freeaddrinfo(raddr);
	} else if (local_port) {
		// Server in reuse mode
		LOGI("UDP ch %d, src port %d.", addr->channel, local_port);

		memset(d.raddr[idx].sin_zero, 0, sizeof(d.raddr[idx].sin_zero));
		d.raddr[idx].sin_len = sizeof(struct sockaddr_in);
		d.raddr[idx].sin_family = AF_INET;
		d.raddr[idx].sin_addr.s_addr = lwip_htonl(INADDR_ANY);
		d.raddr[idx].sin_port = lwip_htons(local_port);
	} else {
		LOGE("Invalid UDP socket data");
		return -1;
	}

	if (lwip_bind(s, (struct sockaddr*)&d.raddr[idx],
				sizeof(struct sockaddr_in)) < 0) {
		LOGE("bind() failed. Is UDP port in use?");
		lwip_close(s);
		return -1;
	}

	LOGI("UDP socket %d bound", s);
	// Record socket number and mark channel as in use.
	d.sock[idx] = s;
	d.chan[s - LWIP_SOCKET_OFFSET] = addr->channel;
	d.ss[idx] = MW_SOCK_UDP_READY;
	// Enable LSD channel
	LsdChEnable(addr->channel);

	// Add listener to the FD set
	FD_SET(s, &d.fds);
	d.fdMax = MAX(s, d.fdMax);

	return s;
}

/// Close all opened sockets
static void MwFsmCloseAll(void) {
	int i;

	for (i = 0; i < MW_MAX_SOCK; i++) {
		if (d.ss[i] > 0) {
			LOGI("Closing sock %d on ch %d", d.sock[i], i + 1);
			MwSockClose(i + 1);
			LsdChDisable(i + 1);
		}
	}
}

/// Check if a command is on a command list mask
static int MwCmdInList(uint8_t cmd, const uint32_t list[2]) {

	if (cmd < 32) {
		return (((1<<cmd) & list[0]) != 0);
	} else if (cmd < 64) {
		return (((1<<(cmd - 32)) & list[1]) != 0);
	} else {
		return 0;
	}
}

/// Set default configuration.
static void MwSetDefaultCfg(void) {
	memset(&cfg, 0, sizeof(cfg));
	cfg.defaultAp = -1;
	// Copy the 3 default NTP servers
	*(1 + StrCpyDst(1 + StrCpyDst(1 + StrCpyDst(cfg.ntpPool, MW_SNTP_SERV_0),
		MW_SNTP_SERV_1), MW_SNTP_SERV_2)) = '\0';
	cfg.ntpPoolLen = sizeof(MW_SNTP_SERV_0) + sizeof(MW_SNTP_SERV_1) +
		sizeof(MW_SNTP_SERV_2) + 1;
	cfg.ntpUpDelay = 300;	// Update each 5 minutes
	// NOTE: Checksum is only computed before storing configuration
}

/// Saves configuration to non volatile flash
int MwNvCfgSave(void) {
	// Compute MD5 of the configuration data
	mbedtls_md5((const unsigned char*)&cfg, ((uint32_t)&cfg.md5) - 
			((uint32_t)&cfg), cfg.md5);
#ifdef _DEBUG_MSGS
	char md5_str[33];
	md5_to_str(cfg.md5, md5_str);
	LOGI("Saved MD5: %s", md5_str);
#endif
	// Erase configuration sector
//	if (!spiflash_erase_sector(MW_CFG_FLASH_ADDR)) {
	if (sdk_spi_flash_erase_sector(MW_CFG_FLASH_SECT) !=
			SPI_FLASH_RESULT_OK) {
		LOGE("Flash sector 0x%X erase failed!", MW_CFG_FLASH_SECT);
		return -1;
	}
	// Write configuration to flash
//	if (!spiflash_write(MW_CFG_FLASH_ADDR, (uint8_t*)&cfg, sizeof(MwNvCfg))) {
	if (sdk_spi_flash_write(MW_CFG_FLASH_ADDR, (uint32_t*)&cfg,
			sizeof(MwNvCfg)) != SPI_FLASH_RESULT_OK) {
		LOGE("Flash write addr 0x%X failed!", MW_CFG_FLASH_ADDR);
		return -1;
	}
	LOGI("Configuration saved to flash.");
	return 0;
}

void MwApCfg(void) {
}

// Load configuration from flash. Return 0 if configuration was successfully
// loaded and verified, 1 if configuration check did not pass and default
// configuration has been loaded instead.
int MwCfgLoad(void) {
	uint8_t md5[16];

	// Load configuration from flash
//	spiflash_read(MW_CFG_FLASH_ADDR, (uint8_t*)&cfg, sizeof(MwNvCfg));
	sdk_spi_flash_read(MW_CFG_FLASH_ADDR, (uint32_t*)&cfg, sizeof(MwNvCfg));
	// Check MD5
	mbedtls_md5((const unsigned char*)&cfg, ((uint32_t)&cfg.md5) - 
			((uint32_t)&cfg), md5);
	if (!memcmp(cfg.md5, md5, 16)) {
		// MD5 test passed, return with loaded configuration
		d.s.cfg_ok = TRUE;
		LOGI("Configuration loaded from flash.");
		return 0;
	}
#ifdef _DEBUG_MSGS
	char md5_str[33];
	md5_to_str(cfg.md5, md5_str);
	LOGI("Loaded MD5:   %s", md5_str);
	md5_to_str(md5, md5_str);
	LOGI("Computed MD5: %s");
#endif

	// MD5 did not pass, load default configuration
	MwSetDefaultCfg();
	LOGI("Loaded default configuration.");
	return 1;
}

static void SetIpCfg(int slot) {
	if ((cfg.ip[slot].ip.addr) && (cfg.ip[slot].netmask.addr)
				&& (cfg.ip[slot].gw.addr)) {
		sdk_wifi_station_dhcpc_stop();
		if (sdk_wifi_set_ip_info(STATION_IF, cfg.ip + slot)) {
			LOGI("Static IP configuration set.");
			// Set DNS servers if available
			if (cfg.dns[slot][0].addr) {
				dns_setserver(0, cfg.dns[slot] + 0);
				if (cfg.dns[slot][1].addr) {
					dns_setserver(1, cfg.dns[slot] + 1);
				}
			}
		} else {
			LOGE("Failed setting static IP configuration.");
			sdk_wifi_station_dhcpc_start();
		}
	} else {
		LOGI("Setting DHCP IP configuration.");
		sdk_wifi_station_dhcpc_start();
	}
}

void MwApJoin(uint8_t n) {
	struct sdk_station_config stcfg;

	SetIpCfg(n);
	memset(&stcfg, 0, sizeof(struct sdk_station_config));
	strncpy((char*)stcfg.ssid, cfg.ap[n].ssid, MW_SSID_MAXLEN);
	strncpy((char*)stcfg.password, cfg.ap[n].pass, MW_PASS_MAXLEN);
	sdk_wifi_set_opmode(STATION_MODE);
	sdk_wifi_station_set_config(&stcfg);
	sdk_wifi_station_connect();
	LOGI("AP ASSOC %d", n);
	d.s.sys_stat = MW_ST_AP_JOIN;
}

void MwSysStatFill(MwCmd *rep) {
//	Warning: dt_ok not supported yet
	rep->cmd = MW_CMD_OK;
	rep->datalen = ByteSwapWord(sizeof(MwMsgSysStat));
	rep->sysStat.st_flags = d.s.st_flags;
}

/************************************************************************//**
 * Module initialization. Must be called in user_init() context.
 ****************************************************************************/
void MwInit(void) {
	MwFsmMsg m;
	struct timezone tz;
	char *sntpSrv[SNTP_NUM_SERVERS_SUPPORTED];
	size_t sntpLen = 0;
	int i;
	uint8_t tmp;

	// If enabled, disable auto connect
	if (sdk_wifi_station_get_auto_connect())
		sdk_wifi_station_set_auto_connect(0);

	// Get flash chip information
	LOGI("SPI Flash id: 0x%08X", sdk_spi_flash_get_id());
	// Load configuration from flash
	MwCfgLoad();
	// Set default values for global variables
	s.st_flags = 0;
	memset(&d, 0, sizeof(d));
	d.s.sys_stat = MW_ST_INIT;
	for (i = 0; i < MW_MAX_SOCK; i++) {
		d.sock[i] = -1;
		d.chan[i] = -1;
	}

	// If default IP configuration saved, apply it
	// NOTE: IP configuration can only be set in user_init() context.
	tmp = (uint8_t) cfg.defaultAp;
	SetIpCfg(tmp);

	// Create system queue
    if (!(d.q = xQueueCreate(MW_FSM_QUEUE_LEN, sizeof(MwFsmMsg)))) {
		LOGE("Could not create system queue!");
		return;
	};
  	// Create FSM task
	if (pdPASS != xTaskCreate(MwFsmTsk, "FSM", MW_FSM_STACK_LEN, &d.q,
			MW_FSM_PRIO, NULL)) {
		LOGE("Could not create Fsm task!");
	}
	// Create task for receiving data from sockets
	if (pdPASS != xTaskCreate(MwFsmSockTsk, "SCK", MW_SOCK_STACK_LEN, &d.q,
			MW_SOCK_PRIO, NULL)) {
		LOGE("Could not create FsmSock task!");
	}
	// Create task for polling WiFi status
	if (pdPASS != xTaskCreate(MwWiFiStatPollTsk, "WPOL", MW_WPOLL_STACK_LEN,
			&d.q, MW_WPOLL_PRIO, NULL)) {
		LOGE("Could not create WiFiStatPoll task!");
	}
	// Initialize SNTP
	// TODO: Maybe this should be moved to the "READY" state
	for (i = 0, sntpSrv[0] = cfg.ntpPool; (i < SNTP_NUM_SERVERS_SUPPORTED) &&
			((sntpLen = strlen(sntpSrv[i])) > 0); i++) {
			sntpSrv[i + 1] = sntpSrv[i] + sntpLen + 1;
			LOGE("SNTP server: %s", sntpSrv[i]);
	}
	if (i) {
		LOGI("%d SNTP servers found.", i);
		tz.tz_minuteswest = cfg.timezone * 60;
		tz.tz_dsttime = cfg.dst;
		tz.tz_minuteswest = 0;
		sntp_initialize(&tz);
		LOGI("Setting update delay to %d seconds.", cfg.ntpUpDelay);
		/// \todo FIXME Setting sntp servers breaks tasking when using
		/// latest esp-open-rtos
		sntp_set_update_delay(cfg.ntpUpDelay * 1000);
//		if (sntp_set_servers(sntpSrv, i)) {
//			LOGI("Error setting SNTP servers!");
//			return;
//		}
	} else LOGE("No NTP servers found!");
	// Initialize LSD layer (will create receive task among other stuff).
	LsdInit(d.q);
	LsdChEnable(MW_CTRL_CH);
	// Send the init done message
	m.e = MW_EV_INIT_DONE;
	xQueueSend(d.q, &m, portMAX_DELAY);
}

static int MwSntpCfgGet(MwMsgSntpCfg *sntp) {
	sntp->dst = cfg.dst;
	sntp->tz = cfg.timezone;
	sntp->upDelay = ByteSwapWord(cfg.ntpUpDelay);
	memcpy(sntp->servers, cfg.ntpPool, cfg.ntpPoolLen);

	return sizeof(sntp->dst) + sizeof(sntp->tz) + sizeof(sntp->upDelay) +
		cfg.ntpPoolLen;
}

static void log_ip_cfg(MwMsgIpCfg *ip)
{
	char ip_str[16];

	ipv4_to_str(ip->cfg.ip.addr, ip_str);
	LOGI("IP:   %s", ip_str);
	ipv4_to_str(ip->cfg.netmask.addr, ip_str);
	LOGI("MASK: %s", ip_str);
	ipv4_to_str(ip->cfg.gw.addr, ip_str);
	LOGI("GW:   %s", ip_str);
	ipv4_to_str(ip->dns1.addr, ip_str);
	LOGI("DNS1: %s", ip_str);
	ipv4_to_str(ip->dns2.addr, ip_str);
	LOGI("DNS2: %s\n", ip_str);
}

/// Process command requests (coming from the serial line)
int MwFsmCmdProc(MwCmd *c, uint16_t totalLen) {
	MwCmd reply;
	uint16_t len = ByteSwapWord(c->datalen);
	time_t ts;	// For datetime replies
	uint16_t tmp, replen;
	
	// Sanity check: total Lengt - header length = data length
	if ((totalLen - MW_CMD_HEADLEN) != len) {
		LOGE("MwFsmCmdProc, ERROR: Length inconsistent");
		LOGE("totalLen=%d, dataLen=%d", totalLen, len);
		return MW_CMD_FMT_ERROR;
	}

	// parse command
	LOGI("CmdRequest: %d", ByteSwapWord(c->cmd));
	switch (ByteSwapWord(c->cmd)) {
		case MW_CMD_VERSION:
			reply.cmd = MW_CMD_OK;
			reply.datalen = ByteSwapWord(2 + sizeof(MW_FW_VARIANT) - 1);
			reply.data[0] = MW_FW_VERSION_MAJOR;
			reply.data[1] = MW_FW_VERSION_MINOR;
			memcpy(reply.data + 2, MW_FW_VARIANT, sizeof(MW_FW_VARIANT) - 1);
			LsdSend((uint8_t*)&reply, ByteSwapWord(reply.datalen) +
					MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_ECHO:		// Echo request
			reply.cmd = MW_CMD_OK;
			reply.datalen = c->datalen;
			LOGI("SENDING ECHO!");
			// Send the command response
			if ((LsdSplitStart((uint8_t*)&reply, MW_CMD_HEADLEN,
					len + MW_CMD_HEADLEN, 0) == MW_CMD_HEADLEN) &&
					len) {
				// Send echoed data
				LsdSplitEnd(c->data, len);
			}
			break;

		case MW_CMD_AP_SCAN:
			// Only works when on IDLE state.
			if (MW_ST_IDLE == d.s.sys_stat) {
				d.s.sys_stat = MW_ST_SCAN;
	    		LOGI("SCAN!");
	    		sdk_wifi_station_scan(NULL, (sdk_scan_done_cb_t)
						ScanCompleteCb);
			}
			break;

		case MW_CMD_AP_CFG:
			reply.datalen = 0;
			tmp = c->apCfg.cfgNum;
			if (tmp >= MW_NUM_AP_CFGS) {
				LOGE("Tried to set AP for cfg %d!", tmp);
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				// Copy configuration and save it to flash
				LOGI("Setting AP configuration %d...", tmp);
				strncpy(cfg.ap[tmp].ssid, c->apCfg.ssid, MW_SSID_MAXLEN);
				strncpy(cfg.ap[tmp].pass, c->apCfg.pass, MW_PASS_MAXLEN);
				LOGI("ssid: %s, pass: %s", cfg.ap[tmp].ssid,
						cfg.ap[tmp].pass);
				cfg.defaultAp = tmp;
				if (MwNvCfgSave() < 0) {
					reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				} else {
					reply.cmd = MW_CMD_OK;
				}
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_AP_CFG_GET:
			tmp = c->apCfg.cfgNum;
			if (tmp >= MW_NUM_AP_CFGS) {
				LOGE("Requested AP for cfg %d!", tmp);
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				reply.datalen = 0;
				replen = 0;
			} else {
				LOGI("Getting AP configuration %d...", tmp);
				reply.cmd = MW_CMD_OK;
				replen = sizeof(MwMsgApCfg);
				reply.datalen = ByteSwapWord(sizeof(MwMsgApCfg));
				reply.apCfg.cfgNum = c->apCfg.cfgNum;
				strncpy(reply.apCfg.ssid, cfg.ap[tmp].ssid, MW_SSID_MAXLEN);
				strncpy(reply.apCfg.pass, cfg.ap[tmp].pass, MW_PASS_MAXLEN);
				LOGI("ssid: %s, pass: %s", reply.apCfg.ssid,
						reply.apCfg.pass);
			} 
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_IP_CURRENT:
			reply.datalen = replen = 0;
			reply.cmd = MW_CMD_OK;
			LOGI("Getting current IP configuration...");
			replen = sizeof(MwMsgIpCfg);
			reply.datalen = ByteSwapWord(sizeof(MwMsgIpCfg));
			reply.ipCfg.cfgNum = 0;
			sdk_wifi_get_ip_info(STATION_IF, &reply.ipCfg.cfg);
			reply.ipCfg.dns1 = *dns_getserver(0);
			reply.ipCfg.dns2 = *dns_getserver(1);
			log_ip_cfg(&reply.ipCfg);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_IP_CFG:
			tmp = (uint8_t)c->ipCfg.cfgNum;
			reply.datalen = 0;
			if (tmp >= MW_NUM_AP_CFGS) {
				LOGE("Tried to set IP for cfg %d!", tmp);
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				LOGI("Setting IP configuration %d...", tmp);
				cfg.ip[tmp] = c->ipCfg.cfg;
				cfg.dns[tmp][0] = c->ipCfg.dns1;
				cfg.dns[tmp][1] = c->ipCfg.dns2;
				log_ip_cfg(&c->ipCfg);
				if (MwNvCfgSave() < 0) {
					reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				} else {
					reply.cmd = MW_CMD_OK;
				}
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_IP_CFG_GET:
			tmp = c->ipCfg.cfgNum;
			reply.datalen = replen = 0;
			if (tmp >= MW_NUM_AP_CFGS) {
				LOGE("Requested IP for cfg %d!", tmp);
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				LOGI("Getting IP configuration %d...", tmp);
				reply.cmd = MW_CMD_OK;
				replen = sizeof(MwMsgIpCfg);
				reply.datalen = ByteSwapWord(sizeof(MwMsgIpCfg));
				reply.ipCfg.cfgNum = c->ipCfg.cfgNum;
				reply.ipCfg.cfg = cfg.ip[tmp];
				reply.ipCfg.dns1 = cfg.dns[tmp][0];
				reply.ipCfg.dns2 = cfg.dns[tmp][1];
				log_ip_cfg(&reply.ipCfg);
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_DEF_AP_CFG:
			reply.datalen = 0;
			tmp = c->data[0];
			if (tmp < MW_NUM_AP_CFGS) {
				cfg.defaultAp = tmp;
				if (MwNvCfgSave() != 0) {
					LOGE("Failed to set default AP: %d", cfg.defaultAp);
					reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				} else {
					LOGI("Set default AP: %d", cfg.defaultAp);
					reply.cmd = MW_CMD_OK;
				}
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_DEF_AP_CFG_GET:
			reply.datalen = ByteSwapWord(1);
			reply.cmd = MW_CMD_OK;
			reply.data[0] = cfg.defaultAp;
			LOGI("Sending default AP: %d", cfg.defaultAp);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + 1, 0);
			break;

		case MW_CMD_AP_JOIN:
			// Start connecting to AP and jump to AP_JOIN state
			reply.datalen = 0;
			if ((c->data[0] >= MW_NUM_AP_CFGS) ||
					!(cfg.ap[c->data[0]].ssid[0])) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				LOGE("Invalid AP_JOIN on config %d", c->data[0]);
			} else {
				MwApJoin(c->data[0]);
				reply.cmd = MW_CMD_OK;
				// TODO: Save configuration to update default AP?
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_AP_LEAVE:	// Leave access point
			LOGI("Disconnecting from AP");
			// Close all opened sockets
			MwFsmCloseAll();
			// Disconnect and switch to IDLE state
			sdk_wifi_station_disconnect();
			d.s.sys_stat = MW_ST_IDLE;
			LOGI("IDLE!");
			reply.cmd = MW_OK;
			reply.datalen = 0;
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_TCP_CON:
			LOGI("TRYING TO CONNECT TCP SOCKET...");
			reply.datalen = 0;
			reply.cmd = (MwFsmTcpCon(&c->inAddr) < 0)?ByteSwapWord(
					MW_CMD_ERROR):MW_CMD_OK;
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_TCP_BIND:
			reply.datalen = 0;
			reply.cmd = MwFsmTcpBind(&c->bind)?ByteSwapWord(MW_CMD_ERROR):
				MW_CMD_OK;
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_CLOSE:
			reply.datalen = 0;
			// If channel number OK, disconnect the socket on requested channel
			if ((c->data[0] > 0) && (c->data[0] <= LSD_MAX_CH) &&
					d.ss[c->data[0] - 1]) {
				LOGI("Closing socket %d from channel %d",
						d.sock[c->data[0] - 1], c->data[0]);
				MwSockClose(c->data[0]);
				LsdChDisable(c->data[0]);
				reply.cmd = MW_CMD_OK;
			} else {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				LOGE("Requested disconnect of not opened channel %d.",
						c->data[0]);
			}
			reply.cmd = ByteSwapWord(reply.cmd);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_UDP_SET:
			LOGI("Configuring UDP socket...");
			reply.datalen = 0;
			reply.cmd = (MwUdpSet(&c->inAddr) < 0)?ByteSwapWord(
					MW_CMD_ERROR):MW_CMD_OK;
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_SOCK_STAT:
			if ((c->data[0] > 0) && (c->data[0] < LSD_MAX_CH)) {
				// Send channel status and clear channel event flag
				replen = 1;
				reply.datalen = ByteSwapWord(1);
				reply.data[0] = (uint8_t)d.ss[c->data[0] - 1];
				MwFsmClearChEvent(c->data[0]);
			} else {
				reply.datalen = replen = 0;
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				LOGE("Requested unavailable channel!");
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_PING:
			LOGE("PING unimplemented");
			break;

		case MW_CMD_SNTP_CFG:
			reply.datalen = 0;
			cfg.ntpPoolLen = len - 4;
			if ((cfg.ntpPoolLen > MW_NTP_POOL_MAXLEN) ||
					(c->sntpCfg.tz < -11) || (c->sntpCfg.tz > 13)) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				cfg.ntpUpDelay = ByteSwapWord(c->sntpCfg.upDelay);
				cfg.timezone = c->sntpCfg.tz;
				cfg.dst = c->sntpCfg.dst;
				memcpy(cfg.ntpPool, c->sntpCfg.servers, cfg.ntpPoolLen);
				if (MwNvCfgSave() < 0) {
					reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				} else {
					reply.cmd = MW_CMD_OK;
				}
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_SNTP_CFG_GET:
			replen = MwSntpCfgGet(&reply.sntpCfg);
			reply.datalen = ByteSwapWord(replen);
			reply.cmd = MW_CMD_OK;
			LOGI("sending configuration (%d bytes)", replen);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_DATETIME:
			reply.cmd = MW_CMD_OK;
			reply.datetime.dtBin[0] = 0;
			reply.datetime.dtBin[1] = ByteSwapDWord(ts);
			ts = time(NULL);
			strcpy(reply.datetime.dtStr, ctime(&ts));
			LOGI("sending datetime %s", reply.datetime.dtStr);
			tmp = 2*sizeof(uint32_t) + strlen(reply.datetime.dtStr);
			reply.datalen = ByteSwapWord(tmp);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + tmp, 0);
			break;

		case MW_CMD_DT_SET:
			LOGE("DT_SET unimplemented");
			break;

		case MW_CMD_FLASH_WRITE:
			reply.datalen = 0;
			reply.cmd = MW_CMD_OK;
			// Compute effective flash address
			c->flData.addr = ByteSwapDWord(c->flData.addr) +
				MW_FLASH_USER_BASE_ADDR;
			// Check for overflows (avoid malevolous attempts to write to
			// protected area
			if ((c->flData.addr + (len - sizeof(uint32_t) - 1)) <
					MW_FLASH_USER_BASE_ADDR) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				LOGE("Address/length combination overflows!");
			} else if (sdk_spi_flash_write(c->flData.addr,
						(uint32_t*)c->flData.data, len - sizeof(uint32_t)) !=
					SPI_FLASH_RESULT_OK) {
//			} else if (!spiflash_write(c->flData.addr, (uint8_t*)c->flData.data,
//					len - sizeof(uint32_t))) {
				LOGE("Write to flash failed!");
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_FLASH_READ:
			// Compute effective flash address
			c->flRange.addr = ByteSwapDWord(c->flRange.addr) +
				MW_FLASH_USER_BASE_ADDR;
			c->flRange.len = ByteSwapWord(c->flRange.len);
			// Check requested length fits a transfer and there's no overflow
			// on address and length (maybe a malicious attempt to read
			// protected area
			if ((c->flRange.len > MW_MSG_MAX_BUFLEN) || ((c->flRange.addr - 1 +
					c->flRange.len) < MW_FLASH_USER_BASE_ADDR)) {
				reply.datalen = 0;
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				LOGE("Invalid address/length combination.");
			// Perform read and check result
			} else if (sdk_spi_flash_read(c->flRange.addr,
						(uint32_t*)reply.data, c->flRange.len) !=
					SPI_FLASH_RESULT_OK) {
//			} else if (!spiflash_read(c->flRange.addr,
//						(uint8_t*)reply.data, c->flRange.len)) {
				reply.datalen = 0;
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				LOGE("Flash read failed!");
			} else {
				reply.datalen = ByteSwapWord(c->flRange.len);
				reply.cmd = MW_CMD_OK;
				LOGI("Flash read OK!");
			}
			LsdSend((uint8_t*)&reply, c->flRange.len + MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_FLASH_ERASE:
			reply.datalen = 0;
			// Check for sector overflow
			c->flSect = ByteSwapWord(c->flSect) + MW_FLASH_USER_BASE_SECT;
			if (c->flSect < MW_FLASH_USER_BASE_SECT) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				LOGW("Wrong sector number.");
			} else if (sdk_spi_flash_erase_sector(c->flSect) !=
					SPI_FLASH_RESULT_OK) {
//			} else if (!spiflash_erase_sector((c->flSect)<<12)) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				LOGE("Sector erase failed!");
			} else {
				reply.cmd = MW_CMD_OK;
				LOGE("Sector erase OK!");
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_FLASH_ID:
			reply.cmd = MW_CMD_OK;
			reply.datalen = ByteSwapWord(sizeof(uint32_t));
			reply.flId = sdk_spi_flash_get_id();
			LsdSend((uint8_t*)&reply, sizeof(uint32_t) + MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_SYS_STAT:
			MwSysStatFill(&reply);
			LOGI("%02X %02X %02X %02X", reply.data[0], reply.data[1],
					reply.data[2], reply.data[3]);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + sizeof(MwMsgSysStat),
				0);
			break;

		case MW_CMD_DEF_CFG_SET:
			reply.datalen = 0;
			// Check lengt and magic value
			if ((len != 4) || (c->dwData[0] !=
						ByteSwapDWord(MW_FACT_RESET_MAGIC))) {
				LOGE("Wrong DEF_CFG_SET command invocation!");
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else if (sdk_spi_flash_erase_sector(MW_CFG_FLASH_SECT) !=
					SPI_FLASH_RESULT_OK) {
//			} else if (!spiflash_erase_sector(MW_CFG_FLASH_ADDR)) {
				LOGE("Config flash sector erase failed!");
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				LOGI("Configuration set to default.");
				reply.cmd = MW_CMD_OK;
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_HRNG_GET:
			replen = ByteSwapWord(c->rndLen);
			if (replen > MW_CMD_MAX_BUFLEN) {
				replen = 0;
				reply.datalen = 0;
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				reply.cmd = MW_CMD_OK;
				reply.datalen = c->rndLen;
				hwrand_fill(reply.data, replen);
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_BSSID_GET:
			reply.datalen = ByteSwapWord(6);
			reply.cmd = MW_CMD_OK;
			sdk_wifi_get_macaddr(c->data[0], reply.data);
			LOGI("Got BSSID(%d) %02X:%02X:%02X:%02X:%02X:%02X",
					c->data[0], reply.data[0], reply.data[1],
					reply.data[2], reply.data[3],
					reply.data[4], reply.data[5]);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + 6, 0);
			break;

		case MW_CMD_GAMERTAG_SET:
			reply.datalen = 0;
			if (c->gamertag_set.slot >= MW_NUM_GAMERTAGS ||
					len != sizeof(struct mw_gamertag_set_msg)) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				// Copy gamertag and save to flash
				memcpy(&cfg.gamertag[c->gamertag_set.slot],
						&c->gamertag_set.gamertag,
						sizeof(struct mw_gamertag));
				MwNvCfgSave();
				reply.cmd = MW_CMD_OK;
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_GAMERTAG_GET:
			if (c->data[0] >= MW_NUM_GAMERTAGS) {
				replen = 0;
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				replen = sizeof(struct mw_gamertag);
				memcpy(&reply.gamertag_get, &cfg.gamertag[c->data[0]],
						replen);
				reply.cmd = MW_CMD_OK;
			}
			reply.datalen = ByteSwapWord(replen);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_LOG:
			puts((char*)c->data);
			reply.cmd = MW_CMD_OK;
			reply.datalen = 0;
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_FACTORY_RESET:
			reply.datalen = 0;
			MwSetDefaultCfg();
			if (MwNvCfgSave() < 0) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				reply.cmd = MW_CMD_OK;
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		default:
			LOGE("UNKNOWN REQUEST!");
			break;
	}
	return MW_OK;
}

static int MwUdpSend(int idx, const void *data, int len) {
	struct sockaddr_in remote;
	int s = d.sock[idx];
	int sent;

	if (d.raddr[idx].sin_addr.s_addr) {
		sent = lwip_sendto(s, data, len, 0, (struct sockaddr*)
				&d.raddr[idx], sizeof(struct sockaddr_in));
	} else {
		// Reuse mode, extract address from leading bytes
		// NOTE: d.raddr[idx].sin_addr.s_addr == INADDR_ANY
		remote.sin_addr.s_addr = *((int32_t*)data);
		remote.sin_port = *((int16_t*)(data + 4));
		remote.sin_family = AF_INET;
		remote.sin_len = sizeof(struct sockaddr_in);
		memset(remote.sin_zero, 0, sizeof(remote.sin_zero));
		sent = lwip_sendto(s, data + 6, len - 6, 0, (struct sockaddr*)
				&remote, sizeof(struct sockaddr_in)) + 6;
	}

	return sent;
}

static int MwSend(int ch, const void *data, int len) {
	int idx = ch - 1;
	int s = d.sock[idx];

	switch (d.ss[idx]) {
		case MW_SOCK_TCP_EST:
			return lwip_send(s, data, len, 0);

		case MW_SOCK_UDP_READY:
			return MwUdpSend(idx, data, len);
			break;

		default:
			return -1;
	}
}

// Process messages during ready stage
void MwFsmReady(MwFsmMsg *msg) {
	// Pointer to the message buffer (from RX line).
	MwMsgBuf *b = msg->d;
	MwCmd *rep;

	switch (msg->e) {
		case MW_EV_WIFI:		///< WiFi events, excluding scan related.
			LOGI("WIFI_EVENT (not parsed)");
			break;

		case MW_EV_SCAN:		///< WiFi scan complete.
			LOGI("EV_SCAN (not parsed)");
			break;

		case MW_EV_SER_RX:		///< Data reception from serial line.
			LOGI("Serial recvd %d bytes.", b->len);
			// If using channel 0, process command. Else forward message
			// to the appropiate socket.
			if (MW_CTRL_CH == b->ch) {
				// Check command is allowed on READY state
				if (MwCmdInList(b->cmd.cmd>>8, readyCmdMask)) {
					MwFsmCmdProc((MwCmd*)b, b->len);
				} else {
					LOGE("Command %d not allowed on READY state",
							b->cmd.cmd>>8);
					// TODO: Throw error event?
					rep = (MwCmd*)msg->d;
					rep->datalen = 0;
					rep->cmd = ByteSwapWord(MW_CMD_ERROR);
					LsdSend((uint8_t*)rep, MW_CMD_HEADLEN, 0);
				}
			} else {
				// Forward message if channel is enabled.
				if (b->ch < LSD_MAX_CH && d.ss[b->ch - 1]) {
					if (MwSend(b->ch, b->data, b->len) != b->len) {
						LOGE("ch %d socket send error!", b->ch);
						// TODO throw error event?
						rep = (MwCmd*)msg->d;
						rep->datalen = 0;
						rep->cmd = ByteSwapWord(MW_CMD_ERROR);
						LsdSend((uint8_t*)rep, MW_CMD_HEADLEN, 0);
					}
				} else {
					LOGE("Requested to forward data on wrong channel: %d",
							b->ch);
				}
			}
			break;

		case MW_EV_TCP_CON:		///< TCP connection established.
			LOGI("TCP_CON (not parsed)");
			break;

		case MW_EV_TCP_RECV:	///< Data received from TCP connection.
			// Forward data to the appropiate channel of serial line
//			LsdSend(, , d.ss[b->ch - 1]);
			LOGI("TCP_RECV (not parsed)");
			break;

		case MW_EV_TCP_SENT:	///< Data sent to peer on TCP connection.
			LOGI("TCP_SENT (not parsed)");
			break;

		case MW_EV_UDP_RECV:	///< Data received from UDP connection.
			LOGI("UDP_RECV (not parsed)");
			break;

		case MW_EV_CON_DISC:	///< TCP disconnection.
			LOGI("CON_DISC (not parsed)");
			break;

		case MW_EV_CON_ERR:		///< TCP connection error.
			LOGI("CON_ERR (not parsed)");
			break;

		default:
			LOGI("MwFsmReady: UNKNOKWN EVENT!");
			break;
	}
}

static void MwFsm(MwFsmMsg *msg) {
	MwMsgBuf *b = msg->d;
	MwCmd *rep;

	switch (d.s.sys_stat) {
		case MW_ST_INIT:
			// Ignore all events excepting the INIT DONE one
			if (msg->e == MW_EV_INIT_DONE) {
				LOGI("INIT DONE!");
				// If there's a valid AP configuration, try to join it and
				// jump to the AP_JOIN state. Else jump to IDLE state.
//				if ((cfg.defaultAp >= 0) && (cfg.defaultAp < MW_NUM_AP_CFGS)) {
//					MwApJoin(cfg.defaultAp);
//					// TODO: Maybe we should set an AP join timeout.
//				} else {
//					LOGE("No default AP found.\nIDLE!");
					d.s.sys_stat = MW_ST_IDLE;
//				}
			}
			break;

		case MW_ST_AP_JOIN:
			if (MW_EV_WIFI == msg->e) {
				LOGI("WiFi event: %d", (uint32_t)msg->d);
				switch ((uint32_t)msg->d) {
					case STATION_GOT_IP:
						// Connected!
						LOGI("READY!");
						d.s.sys_stat = MW_ST_READY;
						d.s.online = TRUE;
						break;

					case STATION_CONNECTING:
						break;

					case STATION_IDLE:
					case STATION_WRONG_PASSWORD:
					case STATION_NO_AP_FOUND:
					case STATION_CONNECT_FAIL:
					default:
						// Error
						sdk_wifi_station_disconnect();
						LOGE("Could not connect to AP!, IDLE!");
						d.s.sys_stat = MW_ST_IDLE;
				}
			} else if (MW_EV_SER_RX == msg->e) {
				// The only rx events supported during AP_JOIN are AP_LEAVE,
				// VERSION_GET and SYS_STAT
				if (MW_CMD_AP_LEAVE == (b->cmd.cmd>>8)) {
					MwFsmCmdProc((MwCmd*)b, b->len);
				} else if (MW_CMD_VERSION) {
					rep = (MwCmd*)msg->d;
					rep->cmd = MW_CMD_OK;
					rep->datalen = ByteSwapWord(2 + sizeof(MW_FW_VARIANT) - 1);
					rep->data[0] = MW_FW_VERSION_MAJOR;
					rep->data[1] = MW_FW_VERSION_MINOR;
					memcpy(rep->data + 2, MW_FW_VARIANT,
							sizeof(MW_FW_VARIANT) - 1);
					LsdSend((uint8_t*)rep, ByteSwapWord(rep->datalen) +
							MW_CMD_HEADLEN, 0);
				} else if (MW_CMD_SYS_STAT == (b->cmd.cmd>>8)) {
					rep = (MwCmd*)msg->d;
					MwSysStatFill(rep);
					LOGI("%02X %02X %02X %02X", rep->data[0],
							rep->data[1], rep->data[2], rep->data[3]);
					LsdSend((uint8_t*)rep, sizeof(MwMsgSysStat) + MW_CMD_HEADLEN,
							0);
				} else {
					LOGE("Command %d not allowed on AP_JOIN state",
							b->cmd.cmd>>8);
				}
			}
			break;

		case MW_ST_IDLE:
			// IDLE state is abandoned once connected to an AP
			if (MW_EV_SER_RX == msg->e) {
				// Parse commands on channel 0 only
				LOGD("Serial recvd %d bytes.", b->len);
				if (MW_CTRL_CH == b->ch) {
					// Check command is allowed on IDLE state
					if (MwCmdInList(b->cmd.cmd>>8, idleCmdMask)) {
						MwFsmCmdProc((MwCmd*)b, b->len);
					} else {
						LOGE("Command %d not allowed on IDLE state",
								b->cmd.cmd>>8);
						// TODO: Throw error event?
						rep = (MwCmd*)msg->d;
						rep->datalen = 0;
						rep->cmd = ByteSwapWord(MW_CMD_ERROR);
						LsdSend((uint8_t*)rep, MW_CMD_HEADLEN, 0);
					}
				} else {
					LOGE("IDLE received data on non ctrl channel!");
				}
			}
			break;

		case MW_ST_SCAN:
			// Ignore events until we receive the scan result
			if (MW_EV_SCAN == msg->e) {
				// We receive the station data reply ready to be sent
				LOGI("Sending station data");
				rep = (MwCmd*)msg->d;
				LsdSend((uint8_t*)rep, ByteSwapWord(rep->datalen) +
						MW_CMD_HEADLEN, 0);
				free(rep);
				d.s.sys_stat = MW_ST_IDLE;
				LOGI("IDLE!");
			}
			break;

		case MW_ST_READY:
			// Module ready. Here we will have most of the activity
			MwFsmReady(msg);
			// NOTE: LSD channel 0 is treated as a special control channel.
			// All other channels are socket-bridged.
			
			break;

		case MW_ST_TRANSPARENT:
			LOGI("TRANSPARENT!");
			break;


		default:
			break;
	}
}

// Accept incoming connection and get fds. Then close server socket (no more
// connections allowed on this port unless explicitly requested again).
static int MwAccept(int sock, int ch) {
	// Client address
	struct sockaddr_in caddr;
	socklen_t addrlen = sizeof(caddr);
	int newsock;

	if ((newsock = lwip_accept(sock, (struct sockaddr*)&caddr,
					&addrlen)) < 0) {
		LOGE("Accept failed for socket %d, channel %d", sock, ch);
		return -1;
	}
	// Connection accepted, add to the FD set
	LOGI("Socket %d, channel %d: established connection from %s.",
			newsock, ch, inet_ntoa(caddr.sin_addr));
	FD_SET(newsock, &d.fds);
	d.fdMax = MAX(newsock, d.fdMax);
	// Close server socket and remove it from the set
	lwip_close(sock);
	FD_CLR(sock, &d.fds);
	// Update channel data
	d.chan[newsock - LWIP_SOCKET_OFFSET] = ch;
	d.sock[ch - 1] = newsock;
	d.ss[ch - 1] = MW_SOCK_TCP_EST;

	// Enable channel to send/receive data
	LsdChEnable(ch);

	return 0;
}

static int MwUdpRecv(int idx, char *buf) {
	ssize_t recvd;
	int s = d.sock[idx];
	struct sockaddr_in remote;
	socklen_t addr_len = sizeof(remote);

	if (d.raddr->sin_addr.s_addr) {
		// Receive only from specified address
		recvd = lwip_recvfrom(s, buf, LSD_MAX_LEN, 0,
				(struct sockaddr*)&remote, &addr_len);
		if (recvd > 0) {
			if (remote.sin_addr.s_addr != d.raddr[idx].sin_addr.s_addr) {
				LOGE("Discarding UDP packet from unknown addr");
				recvd = -1;
			}
		}
	} else {
		// Reuse mode, data is preceded by remote IPv4 and port
		recvd = lwip_recvfrom(s, buf + 6, LSD_MAX_LEN - 6, 0,
				(struct sockaddr*)&remote, &addr_len);
		if (recvd > 0) {
			*((uint32_t*)buf) = remote.sin_addr.s_addr;
			*((uint16_t*)(buf + 4)) = remote.sin_port;
			recvd += 6;
		}
	}

	return recvd;
}

static int MwRecv(int ch, char *buf, int len) {
	int idx = ch - 1;
	int s = d.sock[idx];
	// No IPv6 support yet
	ssize_t recvd;
	UNUSED_PARAM(len);

	switch(d.ss[idx]) {
		case MW_SOCK_TCP_EST:
			return lwip_recv(s, buf, LSD_MAX_LEN, 0);

		case MW_SOCK_UDP_READY:
			recvd = MwUdpRecv(idx, buf);
			return recvd;

		default:
			return -1;
	}
}

/// Polls sockets for data or incoming connections using select()
/// \note Maybe MwWiFiStatPollTsk() task should be merged with this one.
void MwFsmSockTsk(void *pvParameters) {
	fd_set readset;
	int i, ch, retval;
	int led = 0;
	int max;
	ssize_t recvd;
	struct timeval tv = {
		.tv_sec = 1,
		.tv_usec = 0
	};

	//QueueHandle_t *q = (QueueHandle_t *)pvParameters;
	UNUSED_PARAM(pvParameters);
	FD_ZERO(&readset);
	d.fdMax = -1;

	while (1) {
		gpio_write(LED_GPIO_PIN, (led++)&1);
		// Update list of active sockets
		readset = d.fds;

		// Wait until event or timeout
		// TODO: d.fdMax is initialized to -1. How does select() behave if
		// nfds = 0?
		LOGD(".");
		if ((retval = select(d.fdMax + 1, &readset, NULL, NULL, &tv)) < 0) {
			// Error.
			LOGE("select() completed with error!");
			vTaskDelayMs(1000);
			continue;
		}
		// If select returned 0, there was a timeout
		if (0 == retval) continue;
		// Poll the socket for data, and forward through the associated
		// channel.
		max = d.fdMax;
		for (i = LWIP_SOCKET_OFFSET; i <= max; i++) {
			if (FD_ISSET(i, &readset)) {
				// Check if new connection or data received
				ch = d.chan[i - LWIP_SOCKET_OFFSET];
				if (d.ss[ch - 1] != MW_SOCK_TCP_LISTEN) {
					LOGD("Rx: sock=%d, ch=%d", i, ch);
					if ((recvd = MwRecv(ch, (char*)buf, LSD_MAX_LEN)) < 0) {
						// Error!
						MwSockClose(ch);
						LsdChDisable(ch);
						LOGE("Error %d receiving from socket!", recvd);
					} else if (0 == recvd) {
						// Socket closed
						// TODO: A listen on a socket closed, should trigger
						// a 0-byte reception, for the client to be able to
						// check server state and close the connection.
						LOGD("Received 0!");
						MwSockClose(ch);
						LOGE("Socket closed!");
						MwFsmRaiseChEvent(ch);
						// Send a 0-byte frame for the receiver to wake up and
						// notice the socket close
						LsdSend(NULL, 0, ch);
						LsdChDisable(ch);
					} else {
						LOGD("%02X %02X %02X %02X: WF->MD %d bytes",
								buf[0], buf[1], buf[2], buf[3],
								recvd);
						LsdSend(buf, (uint16_t)recvd, ch);
					}
				} else {
					// Incoming connection. Accept it.
					MwAccept(i, ch);
					MwFsmRaiseChEvent(ch);
				}
			}
		}
	} // while (1)
}

void MwFsmTsk(void *pvParameters) {
	QueueHandle_t *q = (QueueHandle_t *)pvParameters;
	MwFsmMsg m;

	while(1) {
		if (xQueueReceive(*q, &m, 1000)) {
//			LOGD("Recv msg, evt=%d", m.e);
			MwFsm(&m);
			// If event was MW_EV_SER_RX, free the buffer
			LsdRxBufFree();
		} else {
			// ERROR
			LOGD(".");
		}
	}
}

void MwWiFiStatPollTsk(void *pvParameters) {
	QueueHandle_t *q = (QueueHandle_t *)pvParameters;
	uint8_t con_stat, prev_con_stat;
	MwFsmMsg m;

	m.e = MW_EV_WIFI;

	prev_con_stat = sdk_wifi_station_get_connect_status();

	while(1) {
		con_stat = sdk_wifi_station_get_connect_status();
		if (con_stat != prev_con_stat) {
			// Send WiFi event to FSM
			m.d = (void*)((uint32_t)con_stat);
			xQueueSend(*q, &m, portMAX_DELAY);
			prev_con_stat = con_stat;
		}
		vTaskDelayMs(1000);
	}
}

