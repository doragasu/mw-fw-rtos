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
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/timers.h>

// lwIP
#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>
#include <lwip/ip_addr.h>
#include <lwip/apps/sntp.h>

// mbedtls
#include <mbedtls/md5.h>

// Time keeping
#include <time.h>

// WiFi
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <esp_sleep.h>

// Flash manipulation
#include <spi_flash.h>

#include "megawifi.h"
#include "net_util.h"
#include "lsd.h"
#include "util.h"
#include "led.h"
#include "http.h"

#define SPI_FLASH_BASE	0x40200000
#define SPI_FLASH_ADDR(flash_addr)	(SPI_FLASH_BASE + (flash_addr))

#define HTTP_CERT_HASH_ADDR	SPI_FLASH_ADDR(MW_CERT_FLASH_ADDR)

#define HTTP_CERT_LEN_ADDR	SPI_FLASH_ADDR(MW_CERT_FLASH_ADDR + \
		sizeof(uint32_t))

#define HTTP_CERT_ADDR		SPI_FLASH_ADDR(MW_CERT_FLASH_ADDR + \
		(2 * sizeof(uint32_t)))

/// Flash sector number where the configuration is stored
#define MW_CFG_FLASH_SECT	(MW_CFG_FLASH_ADDR>>12)

/// Maximum number of reassociation attempts
#define MW_REASSOC_MAX		3

/// Sleep timer period in ms
#define MW_SLEEP_TIMER_MS	30000

/** \addtogroup MwApi MwFdOps FD set operations (add/remove)
 *  \{ */
typedef enum {
	MW_FD_NONE = 0,		///< Do nothing
	MW_FD_ADD,			///< Add socket to the FD set
	MW_FD_REM			///< Remove socket from the FD set
} MwFdOps;

/// Status of the HTTP command
enum http_stat {
	MW_HTTP_ST_IDLE = 0,
	MW_HTTP_ST_OPEN_CONTENT_WAIT,
	MW_HTTP_ST_FINISH_WAIT,
	MW_HTTP_ST_FINISH_CONTENT_WAIT,
	MW_HTTP_ST_CERT_SET,
	MW_HTTP_ST_ERROR,
	MW_HTTP_ST_STAT_MAX
};

struct http_data {
	/// HTTP client handle
	esp_http_client_handle_t h;
	/// HTTP machine state
	enum http_stat s;
	/// Remaining bytes to read/write
	int remaining;
};

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
	(1<<(MW_CMD_LOG - 32))            | (1<<(MW_CMD_FACTORY_RESET - 32))|
	(1<<(MW_CMD_SLEEP - 32))          | (1<<(MW_CMD_HTTP_URL_SET - 32)) |
	(1<<(MW_CMD_HTTP_METHOD_SET - 32))| (1<<(MW_CMD_HTTP_CERT_QUERY - 32))|
	(1<<(MW_CMD_HTTP_CERT_SET - 32))  | (1<<(MW_CMD_HTTP_HDR_ADD - 32)) |
	(1<<(MW_CMD_HTTP_HDR_DEL - 32))   | (1<<(MW_CMD_HTTP_CLEANUP - 32))
};

/// Commands allowed while in READY state
const static uint32_t readyCmdMask[2] = {
	(1<<MW_CMD_VERSION)              | (1<<MW_CMD_ECHO)                  |
	(1<<MW_CMD_AP_CFG)               | (1<<MW_CMD_AP_CFG_GET)            |
	(1<<MW_CMD_IP_CURRENT)           | (1<<MW_CMD_IP_CFG)                |
	(1<<MW_CMD_IP_CFG_GET)           | (1<<MW_CMD_DEF_AP_CFG)            |
	(1<<MW_CMD_DEF_AP_CFG_GET)       | (1<<MW_CMD_AP_LEAVE)              |
	(1<<MW_CMD_TCP_CON)              | (1<<MW_CMD_TCP_BIND)              |
	(1<<MW_CMD_CLOSE)                | (1<<MW_CMD_UDP_SET)               |
	(1<<MW_CMD_SOCK_STAT)            | (1<<MW_CMD_PING)                  |
	(1<<MW_CMD_SNTP_CFG)             | (1<<MW_CMD_SNTP_CFG_GET)          |
	(1<<MW_CMD_DATETIME)             | (1<<MW_CMD_DT_SET)                |
	(1<<MW_CMD_FLASH_WRITE)          | (1<<MW_CMD_FLASH_READ)            |
	(1<<MW_CMD_FLASH_ERASE)          | (1<<MW_CMD_FLASH_ID)              |
	(1<<MW_CMD_SYS_STAT)             | (1<<MW_CMD_DEF_CFG_SET),
	(1<<(MW_CMD_HRNG_GET - 32))      | (1<<(MW_CMD_BSSID_GET - 32))      |
	(1<<(MW_CMD_GAMERTAG_SET - 32))  | (1<<(MW_CMD_GAMERTAG_GET - 32))   |
	(1<<(MW_CMD_LOG - 32))           | (1<<(MW_CMD_SLEEP - 32))          |
	(1<<(MW_CMD_HTTP_URL_SET - 32))  | (1<<(MW_CMD_HTTP_METHOD_SET - 32))|
	(1<<(MW_CMD_HTTP_CERT_QUERY - 32))|(1<<(MW_CMD_HTTP_CERT_SET - 32))  |
	(1<<(MW_CMD_HTTP_HDR_ADD - 32))  | (1<<(MW_CMD_HTTP_HDR_DEL - 32))   |
	(1<<(MW_CMD_HTTP_OPEN - 32))     | (1<<(MW_CMD_HTTP_FINISH - 32))    |
	(1<<(MW_CMD_HTTP_CLEANUP - 32))
};

