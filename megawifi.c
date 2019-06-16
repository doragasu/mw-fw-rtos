/************************************************************************//**
 * \brief MeGaWiFi application programming interface.
 *
 * \Author Jesus Alonso (idoragasu)
 * \date   2016
 ****************************************************************************/

// Espressif definitions
#include <espressif/esp_common.h>
#include <espressif/user_interface.h>
#include <esp/uart.h>
#include <esp/hwrand.h>

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
#include <sntp.h>
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

// TODO: Maybe this could be optimized by changing the data to define
// command ranges, instead of commands
/// Commands allowed while in IDLE state
const static uint8_t mwIdleCmds[] = {
	MW_CMD_VERSION, MW_CMD_ECHO, MW_CMD_AP_SCAN, MW_CMD_AP_CFG,
	MW_CMD_AP_CFG_GET, MW_CMD_IP_CFG, MW_CMD_IP_CFG_GET, MW_CMD_DEF_AP_CFG,
	MW_CMD_DEF_AP_CFG_GET, MW_CMD_AP_JOIN,
	MW_CMD_SNTP_CFG, /*MW_CMD_SNTP_CFG_GET,*/ MW_CMD_DATETIME, MW_CMD_DT_SET,
	MW_CMD_FLASH_WRITE, MW_CMD_FLASH_READ, MW_CMD_FLASH_ERASE, MW_CMD_FLASH_ID,
	MW_CMD_SYS_STAT, MW_CMD_DEF_CFG_SET, MW_CMD_HRNG_GET, MW_CMD_BSSID_GET,
	MW_CMD_GAMERTAG_SET, MW_CMD_GAMERTAG_GET
};

/// Commands allowed while in READY state
const static uint8_t mwReadyCmds[] = {
	MW_CMD_VERSION, MW_CMD_ECHO, MW_CMD_AP_CFG, MW_CMD_AP_CFG_GET,
	MW_CMD_IP_CURRENT, MW_CMD_IP_CFG, MW_CMD_IP_CFG_GET, MW_CMD_DEF_AP_CFG,
	MW_CMD_DEF_AP_CFG_GET, MW_CMD_AP_LEAVE,
	MW_CMD_TCP_CON, MW_CMD_TCP_BIND, MW_CMD_CLOSE,
	MW_CMD_UDP_SET, MW_CMD_SOCK_STAT, MW_CMD_PING,
	MW_CMD_SNTP_CFG, MW_CMD_SNTP_CFG_GET, MW_CMD_DATETIME, MW_CMD_DT_SET,
	MW_CMD_FLASH_WRITE, MW_CMD_FLASH_READ, MW_CMD_FLASH_ERASE, MW_CMD_FLASH_ID,
	MW_CMD_SYS_STAT, MW_CMD_DEF_CFG_SET, MW_CMD_HRNG_GET, MW_CMD_BSSID_GET,
	MW_CMD_GAMERTAG_SET, MW_CMD_GAMERTAG_GET
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
void PrintStationData(struct sdk_bss_info *bss) {
	AUTH_MODE atmp;
	// Character strings related to supported authentication modes
	const char *authStr[AUTH_MAX + 1] = {
		"OPEN", "WEP", "WPA_PSK", "WPA2_PSK", "WPA_WPA2_PSK", "???"
	};


	atmp = MIN(bss->authmode, AUTH_MAX);
	printf("%s, %s, ch=%d, str=%d\n",
			bss->ssid, authStr[atmp], bss->channel, bss->rssi);
}

/// Prints a list of found scanned stations
void MwBssListPrint(struct sdk_bss_info *bss) {
	// Traverse the bss list, ignoring first entry
	while (bss->next.stqe_next != NULL) {
		bss = bss->next.stqe_next;
		PrintStationData(bss);
	}
	dprintf("That's all!\n\n");
}

// Raises an event pending flag on requested channel
inline void MwFsmRaiseChEvent(int ch) {
	if ((ch < 1) || (ch >= LSD_MAX_CH)) return;

	s.ch_ev |= 1<<ch;
}

// Clears an event pending flag on requested channel
inline void MwFsmClearChEvent(int ch) {
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
			dprintf("ScanCompleteCb: malloc failed!\n");
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
		dprintf("Scan failed with error %d!\n", status);
		m.d = NULL;
		
	}
	xQueueSend(d.q, &m, portMAX_DELAY);
}