/*
 * PRIVATE PROTOTYPES
 */
static void MwFsm(MwFsmMsg *msg);
void MwFsmTsk(void *pvParameters);
void MwFsmSockTsk(void *pvParameters);

/** \addtogroup MwApi MwNvCfg Configuration saved to non-volatile memory.
 *  \{ */
typedef struct {
	/// Access point configuration (SSID, password).
	ApCfg ap[MW_NUM_AP_CFGS];
	/// IPv4 (IP addr, mask, gateway). If IP=0.0.0.0, use DHCP.
	tcpip_adapter_ip_info_t ip[MW_NUM_AP_CFGS];
	/// DNS configuration (when not using DHCP). 2 servers per AP config.
	ip_addr_t dns[MW_NUM_AP_CFGS][MW_NUM_DNS_SERVERS];
	/// Pool length for SNTP configuration
	uint16_t ntpPoolLen;
	/// SNTP configuration. The TZ string and up to 3 servers are
	/// concatenated and null separated. Two NULL characters mark
	/// the end of the pool
	char ntpPool[MW_NTP_POOL_MAXLEN];
	/// Index of the configuration used on last connection (-1 for none).
	char defaultAp;
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
	/// FSM queue for event reception
	QueueHandle_t q;
	/// Sleep inactivity timer
	TimerHandle_t tim;
	/// File descriptor set for select()
	fd_set fds;
	/// Maximum socket identifier value
	int fdMax;
	/// Address of the remote end, used in UDP sockets
	struct sockaddr_in raddr[MW_MAX_SOCK];
	/// Association retries
	uint8_t n_reassoc;
	/// HTTP machine data
	struct http_data http;
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

static void deep_sleep(void)
{
	LOGI("Entering deep sleep");
	esp_deep_sleep(0);
	// As it takes a little for the module to enter deep
	// sleep, stay here for a while
	vTaskDelayMs(60000);
}

void sleep_timer_cb(TimerHandle_t xTimer)
{
	UNUSED_PARAM(xTimer);

	deep_sleep();
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
	QueueHandle_t q = (QueueHandle_t)ctx;
	MwFsmMsg msg;

	if (!ctx || !event) {
		LOGE("missing ctx or event");
		return ESP_ERR_INVALID_ARG;
	}

	// Forward event to sysfsm
	// TODO When is event freed? It might be a problem if freed just
	// after exiting this function.
	msg.e = MW_EV_WIFI;
	msg.d = event;

	xQueueSend(q, &msg, portMAX_DELAY);
	return ESP_OK;
}

#define http_err_set(...)	do {	\
	LsdChDisable(MW_HTTP_CH);	\
	LOGE(__VA_ARGS__);		\
	d.http.s = MW_HTTP_ST_ERROR;	\
} while(0)

static void http_data_recv(void)
{
	int readed;

	if (d.http.s != MW_HTTP_ST_FINISH_CONTENT_WAIT) {
		http_err_set("ignoring unexpected HTTP data on state %d",
				d.http.s);
		return;
	}

	while (d.http.remaining > 0) {
		readed = esp_http_client_read(d.http.h, (char*)buf,
				MW_MSG_MAX_BUFLEN);
		if (-1 == readed) {
			http_err_set("HTTP read error, %d remaining",
					d.http.remaining);
			return;
		}
		LsdSend(buf, readed, MW_HTTP_CH);
		d.http.remaining -= readed;
	}

	if (d.http.remaining < 0) {
		LOGW("HTTP ignoring extra %d bytes", -d.http.remaining);
	}
	LOGD("HTTP request complete");
	d.http.s = MW_HTTP_ST_IDLE;
	LsdChDisable(MW_HTTP_CH);
}

/// Prints a list of found scanned stations
static void ap_print(const wifi_ap_record_t *ap, int n_aps) {
	wifi_auth_mode_t auth;
	const char *auth_str[WIFI_AUTH_MAX + 1] = {
		"OPEN", "WEP", "WPA_PSK", "WPA2_PSK", "WPA_WPA2_PSK",
		"WPA_WPA2_ENTERPRISE", "UNKNOWN"
	};
	int i;

	for (i = 0; i < n_aps; i++) {
		auth = MIN(ap[i].authmode, WIFI_AUTH_MAX);
		LOGI("%s, %s, ch=%d, str=%d", ap[i].ssid, auth_str[auth],
				ap[i].primary, ap[i].rssi);
	}

	LOGI("That's all!");
}

// Raises an event pending flag on requested channel
static void MwFsmRaiseChEvent(int ch) {
	if ((ch < 1) || (ch >= LSD_MAX_CH)) return;

	d.s.ch_ev |= 1<<ch;
}

// Clears an event pending flag on requested channel
static void MwFsmClearChEvent(int ch) {
	if ((ch < 1) || (ch >= LSD_MAX_CH)) return;

	d.s.ch_ev &= ~(1<<ch);
}

static int build_scan_reply(const wifi_ap_record_t *ap, uint8_t n_aps,
		uint8_t *data)
{
	int i;
	uint8_t *pos = data;
	uint8_t ssid_len;
	int data_len = 1;

	// Skip number of aps
	pos = data + 1;
	for (i = 0; i < n_aps; i++) {
		ssid_len = strnlen((char*)ap[i].ssid, 32);
		// Check if next entry fits along with end
		if ((ssid_len + 5) >= LSD_MAX_LEN) {
			LOGI("discarding %d entries", n_aps - i);
			break;
		}
		pos[0] = ap[i].authmode;
		pos[1] = ap[i].primary;
		pos[2] = ap[i].rssi;
		pos[3] = ssid_len;
		memcpy(pos + 4, ap[i].ssid, data[3]);
		pos += 4 + ssid_len;
		data_len += 4 + ssid_len;
	}
	// Write number of APs in report
	*data = i;

	return data_len;
}

static int wifi_scan(uint8_t *data)
{
	int length = -1;
	uint16_t n_aps = 0;
	wifi_ap_record_t *ap = NULL;
	wifi_scan_config_t scan_cfg = {};
	esp_err_t err;

	err = esp_wifi_start();
	if (ESP_OK != err) {
		LOGE("wifi start failed: %s!", esp_err_to_name(err));
		goto out;
	}
	err = esp_wifi_scan_start(&scan_cfg, true);
	if (ESP_OK != err) {
		LOGE("scan failed: %s!", esp_err_to_name(err));
		goto out;
	}

	esp_wifi_scan_get_ap_num(&n_aps);
	LOGI("found %d APs", n_aps);
	ap = calloc(n_aps, sizeof(wifi_ap_record_t));
 	if (!ap) {
		LOGE("out of memory!");
		goto out;
	}
	err = esp_wifi_scan_get_ap_records(&n_aps, ap);
	if (ESP_OK != err) {
		LOGE("get AP records failed: %s", esp_err_to_name(err));
		goto out;
	}
	ap_print(ap, n_aps);
	length = build_scan_reply(ap, n_aps, data);

out:
	if (ap) {
		free(ap);
	}
	esp_wifi_stop();

	return length;
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
	err = net_dns_lookup(addr->data, addr->dst_port, &res);
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

	if(lwip_connect(s, res->ai_addr, res->ai_addrlen) != 0) {
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

		err = net_dns_lookup(addr->data, addr->dst_port, &raddr);
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
	// Copy the default timezone and NTP servers
	*(1 + StrCpyDst(1 + StrCpyDst(1 + StrCpyDst(1 + StrCpyDst(cfg.ntpPool,
		MW_TZ_DEF), MW_SNTP_SERV_0), MW_SNTP_SERV_1), MW_SNTP_SERV_2)) =
		'\0';
	cfg.ntpPoolLen = sizeof(MW_TZ_DEF) + sizeof(MW_SNTP_SERV_0) +
		sizeof(MW_SNTP_SERV_1) + sizeof(MW_SNTP_SERV_2) + 1;
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
	if (spi_flash_erase_sector(MW_CFG_FLASH_SECT) != ESP_OK) {
		LOGE("Flash sector 0x%X erase failed!", MW_CFG_FLASH_SECT);
		return -1;
	}
	// Write configuration to flash
	if (spi_flash_write(MW_CFG_FLASH_ADDR, (uint32_t*)&cfg,
			sizeof(MwNvCfg)) != ESP_OK) {
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
	spi_flash_read(MW_CFG_FLASH_ADDR, (uint32_t*)&cfg, sizeof(MwNvCfg));
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
	LOGI("Computed MD5: %s", md5_str);
#endif

	// MD5 did not pass, load default configuration
	MwSetDefaultCfg();
	LOGI("Loaded default configuration.");
	return 1;
}

static void SetIpCfg(int slot) {
	esp_err_t err;

	if ((cfg.ip[slot].ip.addr) && (cfg.ip[slot].netmask.addr)
				&& (cfg.ip[slot].gw.addr)) {
		tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA);
		err = tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &cfg.ip[slot]);
		if (!err) {
			LOGI("static IP configuration %d set", slot);
			// Set DNS servers if available
			if (cfg.dns[slot][0].addr) {
				dns_setserver(0, cfg.dns[slot] + 0);
				if (cfg.dns[slot][1].addr) {
					dns_setserver(1, cfg.dns[slot] + 1);
				}
			}
		} else {
			LOGE("failed setting static IP configuration %d: %s",
					slot, esp_err_to_name(err));
			tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA);
		}
	} else {
		LOGI("Setting DHCP IP configuration.");
		tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA);
	}
}

void MwApJoin(uint8_t n) {
	wifi_config_t if_cfg = {};

	SetIpCfg(n);
	strncpy((char*)if_cfg.sta.ssid, cfg.ap[n].ssid, MW_SSID_MAXLEN);
	strncpy((char*)if_cfg.sta.password, cfg.ap[n].pass, MW_PASS_MAXLEN);
	esp_wifi_set_config(ESP_IF_WIFI_STA, &if_cfg);
	esp_wifi_start();
	LOGI("AP ASSOC %d", n);
	d.s.sys_stat = MW_ST_AP_JOIN;
	d.n_reassoc = 0;
}

void MwSysStatFill(MwCmd *rep) {
//	Warning: dt_ok not supported yet
	rep->datalen = ByteSwapWord(sizeof(MwMsgSysStat));
	rep->sysStat.st_flags = d.s.st_flags;
	LOGD("Stat flags: 0x%04X, len: %d", d.s.st_flags, sizeof(MwMsgSysStat));
}

static int tokens_get(const char *in, const char *token[],
		int token_max, uint16_t *len_total)
{
	int i;
	size_t len;

	token[0] = in;
	for (i = 0; i < (token_max - 1) && *token[i]; i++) {
		len = strlen(token[i]);
		token[i + 1] = token[i] + len + 1;
	}

	if (*token[i]) {
		i++;
	}

	if (len_total) {
		// ending address minus initial address plus an extra null
		// termination plus 1 (because if something uses bytes from
		// x pos to y pos, length is (x - y + 1).
		// note: this assumes that last token always has an extra
		// null termination.
		*len_total = ((token[i - 1] + strlen(token[i - 1]) + 2) - in);
	}

	return i;
}