static int MwChannelCheck(int ch) {
	// Check channel is valid and not in use.
	if (ch >= LSD_MAX_CH) {
		dprintf("Requested unavailable channel %d\n", ch);
		return -1;
	}
	if (d.ss[ch - 1]) {
		dprintf("Requested already in-use channel %d\n", ch);
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
		dprintf("DNS lookup failure %d\n", err);
		if(*addr_info) {
			freeaddrinfo(*addr_info);
		}
		return -1;
	}
	// DNS lookup OK
	raddr = &((struct sockaddr_in *)(*addr_info)->ai_addr)->sin_addr;
	dprintf("DNS lookup succeeded. IP=%s\n", inet_ntoa(*raddr));

	return 0;
}

/// Establish a connection with a remote server
int MwFsmTcpCon(MwMsgInAddr* addr) {
	struct addrinfo *res;
	int err;
	int s;

	dprintf("Con. ch %d to %s:%s\n", addr->channel, addr->data,
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
		dprintf("... Failed to allocate socket.\n");
		freeaddrinfo(res);
		return -1;
	}

	dprintf("... allocated socket\n");

	if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
		lwip_close(s);
		freeaddrinfo(res);
		dprintf("... socket connect failed.\n");
		return -1;
	}

	dprintf("... connected sock %d on ch %d\n", s, addr->channel);
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

int MwFsmTcpBind(MwMsgBind *b) {
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
		dprintf("Could not create server socket!\n");
		return -1;
	}

	if (lwip_setsockopt(serv, SOL_SOCKET, SO_REUSEADDR, &optval,
				sizeof(int)) < 0) {
		lwip_close(serv);
		dprintf("setsockopt failed!\n");
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
		dprintf("Bind to port %d failed!\n", port);
		return -1;
	}

	// Listen for incoming connections
	if (lwip_listen(serv, MW_MAX_SOCK) < 0) {
		lwip_close(serv);
		dprintf("Listen to port %d failed!\n", port);
		return -1;
	}
	printf("Listening to port %d.\n", port);

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

/// Creates a socket and binds it to a port
int MwTcpBind(int ch, uint16_t port) {
	struct sockaddr_in saddr;	// Address of the server
	int s;
	int optval = 1;
	socklen_t addrlen = sizeof(struct sockaddr_in);
	int err;

	printf("Bind ch %d to port %d.\n", ch, port);

	err = MwChannelCheck(ch);
	if (err) {
		return err;
	}

	if ((s = lwip_socket(AF_INET, SOCK_STREAM, 0)) < 0)
		dprintf("Could not create listener!");
	dprintf("Created listener socket number %d.\n", s);
	// Allow address reuse
	if (lwip_setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval,
				sizeof(int)) < 0) dprintf("setsockopt() failed!");

	// Fill in address information
	memset((char*)&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_len = addrlen;
	saddr.sin_addr.s_addr = lwip_htonl(INADDR_ANY);
	saddr.sin_port = lwip_htons(port);

	// Bind to address
	if (lwip_bind(s, (struct sockaddr*)&saddr, sizeof(saddr)) < 0) {
		lwip_close(s);
		dprintf("bind() failed. Is TCP port in use?");
	}

	// Listen for incoming connections
	if (lwip_listen(s, 1) < 0) dprintf("listen() failed!");
	printf("Server socket created!\n");
	// Record socket number and mark channel as in use.
	d.sock[ch - 1] = s;
	d.chan[ch - 1] = TRUE;
	d.ss[ch - 1] = MW_SOCK_TCP_LISTEN;
	// Enable LSD channel
	LsdChEnable(ch);

	return s;
}