static void sntp_set_config(void)
{
	const char *token[4];
	int num_tokens;

	num_tokens = tokens_get(cfg.ntpPool, token, 4, NULL);

	setenv("TZ", token[0], 1);
	tzset();

	for (int i = 1; i < num_tokens; i++) {
		sntp_setservername(i - 1, (char*)token[i]);
		LOGI("SNTP server: %s", token[i]);
	}
}

/************************************************************************//**
 * Module initialization. Must be called in user_init() context.
 ****************************************************************************/
int MwInit(void) {
	MwFsmMsg m;
	int i;
	uint8_t tmp;
	esp_err_t err;
	wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();

	if (sizeof(MwNvCfg) > MW_FLASH_SECT_LEN) {
		LOGE("STOP: config length too big (%zd)", sizeof(MwNvCfg));
		deep_sleep();
	}
	LOGI("Configured SPI length: %zd", spi_flash_get_chip_size());
	// Load configuration from flash
	MwCfgLoad();
	// Set default values for global variables
	d.s.st_flags = 0;
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
		LOGE("could not create system queue!");
		goto err;
	};

	tcpip_adapter_init();
	err = esp_event_loop_init(event_handler, d.q);
	if (err) {
		LOGE("failed to initialize event loop: %d", err);
		goto err;
	}
	// Configure and start WiFi interface
	err = esp_wifi_init(&wifi_cfg);
	if (err) {
		LOGE("wifi init failed: %s", esp_err_to_name(err));
		goto err;
	}
	esp_wifi_set_storage(WIFI_STORAGE_RAM);
	esp_wifi_set_mode(WIFI_MODE_STA);

  	// Create FSM task
	if (pdPASS != xTaskCreate(MwFsmTsk, "FSM", MW_FSM_STACK_LEN, &d.q,
			MW_FSM_PRIO, NULL)) {
		LOGE("Could not create Fsm task!");
		goto err;
	}
	// Create task for receiving data from sockets
	if (pdPASS != xTaskCreate(MwFsmSockTsk, "SCK", MW_SOCK_STACK_LEN, &d.q,
			MW_SOCK_PRIO, NULL)) {
		LOGE("Could not create FsmSock task!");
		goto err;
	}
	// Initialize SNTP
	// TODO: Maybe this should be moved to the "READY" state
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_set_config();
	sntp_init();
	// Initialize LSD layer (will create receive task among other stuff).
	LsdInit(d.q);
	LsdChEnable(MW_CTRL_CH);
	// Send the init done message
	m.e = MW_EV_INIT_DONE;
	xQueueSend(d.q, &m, portMAX_DELAY);

	// TODO Start the one-shot inactivity sleep timer
	d.tim = xTimerCreate("SLEEP", MW_SLEEP_TIMER_MS / portTICK_PERIOD_MS,
			pdFALSE, (void*) 0, sleep_timer_cb);
	xTimerStart(d.tim, MW_SLEEP_TIMER_MS / portTICK_PERIOD_MS);

	return 0;

err:
	LOGE("fatal error during initialization");
	return 1;
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

static void reply_set_ok_empty(MwCmd *reply)
{
	reply->datalen = 0;
	reply->cmd = MW_CMD_OK;
}

static void rand_fill(uint8_t *buf, uint16_t len)
{
	uint32_t *data = (uint32_t*)buf;
	uint8_t last[4];
	uint16_t dwords = len>>2;
	uint16_t i;

	// Generate random dwords
	for (i = 0; i < dwords; i++) {
		data[i] = esp_random();
	}

	// Generate remaining random bytes
	*((uint32_t*)last) = esp_random();
	buf = (uint8_t*)(data + i);
	for (i = 0; i < (len & 3); i++) {
		buf[i] = last[i];
	}
}

static esp_http_client_handle_t http_parse_init(const char *url,
		http_event_handle_cb event_cb)
{
	uint32_t cert_len = *((uint32_t*)HTTP_CERT_LEN_ADDR);
	const char *cert = (const char*)HTTP_CERT_ADDR;


	if (!cert_len || cert_len > MW_CERT_MAXLEN || !cert[0] ||
			0xFF == cert[0]) {
		LOGW("no valid certificate found, len %" PRIu32 ", start: %d",
				cert_len, cert[0]);
		cert = NULL;
	}

	return http_init(url, cert, event_cb);
}

static void http_parse_url_set(const char *url, MwCmd *reply)
{
	LOGD("set url %s", url);
	if (!d.http.h) {
		LOGD("init, HTTP URL: %s", url);
		d.http.h = http_parse_init(url, NULL);
		return;
	}

	if (((MW_HTTP_ST_IDLE != d.http.s) && (MW_HTTP_ST_ERROR != d.http.s)) ||
		http_url_set(d.http.h, url)) {
		LOGE("HTTP failed to set URL %s", url);
		reply->cmd = htons(MW_CMD_ERROR);
	} else {
		LOGD("HTTP URL: %s", url);
	}
}

static void http_parse_method_set(esp_http_client_method_t method, MwCmd *reply)
{
	LOGD("set method %d", method);
	if (!d.http.h) {
		d.http.h = http_parse_init("", NULL);
	}

	if (((MW_HTTP_ST_IDLE != d.http.s) && (MW_HTTP_ST_ERROR != d.http.s)) ||
			http_method_set(d.http.h, method)) {
		LOGE("HTTP failed to set method %d", method);
		reply->cmd = htons(MW_CMD_ERROR);
	} else {
		LOGD("HTTP method: %d", method);
	}
}

static void http_parse_header_add(const char *data, MwCmd *reply)
{
	const char *item[2] = {0};
	int n_items;

	if (!d.http.h) {
		d.http.h = http_parse_init("", NULL);
	}

	if (((MW_HTTP_ST_IDLE != d.http.s) && (MW_HTTP_ST_ERROR != d.http.s))) {
		goto err;
	}

	n_items = itemizer(data, item, 2);
	LOGD("HTTP header: %s: %s", item[0], item[1]);

	if ((n_items != 2) || http_header_add(d.http.h, item[0], item[1])) {
		goto err;
	}

	return;
err:
	LOGE("HTTP header add failed");
	reply->cmd = htons(MW_CMD_ERROR);
}

static void http_parse_header_del(const char *key, MwCmd *reply)
{
	if (((MW_HTTP_ST_IDLE != d.http.s) && (MW_HTTP_ST_ERROR != d.http.s)) ||
		http_header_del(d.http.h, key)) {
		LOGE("HTTP failed to del header %s", key);
		reply->cmd = htons(MW_CMD_ERROR);
	} else {
		LOGD("HTTP del header: %s", key);
	}
}

static void http_parse_open(uint32_t write_len, MwCmd *reply)
{
	LOGD("opening ");
	if (((MW_HTTP_ST_IDLE != d.http.s) && (MW_HTTP_ST_ERROR != d.http.s)) ||
			http_open(d.http.h, write_len)) {
		LOGE("HTTP open failed");
		reply->cmd = htons(MW_CMD_ERROR);
	} else {
		LsdChEnable(MW_HTTP_CH);
		LOGD("HTTP open OK, %" PRIu32 " bytes", write_len);
		if (write_len) {
			d.http.remaining = write_len;
			d.http.s = MW_HTTP_ST_OPEN_CONTENT_WAIT;
		} else {
			d.http.s = MW_HTTP_ST_FINISH_WAIT;
		}
	}
}

static uint16_t http_parse_finish(MwCmd *reply)
{
	int status;
	int len = 0;
	uint16_t replen = 0;

	if ((MW_HTTP_ST_FINISH_WAIT != d.http.s) ||
			((status = http_finish(d.http.h, &len)) < 0)) {
		LOGE("HTTP finish failed");
		reply->cmd = htons(MW_CMD_ERROR);
	} else {
		LOGD("HTTP finish: %d: %" PRId32 " bytes", status, len);
		reply->dwData[0] = htonl(len);
		reply->wData[2] = htons(status);
		reply->datalen = htons(6);
		replen = 6;
		if (len) {
			d.http.remaining = len;
			d.http.s = MW_HTTP_ST_FINISH_CONTENT_WAIT;
		} else {
			d.http.s = MW_HTTP_ST_IDLE;
			LsdChDisable(MW_HTTP_CH);
		}
	}

	return replen;
}

static void http_parse_cleanup(MwCmd *reply)
{
	d.http.s = MW_HTTP_ST_IDLE;

	if (!d.http.h) {
		return;
	}

	LsdChDisable(MW_HTTP_CH);
	if (http_cleanup(d.http.h)) {
		LOGE("HTTP cleanup failed");
		reply->cmd = htons(MW_CMD_ERROR);
	} else {
		LOGD("HTTP cleanup OK");
	}
	d.http.h = NULL;
}

static void http_cert_flash_write(const char *data, uint16_t len)
{
	static uint16_t written;
	uint16_t to_write;
	esp_err_t err;

	// Special condition: if no data payload, reset written counter
	if (!data && !len) {
		LOGD("reset data counter");
		written = 0;
		return;
	}

	LOGD("write %" PRIu16 " cert bytes", len);
	// Note we are using d.http.remaining as total (it is not decremented
	// each time we write data)
	to_write = MIN(d.http.remaining - written, len);
	if (to_write) {
		err = spi_flash_write(MW_CERT_FLASH_ADDR + 2 * sizeof(uint32_t)
				+ written, data, to_write);
		if (err) {
			LOGE("flash write failed");
			d.http.s = MW_HTTP_ST_IDLE;
			LsdChDisable(MW_HTTP_CH);
			return;
		}
		written += to_write;
	}

	if (written >= d.http.remaining) {
		d.http.s = MW_HTTP_ST_IDLE;
		LOGI("certificate stored");
		if (to_write < len) {
			LOGW("ignoring %d certificate bytes", len - to_write);
		}
		LsdChDisable(MW_HTTP_CH);
	}
}

static int http_parse_cert_query(MwCmd *reply)
{
	uint32_t cert = *((uint32_t*)HTTP_CERT_HASH_ADDR);
	int replen = 0;

	LOGD("cert hash: %08x", cert);
	if (0xFFFFFFFF == cert) {
		replen = 0;
		reply->cmd = htons(MW_CMD_ERROR);
	} else {
		replen = 4;
		reply->dwData[0] = htonl(cert);
	}

	return replen;
}

static int http_cert_erase(void)
{
	int cert_sect = MW_CERT_FLASH_ADDR>>12;
	int err = FALSE;

	for (int i = 0; !err && i < (1 + ((MW_CERT_MAXLEN - 1) /
					MW_FLASH_SECT_LEN)); i++) {
		err = spi_flash_erase_sector(cert_sect + i) != ESP_OK;
	}

	return err;
}