static int MwUdpSet(MwMsgInAddr* addr) {
	int err;
	int s;
	int idx;
	unsigned int local_port;
	struct addrinfo *raddr;
	struct sockaddr_in local;

	dprintf("UDP ch %d, port %s to addr %s:%s.\n", addr->channel,
			addr->src_port, addr->data, addr->dst_port);

	err = MwChannelCheck(addr->channel);
	if (err) {
		return err;
	}
	idx = addr->channel - 1;

	local_port = atoi(addr->src_port);
	if (!local_port) {
		dprintf("Invalid src_port\n");
		return -1;
	}

	// Create UDP socket
	if ((s = lwip_socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
		dprintf("Failed to create UDP socket\n");
		return -1;
	}

	// Get address of remote end
	err = MwDnsLookup(addr->data, addr->dst_port, &raddr);
	if (err) {
		lwip_close(s);
		return -1;
	}
	d.raddr[idx] = *((struct sockaddr_in*)raddr->ai_addr);
	freeaddrinfo(raddr);

	memset((char*)&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_port = lwip_htons(local_port);
	local.sin_addr.s_addr = lwip_htonl(INADDR_ANY);

	if (lwip_bind(s, (struct sockaddr*)&local, sizeof(local)) < 0) {
		dprintf("bind() failed. Is UDP port in use?\n");
		lwip_close(s);
		return -1;
	}

	printf("UDP socket bound\n");
	// Record socket number and mark channel as in use.
	d.sock[idx] = s;
	d.chan[s - LWIP_SOCKET_OFFSET] = addr->channel;
	d.ss[idx] = MW_SOCK_UDP_READY;
	// Enable LSD channel
	LsdChEnable(addr->channel);

	return s;
}

/// Close all opened sockets
void MwFsmCloseAll(void) {
	int i;

	for (i = 0; i < MW_MAX_SOCK; i++) {
		if (d.ss[i] > 0) {
			dprintf("Closing sock %d on ch %d\n", d.sock[i], i + 1);
			MwSockClose(i + 1);
			LsdChDisable(i + 1);
		}
	}
}

/// Check if a command is on a list
inline uint8_t MwCmdInList(uint8_t cmd, const uint8_t *list,
		                   uint8_t listLen) {
	uint8_t i;

	for (i = 0; (i < listLen) && (list[i] != cmd); i++);
	return !(i == listLen);
}

void PrintHex(uint8_t data[], uint16_t len) {
	uint16_t i;

	for (i = 0; i < len; i++) printf("%02x", data[i]);
}

/// Set default configuration.
static void MwSetDefaultCfg(void) {
	memset(&cfg, 0, sizeof(cfg));
	cfg.defaultAp = -1;
	// Copy the 3 default NTP servers
	*(1 + StrCpyDst(1 + StrCpyDst(1 + StrCpyDst(cfg.ntpPool, MW_SNTP_SERV_0),
		MW_SNTP_SERV_1), MW_SNTP_SERV_2)) = '\0';
	cfg.ntpUpDelay = 300;	// Update each 5 minutes
	// NOTE: Checksum is only computed before storing configuration
}

/// Saves configuration to non volatile flash
int MwNvCfgSave(void) {
	// Compute MD5 of the configuration data
	mbedtls_md5((const unsigned char*)&cfg, ((uint32_t)&cfg.md5) - 
			((uint32_t)&cfg), cfg.md5);
#ifdef _DEBUG_MSGS
	printf("Saved MD5: "); PrintHex(cfg.md5, 16); putchar('\n');
#endif
	// Erase configuration sector
//	if (!spiflash_erase_sector(MW_CFG_FLASH_ADDR)) {
	if (sdk_spi_flash_erase_sector(MW_CFG_FLASH_SECT) !=
			SPI_FLASH_RESULT_OK) {
		dprintf("Flash sector 0x%X erase failed!\n", MW_CFG_FLASH_SECT);
		return -1;
	}
	// Write configuration to flash
//	if (!spiflash_write(MW_CFG_FLASH_ADDR, (uint8_t*)&cfg, sizeof(MwNvCfg))) {
	if (sdk_spi_flash_write(MW_CFG_FLASH_ADDR, (uint32_t*)&cfg,
			sizeof(MwNvCfg)) != SPI_FLASH_RESULT_OK) {
		dprintf("Flash write addr 0x%X failed!\n", MW_CFG_FLASH_ADDR);
		return -1;
	}
	dprintf("Configuration saved to flash.\n");
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
		dprintf("Configuration loaded from flash.\n");
		return 0;
	}
#ifdef _DEBUG_MSGS
	printf("Loaded MD5:   "); PrintHex(cfg.md5, 16); putchar('\n');
	printf("Computed MD5: "); PrintHex(md5, 16); putchar('\n');
#endif

	// MD5 did not pass, load default configuration
	MwSetDefaultCfg();
	dprintf("Loaded default configuration.\n");
	return 1;
}

static void SetIpCfg(int slot) {
	if ((cfg.ip[slot].ip.addr) && (cfg.ip[slot].netmask.addr)
				&& (cfg.ip[slot].gw.addr)) {
		sdk_wifi_station_dhcpc_stop();
		if (sdk_wifi_set_ip_info(STATION_IF, cfg.ip + slot)) {
			dprintf("Static IP configuration set.\n");
			// Set DNS servers if available
			if (cfg.dns[slot][0].addr) {
				dns_setserver(0, cfg.dns[slot] + 0);
				if (cfg.dns[slot][1].addr) {
					dns_setserver(1, cfg.dns[slot] + 1);
				}
			}
		} else {
			dprintf("Failed setting static IP configuration.\n");
			sdk_wifi_station_dhcpc_start();
		}
	} else {
		dprintf("Setting DHCP IP configuration.\n");
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
	dprintf("AP JOIN!\n");
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
	dprintf("SPI Flash id: 0x%08X\n", sdk_spi_flash_get_id());
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
		dprintf("Could not create system queue!\n");
		return;
	};
  	// Create FSM task
	if (pdPASS != xTaskCreate(MwFsmTsk, "FSM", MW_FSM_STACK_LEN, &d.q,
			MW_FSM_PRIO, NULL)) {
		dprintf("Could not create FSM task!\n");
	}
	// Create task for receiving data from sockets
	if (pdPASS != xTaskCreate(MwFsmSockTsk, "SCK", MW_SOCK_STACK_LEN, &d.q,
			MW_SOCK_PRIO, NULL)) {
		dprintf("Could not create FSM task!\n");
	}
	// Create task for polling WiFi status
	if (pdPASS != xTaskCreate(MwWiFiStatPollTsk, "WPOL", MW_WPOLL_STACK_LEN,
			&d.q, MW_WPOLL_PRIO, NULL)) {
		dprintf("Could not create FSM task!\n");
	}
	// Initialize SNTP
	// TODO: Maybe this should be moved to the "READY" state
	for (i = 0, sntpSrv[0] = cfg.ntpPool; (i < SNTP_NUM_SERVERS_SUPPORTED) &&
			((sntpLen = strlen(sntpSrv[i])) > 0); i++) {
			sntpSrv[i + 1] = sntpSrv[i] + sntpLen + 1;
			dprintf("SNTP server: %s\n", sntpSrv[i]);
	}
	if (i) {
		dprintf("%d SNTP servers found.\n", i);
		tz.tz_minuteswest = cfg.timezone * 60;
		tz.tz_dsttime = cfg.dst;
		tz.tz_minuteswest = 0;
		sntp_initialize(&tz);
		dprintf("Setting update delay to %d seconds.\n", cfg.ntpUpDelay);
		/// \todo FIXME Setting sntp servers breaks tasking when using
		/// latest esp-open-rtos
		sntp_set_update_delay(cfg.ntpUpDelay * 1000);
//		if (sntp_set_servers(sntpSrv, i)) {
//			dprintf("Error setting SNTP servers!\n");
//			return;
//		}
	} else dprintf("No NTP servers found!\n");
	// Initialize LSD layer (will create receive task among other stuff).
	LsdInit(d.q);
	LsdChEnable(MW_CTRL_CH);
	// Send the init done message
	m.e = MW_EV_INIT_DONE;
	xQueueSend(d.q, &m, portMAX_DELAY);
}

/// Process command requests (coming from the serial line)
int MwFsmCmdProc(MwCmd *c, uint16_t totalLen) {
	MwCmd reply;
	uint16_t len = ByteSwapWord(c->datalen);
	time_t ts;	// For datetime replies
	uint16_t tmp, replen;
	
	// Sanity check: total Lengt - header length = data length
	if ((totalLen - MW_CMD_HEADLEN) != len) {
		dprintf("MwFsmCmdProc, ERROR: Length inconsistent\n");
		dprintf("totalLen=%d, dataLen=%d\n", totalLen, len);
		return MW_CMD_FMT_ERROR;
	}

	// parse command
	dprintf("CmdRequest: %d\n", ByteSwapWord(c->cmd));
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
			dprintf("SENDING ECHO!\n");
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
	    		dprintf("SCAN!\n");
	    		sdk_wifi_station_scan(NULL, (sdk_scan_done_cb_t)
						ScanCompleteCb);
			}
			break;

		case MW_CMD_AP_CFG:
			reply.datalen = 0;
			tmp = c->apCfg.cfgNum;
			if (tmp >= MW_NUM_AP_CFGS) {
				dprintf("Tried to set AP for cfg %d!\n", tmp);
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				// Copy configuration and save it to flash
				dprintf("Setting AP configuration %d...\n", tmp);
				strncpy(cfg.ap[tmp].ssid, c->apCfg.ssid, MW_SSID_MAXLEN);
				strncpy(cfg.ap[tmp].pass, c->apCfg.pass, MW_PASS_MAXLEN);
				dprintf("ssid: %s\npass: %s\n", cfg.ap[tmp].ssid,
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
				dprintf("Requested AP for cfg %d!\n", tmp);
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				reply.datalen = 0;
				replen = 0;
			} else {
				dprintf("Getting AP configuration %d...\n", tmp);
				reply.cmd = MW_CMD_OK;
				replen = sizeof(MwMsgApCfg);
				reply.datalen = ByteSwapWord(sizeof(MwMsgApCfg));
				reply.apCfg.cfgNum = c->apCfg.cfgNum;
				strncpy(reply.apCfg.ssid, cfg.ap[tmp].ssid, MW_SSID_MAXLEN);
				strncpy(reply.apCfg.pass, cfg.ap[tmp].pass, MW_PASS_MAXLEN);
				dprintf("ssid: %s\npass: %s\n", reply.apCfg.ssid,
						reply.apCfg.pass);
			} 
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_IP_CURRENT:
			reply.datalen = replen = 0;
			reply.cmd = MW_CMD_OK;
			dprintf("Getting current IP configuration...\n");
			replen = sizeof(MwMsgIpCfg);
			reply.datalen = ByteSwapWord(sizeof(MwMsgIpCfg));
			reply.ipCfg.cfgNum = 0;
			sdk_wifi_get_ip_info(STATION_IF, &reply.ipCfg.cfg);
			reply.ipCfg.dns1 = *dns_getserver(0);
			reply.ipCfg.dns2 = *dns_getserver(1);
			dprintf("IP: "); PrintIp(reply.ipCfg.cfg.ip.addr);
			dprintf("\nMASK: "); PrintIp(reply.ipCfg.cfg.netmask.addr);
			dprintf("\nGW: "); PrintIp(reply.ipCfg.cfg.gw.addr);
			dprintf("\nDNS1: "); PrintIp(reply.ipCfg.dns1.addr);
			dprintf("\nDNS2: "); PrintIp(reply.ipCfg.dns2.addr);
			dprintf("\n");
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_IP_CFG:
			tmp = (uint8_t)c->ipCfg.cfgNum;
			reply.datalen = 0;
			if (tmp >= MW_NUM_AP_CFGS) {
				dprintf("Tried to set IP for cfg %d!\n", tmp);
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				dprintf("Setting IP configuration %d...\n", tmp);
				cfg.ip[tmp] = c->ipCfg.cfg;
				cfg.dns[tmp][0] = c->ipCfg.dns1;
				cfg.dns[tmp][1] = c->ipCfg.dns2;
				dprintf("IP: "); PrintIp(cfg.ip[tmp].ip.addr);
				dprintf("\nMASK: "); PrintIp(cfg.ip[tmp].netmask.addr);
				dprintf("\nGW: "); PrintIp(cfg.ip[tmp].gw.addr);
				dprintf("\nDNS1: "); PrintIp(cfg.dns[tmp][0].addr);
				dprintf("\nDNS2: "); PrintIp(cfg.dns[tmp][1].addr);
				dprintf("\n");
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
				dprintf("Requested IP for cfg %d!\n", tmp);
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				dprintf("Getting IP configuration %d...\n", tmp);
				reply.cmd = MW_CMD_OK;
				replen = sizeof(MwMsgIpCfg);
				reply.datalen = ByteSwapWord(sizeof(MwMsgIpCfg));
				reply.ipCfg.cfgNum = c->ipCfg.cfgNum;
				reply.ipCfg.cfg = cfg.ip[tmp];
				reply.ipCfg.dns1 = cfg.dns[tmp][0];
				reply.ipCfg.dns2 = cfg.dns[tmp][1];
				dprintf("IP: "); PrintIp(cfg.ip[tmp].ip.addr);
				dprintf("\nMASK: "); PrintIp(cfg.ip[tmp].netmask.addr);
				dprintf("\nGW: "); PrintIp(cfg.ip[tmp].gw.addr);
				dprintf("\nDNS1: "); PrintIp(cfg.dns[tmp][0].addr);
				dprintf("\nDNS2: "); PrintIp(cfg.dns[tmp][1].addr);
				dprintf("\n");
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_DEF_AP_CFG:
			reply.datalen = 0;
			tmp = c->data[0];
			if (tmp < MW_NUM_AP_CFGS) {
				cfg.defaultAp = tmp;
				dprintf("Set default AP: %d\n", cfg.defaultAp);
				if (MwNvCfgSave() != 0) {
					reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				} else {
					dprintf("Set default AP: %d\n", cfg.defaultAp);
					reply.cmd = MW_CMD_OK;
				}
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_DEF_AP_CFG_GET:
			reply.datalen = ByteSwapWord(1);
			reply.cmd = MW_CMD_OK;
			reply.data[0] = cfg.defaultAp;
			dprintf("Sending default AP: %d\n", cfg.defaultAp);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + 1, 0);
			break;

		case MW_CMD_AP_JOIN:
			// Start connecting to AP and jump to AP_JOIN state
			reply.datalen = 0;
			if ((c->data[0] >= MW_NUM_AP_CFGS) ||
					!(cfg.ap[c->data[0]].ssid[0])) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				MwApJoin(c->data[0]);
				reply.cmd = MW_CMD_OK;
				// TODO: Save configuration to update default AP?
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_AP_LEAVE:	// Leave access point
			dprintf("Disconnecting from AP\n");
			// Close all opened sockets
			MwFsmCloseAll();
			// Disconnect and switch to IDLE state
			sdk_wifi_station_disconnect();
			d.s.sys_stat = MW_ST_IDLE;
			dprintf("IDLE!\n");
			reply.cmd = MW_OK;
			reply.datalen = 0;
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_TCP_CON:
			dprintf("TRYING TO CONNECT TCP SOCKET...\n");
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
				dprintf("Closing socket %d from channel %d\n",
						d.sock[c->data[0] - 1], c->data[0]);
				MwSockClose(c->data[0]);
				LsdChDisable(c->data[0]);
				reply.cmd = MW_CMD_OK;
			} else {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				dprintf("Requested disconnect of not opened channel %d.\n",
						c->data[0]);
			}
			reply.cmd = ByteSwapWord(reply.cmd);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_UDP_SET:
			dprintf("Configuring UDP socket...\n");
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
				dprintf("Requested unavailable channel!\n");
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_PING:
			dprintf("PING unimplemented\n");
			break;

		case MW_CMD_SNTP_CFG:
			reply.datalen = 0;
			cfg.ntpUpDelay = ByteSwapWord(c->sntpCfg.upDelay);
			cfg.ntpPoolLen = len - 4;
			if ((cfg.ntpPoolLen > MW_NTP_POOL_MAXLEN) ||
					(c->sntpCfg.tz < -11) || (c->sntpCfg.tz > 13)) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				cfg.timezone = c->sntpCfg.tz;
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
			dprintf("SNTP_CFG_GET unimplemented\n");
			break;

		case MW_CMD_DATETIME:
			reply.cmd = MW_CMD_OK;
			reply.datetime.dtBin[0] = 0;
			reply.datetime.dtBin[1] = ByteSwapDWord(ts);
			ts = time(NULL);
			strcpy(reply.datetime.dtStr, ctime(&ts));
			dprintf("sending datetime %s\n", reply.datetime.dtStr);
			tmp = 2*sizeof(uint32_t) + strlen(reply.datetime.dtStr);
			reply.datalen = ByteSwapWord(tmp);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + tmp, 0);
			break;

		case MW_CMD_DT_SET:
			dprintf("DT_SET unimplemented\n");
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
				dprintf("Address/length combination overflows!\n");
			} else if (sdk_spi_flash_write(c->flData.addr,
						(uint32_t*)c->flData.data, len - sizeof(uint32_t)) !=
					SPI_FLASH_RESULT_OK) {
//			} else if (!spiflash_write(c->flData.addr, (uint8_t*)c->flData.data,
//					len - sizeof(uint32_t))) {
				dprintf("Write to flash failed!\n");
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
				dprintf("Invalid address/length combination.\n");
			// Perform read and check result
			} else if (sdk_spi_flash_read(c->flRange.addr,
						(uint32_t*)reply.data, c->flRange.len) !=
					SPI_FLASH_RESULT_OK) {
//			} else if (!spiflash_read(c->flRange.addr,
//						(uint8_t*)reply.data, c->flRange.len)) {
				reply.datalen = 0;
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				dprintf("Flash read failed!\n");
			} else {
				reply.datalen = ByteSwapWord(c->flRange.len);
				reply.cmd = MW_CMD_OK;
				dprintf("Flash read OK!\n");
			}
			LsdSend((uint8_t*)&reply, c->flRange.len + MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_FLASH_ERASE:
			reply.datalen = 0;
			// Check for sector overflow
			c->flSect = ByteSwapWord(c->flSect) + MW_FLASH_USER_BASE_SECT;
			if (c->flSect < MW_FLASH_USER_BASE_SECT) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				dprintf("Wrong sector number.\n");
			} else if (sdk_spi_flash_erase_sector(c->flSect) !=
					SPI_FLASH_RESULT_OK) {
//			} else if (!spiflash_erase_sector((c->flSect)<<12)) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				dprintf("Sector erase failed!\n");
			} else {
				reply.cmd = MW_CMD_OK;
				dprintf("Sector erase OK!\n");
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
			dprintf("%02X %02X %02X %02X\n", reply.data[0], reply.data[1],
					reply.data[2], reply.data[3]);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + sizeof(MwMsgSysStat),
				0);
			break;

		case MW_CMD_DEF_CFG_SET:
			reply.datalen = 0;
			// Check lengt and magic value
			if ((len != 4) || (c->dwData[0] !=
						ByteSwapDWord(MW_FACT_RESET_MAGIC))) {
				dprintf("Wrong DEF_CFG_SET command invocation!\n");
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else if (sdk_spi_flash_erase_sector(MW_CFG_FLASH_SECT) !=
					SPI_FLASH_RESULT_OK) {
//			} else if (!spiflash_erase_sector(MW_CFG_FLASH_ADDR)) {
				dprintf("Config flash sector erase failed!\n");
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				dprintf("Configuration set to default.\n");
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
			dprintf("Got BSSID(%d) %02X:%02X:%02X:%02X:%02X:%02X\n",
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

		default:
			dprintf("UNKNOWN REQUEST!\n");
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
		remote.sin_addr.s_addr = *((int32_t*)data);
		remote.sin_port = *((int16_t*)(data + 4));
		remote.sin_family = AF_INET;
		remote.sin_len = sizeof(struct sockaddr_in);
		memset(remote.sin_zero, 0, sizeof(remote.sin_zero));
		sent = lwip_sendto(s, data + 6, len - 6, 0, (struct sockaddr*)
				&remote, sizeof(struct sockaddr_in));
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
			dprintf("WIFI_EVENT (not parsed)\n");
			break;

		case MW_EV_SCAN:		///< WiFi scan complete.
			dprintf("EV_SCAN (not parsed)\n");
			break;

		case MW_EV_SER_RX:		///< Data reception from serial line.
			dprintf("Serial recvd %d bytes.\n", b->len);
			// If using channel 0, process command. Else forward message
			// to the appropiate socket.
			if (MW_CTRL_CH == b->ch) {
				// Check command is allowed on READY state
				if (MwCmdInList(b->cmd.cmd>>8, mwReadyCmds,
							sizeof(mwReadyCmds))) {
					MwFsmCmdProc((MwCmd*)b, b->len);
				} else {
					dprintf("Command %d not allowed on READY state\n",
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
						dprintf("DEB: CH %d socket send error!\n", b->ch);
						// TODO throw error event?
						rep = (MwCmd*)msg->d;
						rep->datalen = 0;
						rep->cmd = ByteSwapWord(MW_CMD_ERROR);
						LsdSend((uint8_t*)rep, MW_CMD_HEADLEN, 0);
					}
				} else {
					dprintf("Requested to forward data on wrong channel: %d\n",
							b->ch);
				}
			}
			break;

		case MW_EV_TCP_CON:		///< TCP connection established.
			dprintf("TCP_CON (not parsed)\n");
			break;

		case MW_EV_TCP_RECV:	///< Data received from TCP connection.
			// Forward data to the appropiate channel of serial line
//			LsdSend(, , d.ss[b->ch - 1]);
			dprintf("TCP_RECV (not parsed)\n");
			break;

		case MW_EV_TCP_SENT:	///< Data sent to peer on TCP connection.
			dprintf("TCP_SENT (not parsed)\n");
			break;

		case MW_EV_UDP_RECV:	///< Data received from UDP connection.
			dprintf("UDP_RECV (not parsed)\n");
			break;

		case MW_EV_CON_DISC:	///< TCP disconnection.
			dprintf("CON_DISC (not parsed)\n");
			break;

		case MW_EV_CON_ERR:		///< TCP connection error.
			dprintf("CON_ERR (not parsed)\n");
			break;

		default:
			dprintf("MwFsmReady: UNKNOKWN EVENT!");
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
				dprintf("INIT DONE!\n");
				// If there's a valid AP configuration, try to join it and
				// jump to the AP_JOIN state. Else jump to IDLE state.
//				if ((cfg.defaultAp >= 0) && (cfg.defaultAp < MW_NUM_AP_CFGS)) {
//					MwApJoin(cfg.defaultAp);
//					// TODO: Maybe we should set an AP join timeout.
//				} else {
//					dprintf("No default AP found.\nIDLE!\n");
					d.s.sys_stat = MW_ST_IDLE;
//				}
			}
			break;

		case MW_ST_AP_JOIN:
			if (MW_EV_WIFI == msg->e) {
				dprintf("WiFi event: %d\n", (uint32_t)msg->d);
				switch ((uint32_t)msg->d) {
					case STATION_GOT_IP:
						// Connected!
						dprintf("READY!\n");
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
						dprintf("Could not connect to AP!\nIDLE!\n");
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
					dprintf("%02X %02X %02X %02X\n", rep->data[0],
							rep->data[1], rep->data[2], rep->data[3]);
					LsdSend((uint8_t*)rep, sizeof(MwMsgSysStat) + MW_CMD_HEADLEN,
							0);
				} else {
					dprintf("Command %d not allowed on AP_JOIN state\n",
							b->cmd.cmd>>8);
				}
			}
			break;

		case MW_ST_IDLE:
			// IDLE state is abandoned once connected to an AP
			if (MW_EV_SER_RX == msg->e) {
				// Parse commands on channel 0 only
				dprintf("Serial recvd %d bytes.\n", b->len);
				if (MW_CTRL_CH == b->ch) {
					// Check command is allowed on IDLE state
					if (MwCmdInList(b->cmd.cmd>>8, mwIdleCmds,
								sizeof(mwIdleCmds))) {
						MwFsmCmdProc((MwCmd*)b, b->len);
					} else {
						dprintf("Command %d not allowed on IDLE state\n",
								b->cmd.cmd>>8);
						// TODO: Throw error event?
						rep = (MwCmd*)msg->d;
						rep->datalen = 0;
						rep->cmd = ByteSwapWord(MW_CMD_ERROR);
						LsdSend((uint8_t*)rep, MW_CMD_HEADLEN, 0);
					}
				} else {
					dprintf("IDLE received data on non ctrl channel!\n");
				}
			}
			break;

		case MW_ST_SCAN:
			// Ignore events until we receive the scan result
			if (MW_EV_SCAN == msg->e) {
				// We receive the station data reply ready to be sent
				dprintf("Sending station data\n");
				rep = (MwCmd*)msg->d;
				LsdSend((uint8_t*)rep, ByteSwapWord(rep->datalen) +
						MW_CMD_HEADLEN, 0);
				free(rep);
				d.s.sys_stat = MW_ST_IDLE;
				dprintf("IDLE!\n");
			}
			break;

		case MW_ST_READY:
			// Module ready. Here we will have most of the activity
			MwFsmReady(msg);
			// NOTE: LSD channel 0 is treated as a special control channel.
			// All other channels are socket-bridged.
			
			break;

		case MW_ST_TRANSPARENT:
			dprintf("TRANSPARENT!\n");
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
		dprintf("Accept failed for socket %d, channel %d\n", sock, ch);
		return -1;
	}
	// Connection accepted, add to the FD set
	dprintf("Socket %d, channel %d: established connection from %s.\n",
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
				dprintf("Discarding UDP packet from unknown addr\n");
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
		dprintf(".");
		if ((retval = select(d.fdMax + 1, &readset, NULL, NULL, &tv)) < 0) {
			// Error.
			dprintf("select() completed with error!\n");
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
					dprintf("Rx: sock=%d, ch=%d\n", i, ch);
					if ((recvd = MwRecv(ch, (char*)buf, LSD_MAX_LEN)) < 0) {
						// Error!
						MwSockClose(ch);
						LsdChDisable(ch);
						dprintf("Error %d receiving from socket!\n", recvd);
					} else if (0 == recvd) {
						// Socket closed
						// TODO: A listen on a socket closed, should trigger
						// a 0-byte reception, for the client to be able to
						// check server state and close the connection.
						dprintf("Received 0!\n");
						MwSockClose(ch);
						dprintf("Socket closed!\n");
						MwFsmRaiseChEvent(ch);
						// Send a 0-byte frame for the receiver to wake up and
						// notice the socket close
						LsdSend(NULL, 0, ch);
						LsdChDisable(ch);
					} else {
						dprintf("%02X %02X %02X %02X: ", buf[0], buf[1],
								buf[2], buf[3]);
						dprintf("WF->MD %d bytes\n", recvd);
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
//			dprintf("Recv msg, evt=%d\n", m.e);
			MwFsm(&m);
			// If event was MW_EV_SER_RX, free the buffer
			LsdRxBufFree();
		} else {
			// ERROR
			dprintf(".");
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