static void http_parse_cert_set(uint32_t x509_hash, uint16_t cert_len,
		MwCmd *reply)
{
	int err = FALSE;
	uint32_t installed = *(uint32_t*)HTTP_CERT_ADDR;

	if (d.http.s != MW_HTTP_ST_IDLE && d.http.s != MW_HTTP_ST_ERROR) {
		LOGE("not allowed in HTTP state %d", d.http.s);
		goto err_out;
	}
	// Check if erase request
	if (installed != 0xFFFFFFFF && !cert_len) {
		LOGD("erasing cert as per request");
		// Erase cert
		err = http_cert_erase();
		if (err) {
			goto err_out;
		} else {
			goto ok_out;
		}
	}
	if (x509_hash == installed) {
		LOGW("cert %08x is already installed", x509_hash);
	}
	if (cert_len > MW_CERT_MAXLEN) {
		LOGE("cert is %d bytes, maximum allowed is "
				STR(MW_CERT_MAXLEN) " bytes",
				cert_len);
		goto err_out;
	}
	// Erase the required sectors (round up the division between sect len)
	LOGD("erasing previous cert");
	err = http_cert_erase();
	// Write certificate id
	// TODO This should be done after writing the cert data!
	if (!err) {
		uint32_t dw_len = cert_len;
		LOGD("write cert hash %08x, len %" PRIu32, x509_hash, dw_len);
		err = spi_flash_write(MW_CERT_FLASH_ADDR, &x509_hash,
				sizeof(uint32_t));
		err = spi_flash_write(MW_CERT_FLASH_ADDR + sizeof(uint32_t),
				&dw_len, sizeof(uint32_t));
	}
	if (err) {
		LOGE("failed to erase certificate store");
		goto err_out;
	}

	LOGI("waiting certificate data");
	// Reset written data counter
	LsdChEnable(MW_HTTP_CH);
	http_cert_flash_write(NULL, 0);
	d.http.s = MW_HTTP_ST_CERT_SET;
	d.http.remaining = cert_len;

ok_out:
	// Everything OK
	return;

err_out:
	reply->cmd = htons(MW_CMD_ERROR);
}


static void sntp_config_set(const char *data, uint16_t len, MwCmd *reply)
{
	// We should have from 2 to 4 null terminated strings. First one
	// is the TZ string. Remaining ones are SNTP server names.
	const char *token[4];
	int num_tokens;
	uint16_t len_total;

	num_tokens = tokens_get(data, token, 4, &len_total);
	if (num_tokens < 2) {
		goto err_out;
	}

	if (len_total != len || len_total > MW_NTP_POOL_MAXLEN) {
		goto err_out;
	}

	// TZ string must be at least 4 bytes long (including null term)
	if ((token[1] - token[0]) < 4) {
		goto err_out;
	}

	memcpy(cfg.ntpPool, data, len);
	memset(cfg.ntpPool + len, 0, MW_NTP_POOL_MAXLEN - len);
	cfg.ntpPoolLen = len;
	if (MwNvCfgSave() < 0) {
		goto err_out;
	}

	sntp_set_config();

	return;

err_out:
	LOGE("SNTP configuration failed");
	reply->cmd = htons(MW_CMD_ERROR);
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
	reply_set_ok_empty(&reply);
	switch (ByteSwapWord(c->cmd)) {
		case MW_CMD_VERSION:
			// Cancel sleep timer
			if (d.tim) {
				xTimerStop(d.tim, 0);
				xTimerDelete(d.tim, 0);
				d.tim = NULL;
			}
			reply.cmd = MW_CMD_OK;
			reply.datalen = ByteSwapWord(2 + sizeof(MW_FW_VARIANT) - 1);
			reply.data[0] = MW_FW_VERSION_MAJOR;
			reply.data[1] = MW_FW_VERSION_MINOR;
			memcpy(reply.data + 2, MW_FW_VARIANT, sizeof(MW_FW_VARIANT) - 1);
			LsdSend((uint8_t*)&reply, ByteSwapWord(reply.datalen) +
					MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_ECHO:		// Echo request
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
	    		LOGI("SCAN!");
			int scan_len = wifi_scan(reply.data);
			if (scan_len <= 0) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				reply.datalen = scan_len;
				reply.datalen = ByteSwapWord(reply.datalen);
			}
			LsdSend((uint8_t*)&reply, scan_len + MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_AP_CFG:
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
				}
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_AP_CFG_GET:
			tmp = c->apCfg.cfgNum;
			if (tmp >= MW_NUM_AP_CFGS) {
				LOGE("Requested AP for cfg %d!", tmp);
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				replen = 0;
			} else {
				LOGI("Getting AP configuration %d...", tmp);
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
			replen = 0;
			LOGI("Getting current IP configuration...");
			replen = sizeof(MwMsgIpCfg);
			reply.datalen = ByteSwapWord(sizeof(MwMsgIpCfg));
			reply.ipCfg.cfgNum = 0;
			tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &reply.ipCfg.cfg);
			reply.ipCfg.dns1 = *dns_getserver(0);
			reply.ipCfg.dns2 = *dns_getserver(1);
			log_ip_cfg(&reply.ipCfg);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_IP_CFG:
			tmp = (uint8_t)c->ipCfg.cfgNum;
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
				}
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_IP_CFG_GET:
			tmp = c->ipCfg.cfgNum;
			replen = 0;
			if (tmp >= MW_NUM_AP_CFGS) {
				LOGE("Requested IP for cfg %d!", tmp);
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				LOGI("Getting IP configuration %d...", tmp);
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
			tmp = c->data[0];
			if (tmp < MW_NUM_AP_CFGS) {
				cfg.defaultAp = tmp;
				if (MwNvCfgSave() != 0) {
					LOGE("Failed to set default AP: %d", cfg.defaultAp);
					reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				} else {
					LOGI("Set default AP: %d", cfg.defaultAp);
				}
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_DEF_AP_CFG_GET:
			reply.datalen = ByteSwapWord(1);
			reply.data[0] = cfg.defaultAp;
			LOGI("Sending default AP: %d", cfg.defaultAp);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + 1, 0);
			break;

		case MW_CMD_AP_JOIN:
			// Start connecting to AP and jump to AP_JOIN state
			if ((c->data[0] >= MW_NUM_AP_CFGS) ||
					!(cfg.ap[c->data[0]].ssid[0])) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				LOGE("Invalid AP_JOIN on config %d", c->data[0]);
			} else {
				MwApJoin(c->data[0]);
				// TODO: Save configuration to update default AP?
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_AP_LEAVE:	// Leave access point
			LOGI("Disconnecting from AP");
			// Close all opened sockets
			MwFsmCloseAll();
			// Disconnect and switch to IDLE state
			esp_wifi_disconnect();
			esp_wifi_stop();
			d.s.sys_stat = MW_ST_IDLE;
			d.s.online = FALSE;
			LOGI("IDLE!");
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_TCP_CON:
			LOGI("TRYING TO CONNECT TCP SOCKET...");
			if (MwFsmTcpCon(&c->inAddr) < 0) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_TCP_BIND:
			if (MwFsmTcpBind(&c->bind)) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_CLOSE:
			// If channel number OK, disconnect the socket on requested channel
			if ((c->data[0] > 0) && (c->data[0] <= LSD_MAX_CH) &&
					d.ss[c->data[0] - 1]) {
				LOGI("Closing socket %d from channel %d",
						d.sock[c->data[0] - 1], c->data[0]);
				MwSockClose(c->data[0]);
				LsdChDisable(c->data[0]);
			} else {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				LOGE("Requested disconnect of not opened channel %d.",
						c->data[0]);
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_UDP_SET:
			LOGI("Configuring UDP socket...");
			if (MwUdpSet(&c->inAddr) < 0) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			}
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
				replen = 0;
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				LOGE("Requested unavailable channel!");
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_PING:
			LOGE("PING unimplemented");
			break;

		case MW_CMD_SNTP_CFG:
			LOGI("setting SNTP cfg for zone %s", c->data);
			sntp_config_set((char*)c->data, len, &reply);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_SNTP_CFG_GET:
			replen = cfg.ntpPoolLen;
			LOGI("sending SNTP cfg (%d bytes)", replen);
			memcpy(reply.data, cfg.ntpPool, replen);
			reply.datalen = htons(replen);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_DATETIME:
			ts = time(NULL);
			reply.datetime.dtBin[0] = 0;
			reply.datetime.dtBin[1] = ByteSwapDWord((uint32_t)ts);
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
			// Compute effective flash address
			c->flData.addr = ByteSwapDWord(c->flData.addr) +
				MW_FLASH_USER_BASE_ADDR;
			// Check for overflows (avoid malevolous attempts to write to
			// protected area
			if ((c->flData.addr + (len - sizeof(uint32_t) - 1)) <
					MW_FLASH_USER_BASE_ADDR) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				LOGE("Address/length combination overflows!");
			} else if (spi_flash_write(c->flData.addr,
						(uint32_t*)c->flData.data, len - sizeof(uint32_t)) !=
					ESP_OK) {
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
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				LOGE("Invalid address/length combination.");
			// Perform read and check result
			} else if (spi_flash_read(c->flRange.addr,
						(uint32_t*)reply.data, c->flRange.len) !=
					ESP_OK) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				LOGE("Flash read failed!");
			} else {
				reply.datalen = ByteSwapWord(c->flRange.len);
				LOGI("Flash read OK!");
			}
			LsdSend((uint8_t*)&reply, c->flRange.len + MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_FLASH_ERASE:
			// Check for sector overflow
			c->flSect = ByteSwapWord(c->flSect) + MW_FLASH_USER_BASE_SECT;
			if (c->flSect < MW_FLASH_USER_BASE_SECT) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				LOGW("Wrong sector number.");
			} else if (spi_flash_erase_sector(c->flSect) !=
					ESP_OK) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
				LOGE("Sector erase failed!");
			} else {
				LOGE("Sector erase OK!");
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_FLASH_ID:
			// FIXME Is there a way to add support?
			LOGW("FLASH_ID unsupported on ESP8266_RTOS_SDK");
			reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_SYS_STAT:
			MwSysStatFill(&reply);
			LOGI("%02X %02X %02X %02X", reply.data[0], reply.data[1],
					reply.data[2], reply.data[3]);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + sizeof(MwMsgSysStat),
				0);
			break;

		case MW_CMD_DEF_CFG_SET:
			// Check lengt and magic value
			if ((len != 4) || (c->dwData[0] !=
						ByteSwapDWord(MW_FACT_RESET_MAGIC))) {
				LOGE("Wrong DEF_CFG_SET command invocation!");
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else if (spi_flash_erase_sector(MW_CFG_FLASH_SECT) !=
					ESP_OK) {
				LOGE("Config flash sector erase failed!");
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				LOGI("Configuration set to default.");
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_HRNG_GET:
			replen = ByteSwapWord(c->rndLen);
			if (replen > MW_CMD_MAX_BUFLEN) {
				replen = 0;
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				reply.datalen = c->rndLen;
				rand_fill(reply.data, replen);
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_BSSID_GET:
			reply.datalen = ByteSwapWord(6);
			esp_wifi_get_mac(c->data[0], reply.data);
			LOGI("Got BSSID(%d) %02X:%02X:%02X:%02X:%02X:%02X",
					c->data[0], reply.data[0], reply.data[1],
					reply.data[2], reply.data[3],
					reply.data[4], reply.data[5]);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + 6, 0);
			break;

		case MW_CMD_GAMERTAG_SET:
			if (c->gamertag_set.slot >= MW_NUM_GAMERTAGS ||
					len != sizeof(struct mw_gamertag_set_msg)) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			} else {
				// Copy gamertag and save to flash
				memcpy(&cfg.gamertag[c->gamertag_set.slot],
						&c->gamertag_set.gamertag,
						sizeof(struct mw_gamertag));
				MwNvCfgSave();
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
			}
			reply.datalen = ByteSwapWord(replen);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_LOG:
			puts((char*)c->data);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_FACTORY_RESET:
			MwSetDefaultCfg();
			if (MwNvCfgSave() < 0) {
				reply.cmd = ByteSwapWord(MW_CMD_ERROR);
			}
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_SLEEP:
			// No reply, wakeup continues from user_init()
			deep_sleep();
			LOGI("Entering deep sleep");
			esp_deep_sleep(0);
			// As it takes a little for the module to enter deep
			// sleep, stay here for a while
			vTaskDelayMs(60000);

		case MW_CMD_HTTP_URL_SET:
			http_parse_url_set((char*)c->data, &reply);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_HTTP_METHOD_SET:
			http_parse_method_set(c->data[0], &reply);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_HTTP_HDR_ADD:
			http_parse_header_add((char*)c->data, &reply);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_HTTP_HDR_DEL:
			http_parse_header_del((char*)c->data, &reply);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_HTTP_OPEN:
			http_parse_open(ntohl(c->dwData[0]), &reply);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_HTTP_FINISH:
			replen = http_parse_finish(&reply);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			/// TODO: Thread this
			http_data_recv();
			break;

		case MW_CMD_HTTP_CLEANUP:
			http_parse_cleanup(&reply);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_HTTP_CERT_QUERY:
			replen = http_parse_cert_query(&reply);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + replen, 0);
			break;

		case MW_CMD_HTTP_CERT_SET:
			http_parse_cert_set(ntohl(c->dwData[0]),
					ntohs(c->wData[2]), &reply);
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

static void MwHttpWrite(const char *data, uint16_t len)
{
	uint16_t to_write;

	LOGD("HTTP data %" PRIu16 " bytes", len);
	// Writes should only be performed by client during the
	// OPEN_CONTENT_WAIT or CERT_SET states
	switch (d.http.s) {
	case MW_HTTP_ST_OPEN_CONTENT_WAIT:
		to_write = MIN(d.http.remaining, len);
		esp_http_client_write(d.http.h, data, to_write);
		d.http.remaining -= to_write;
		if (!d.http.remaining) {
			if (len != to_write) {
				LOGW("ignoring %" PRIu16 " extra bytes",
						len - to_write);
			}
			d.http.s = MW_HTTP_ST_FINISH_WAIT;
		}
		break;

	case MW_HTTP_ST_CERT_SET:
		// Save cert to allocated slot in flash
		http_cert_flash_write(data, len);
		break;

	default:
		LOGE("unexpected HTTP write attempt at state %d", d.http.s);
		break;
	}
}

// Process messages during ready stage
void MwFsmReady(MwFsmMsg *msg) {
	// Pointer to the message buffer (from RX line).
	MwMsgBuf *b = msg->d;
	MwCmd *rep;
	system_event_t *wifi = msg->d;

	switch (msg->e) {
		case MW_EV_WIFI:		///< WiFi events, excluding scan related.
			LOGI("WIFI_EVENT %d (not parsed)", wifi->event_id);
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
			} else if (MW_HTTP_CH == b->ch) {
				// Process channel using HTTP state machine
				MwHttpWrite((char*)b->data, b->len);
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

static void ap_join_ev_handler(system_event_t *wifi)
{
	LOGD("WiFi event: %d", wifi->event_id);
	switch(wifi->event_id) {
		case SYSTEM_EVENT_STA_START:
			esp_wifi_connect();
			break;

		case SYSTEM_EVENT_STA_GOT_IP:
			LOGI("got IP: %s, READY!", ip4addr_ntoa(
					&wifi->event_info.got_ip.ip_info.ip));
			d.s.sys_stat = MW_ST_READY;
			d.s.online = TRUE;
			break;

		case SYSTEM_EVENT_STA_CONNECTED:
			LOGD("station:"MACSTR" join",
					MAC2STR(wifi->event_info.connected.bssid));
			break;

		case SYSTEM_EVENT_STA_DISCONNECTED:
			d.n_reassoc++;
			LOGE("Disconnect %d, reason : %d", d.n_reassoc,
					wifi->event_info.disconnected.reason);
			if (d.n_reassoc < MW_REASSOC_MAX) {
				if (wifi->event_info.disconnected.reason ==
						WIFI_REASON_BASIC_RATE_NOT_SUPPORT) {
					/*Switch to 802.11 bgn mode */
					esp_wifi_set_protocol(ESP_IF_WIFI_STA,
							WIFI_PROTOCAL_11B |
							WIFI_PROTOCAL_11G |
							WIFI_PROTOCAL_11N);
				}
				esp_wifi_connect();
			} else {
				LOGE("Too many assoc attempts, dessisting");
				esp_wifi_disconnect();
				d.s.sys_stat = MW_ST_IDLE;
			}
			break;

		default:
			LOGE("unhandled event %d, connect failed, IDLE!",
					wifi->event_id);
			esp_wifi_disconnect();
			d.s.sys_stat = MW_ST_IDLE;
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
				ap_join_ev_handler(msg->d);
			} else if (MW_EV_SER_RX == msg->e) {
				// The only rx events supported during AP_JOIN are AP_LEAVE,
				// VERSION_GET and SYS_STAT
				if (MW_CMD_AP_LEAVE == (b->cmd.cmd>>8)) {
					MwFsmCmdProc((MwCmd*)b, b->len);
				} else if (MW_CMD_VERSION == (b->cmd.cmd>>8)) {
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
void MwFsmSockTsk(void *pvParameters) {
	fd_set readset;
	int i, ch, retval;
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
		led_toggle();
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
			LOGD("Recv msg, evt=%d", m.e);
			MwFsm(&m);
			// If event was MW_EV_SER_RX, free the buffer
			LsdRxBufFree();
		} else {
			// Timeout
			LOGD(".");
		}
	}
}

