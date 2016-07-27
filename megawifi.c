/************************************************************************//**
 * \brief MeGaWiFi application programming interface.
 *
 * \Author Jesus Alonso (idoragasu)
 * \date   2016
 ****************************************************************************/

// Espressif definitions
#include <espressif/esp_common.h>
#include <esp/uart.h>

// Newlib
#include <stdio.h>
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

// Time keeping
#include <sntp.h>
#include <time.h>

// Local configuration data
// TODO REMOVE ONCE TESTS FINISHED
#include "ssid_config.h"

#include "megawifi.h"
#include "lsd.h"
#include "util.h"

/** \addtogroup MwApi MwState Possible states of the system state machine.
 *  \{ */
typedef enum {
	MW_ST_INIT = 0,		///< Initialization state.
	MW_ST_IDLE,			///< Idle state, until connected to an AP.
	MW_ST_SCAN,			///< Scanning access points.
	MW_ST_TIME_CONF,	///< Waiting for SNTP date/time configuration.
	MW_ST_READY,		///< Ready for communicating through the Internet.
	MW_ST_TRANSPARENT,	///< Transparent communication state.
	MW_ST_MAX			///< Limit number for state machine.
} MwState;
/** \} */

/** \addtogroup MwApi MwSockStat Socket status.
 *  \{ */
typedef enum {
	MW_SOCK_NONE = 0,	///< Unused socket.
	MW_SOCK_TCP,		///< TCP socket, connection established.
	MW_SOCK_UDP			///< UDP socket
} MwSockStat;
/** \} */

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
	char ntpPool[MW_NTP_POOL_MAXLEN + 1];
	/// Index of the configuration used on last connection (-1 for none).
	char defaultAp;
	/// Timezone to use with SNTP.
	uint8_t timezone;
	/// Checksum
	uint8_t csum;
} MwNvCfg;
/** \} */


/** \addtogroup MwApi MwData Module data needed to handle module status
 *  \todo Maybe we should add a semaphore to access data in this struct.
 *  \{ */
typedef struct {
	/// System status
	MwState s;
	/// Sockets.
	int sock[MW_MAX_SOCK];
	/// Channel in use (might be better to change this to a char, or even
	/// remove it completely and use sock member.
	int chan[MW_MAX_SOCK];
	/// Socket status
	MwSockStat ss[MW_MAX_SOCK];
//	/// Timer used to update SNTP clock.
//	os_timer_t sntpTimer;
	/// FSM queue for event reception
	xQueueHandle q;
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

// Scan complete callback: called when a WiFi scan operation has finished.
static void ScanCompleteCb(struct sdk_bss_info *bss,
		                   sdk_scan_status_t status) {
	MwFsmMsg m;
	m.e = MW_EV_SCAN;

	if (status == SCAN_OK) {
		dprintf("Scan complete!\n");
		m.d = bss;
	} else {
		// Scan failed, set null pointer for the FSM to notice.
		dprintf("Scan failed with error %d!\n", status);
		m.d = NULL;
		
	}
	xQueueSend(d.q, &m, portMAX_DELAY);
}

/// Establish a connection with a remote server
int MwFsmTcpCon(MwMsgInAddr* addr) {
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
	struct in_addr *raddr;
	int err;
	int s;

	// Check channel is valid and not in use.
	if (addr->channel >= LSD_MAX_CH) {
		dprintf("Requested unavailable channel %d\n", addr->channel);
		return -1;
	}
	if (d.chan[addr->channel]) {
		dprintf("Requested already in-use channel %d\n", addr->channel);
		return -1;
	}

	// DNS lookup
    err = getaddrinfo(addr->data, addr->dst_port, &hints, &res);

    if(err != 0 || res == NULL) {
		dprintf("DNS lookup failure\n");
        if(res)
            freeaddrinfo(res);
        return -1;
    }
	// DNS lookup OK
    raddr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    dprintf("DNS lookup succeeded. IP=%s\n", inet_ntoa(*raddr));

    s = socket(res->ai_family, res->ai_socktype, 0);
    if(s < 0) {
        dprintf("... Failed to allocate socket.\n");
        freeaddrinfo(res);
        return -1;
    }

    dprintf("... allocated socket\n");

    if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        close(s);
        freeaddrinfo(res);
        dprintf("... socket connect failed.\n");
        return -1;
    }

    dprintf("... connected sock %d on ch %d\n", s, addr->channel);
    freeaddrinfo(res);
	// Record socket number and mark channel as in use.
	d.sock[addr->channel - 1] = s;
	d.chan[addr->channel - 1] = TRUE;
	d.ss[addr->channel - 1] = MW_SOCK_TCP;
	// Enable LSD channel
	LsdChEnable(addr->channel);
	return s;
}

// Closes a socket on the specified channel
void MwFsmTcpDis(int ch) {
	// TODO Might need to use a semaphore to access socket variables.
	ch--;
	d.ss[ch] = MW_SOCK_NONE;
	d.chan[ch] = FALSE;

	lwip_close(d.sock[ch]);
}

/// Close all opened sockets
void MwFsmCloseAll(void) {
	int i;

	for (i = 0; i < MW_MAX_SOCK; i++) {
		if (d.ss[i] > 0) {
			dprintf("Closing sock %d on ch %d\n", d.sock[i], i + 1);
			MwFsmTcpDis(i + 1);
		}
	}
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
//	uint16_t i;
//	uint8_t csum = 0;

	// Load configuration from flash
	// TODO: Test flash API
//	system_param_load(MW_CFG_FLASH_SEQ, 0, &cfg, sizeof(cfg));
//	// If configuration not OK, load default values.
//	for (i = 0; i < sizeof(cfg); i++) csum += ((uint8_t*)&cfg)[i];
//	if (csum) MwSetDefaultCfg();
	MwSetDefaultCfg();
	// Set default values for global variables
	memset(&d, 0, sizeof(d));
	d.s = MW_ST_INIT;

	// Create system queue
    d.q = xQueueCreate(MW_FSM_QUEUE_LEN, sizeof(MwFsmMsg));
  	// Create FSM task
	xTaskCreate(MwFsmTsk, (signed char*)"FSM", MW_FSM_STACK_LEN, &d.q,
			MW_FSM_PRIO, NULL);
	// Create task for receiving data from sockets
	xTaskCreate(MwFsmSockTsk, (signed char*)"SCK", MW_SOCK_STACK_LEN, &d.q,
			MW_SOCK_PRIO, NULL);
	// Create task for polling WiFi status
	xTaskCreate(MwWiFiStatPollTsk, (signed char*)"WPOL", MW_WPOLL_STACK_LEN,
			&d.q, MW_WPOLL_PRIO, NULL);
	// Initialize SNTP
	// TODO: Maybe this should be moved to the "READY" state
	for (i = 0, sntpSrv[0] = cfg.ntpPool; (i < SNTP_NUM_SERVERS_SUPPORTED) &&
			((sntpLen = strlen(sntpSrv[i])) > 0); i++) {
			sntpSrv[i + 1] = sntpSrv[i] + sntpLen + 1;
	}
	if (i) {
		sntp_set_update_delay(cfg.ntpUpDelay);
		tz.tz_dsttime = 60*cfg.timezone;
		tz.tz_minuteswest = 0;
		sntp_initialize(&tz);
		sntp_set_servers(sntpSrv, i);
	}
	// Initialize LSD layer (will create receive task among other stuff).
	LsdInit(&d.q);
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
	
	// Sanity check: total Lengt - header length = data length
	if ((totalLen - MW_CMD_HEADLEN) != len) {
		dprintf("MwFsmCmdProc, ERROR: Length inconsistent\n");
		dprintf("totalLen=%d, dataLen=%d\n", totalLen, len);
		return MW_CMD_FMT_ERROR;
	}

	// parse command
	switch (ByteSwapWord(c->cmd)) {
		case MW_CMD_ECHO:		// Echo request
			reply.cmd = ByteSwapWord(MW_CMD_OK);
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
			if (MW_ST_IDLE == d.s) {
				d.s = MW_ST_SCAN;
	    		dprintf("SCAN!\n");
	    		sdk_wifi_station_scan(NULL, (sdk_scan_done_cb_t)
						ScanCompleteCb);
			}
			break;

		case MW_CMD_AP_CFG:
			dprintf("AP_CFG unimplemented\n");
			break;

		case MW_CMD_IP_CFG:
			dprintf("IP_CFG unimplemented\n");
			break;

		case MW_CMD_AP_JOIN:
			dprintf("AP_JOIN unimplemented\n");
			break;

		case MW_CMD_AP_LEAVE:	// Leave access point
			dprintf("Disconnecting from AP\n");
			// Close all opened sockets
			MwFsmCloseAll();
			// Disconnect and switch to IDLE state
			sdk_wifi_station_disconnect();
			d.s = MW_ST_IDLE;
			dprintf("IDLE!\n");
			reply.cmd = MW_OK;
			reply.datalen = 0;
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_TCP_CON:
			// Only works when on READY state
			dprintf("TRYING TO CONNECT TCP SOCKET...\n");
			reply.datalen = 0;
			if (MW_ST_READY == d.s) {
				reply.cmd = (MwFsmTcpCon(&c->inAddr) < 0)?MW_CMD_ERROR:
					MW_CMD_OK;
			} else reply.cmd = MW_CMD_ERROR;
			reply.cmd = ByteSwapWord(reply.cmd);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_TCP_BIND:
			dprintf("TCP_BIND unimplemented\n");
			break;

		case MW_CMD_TCP_ACCEPT:
			dprintf("TCP_ACCEPT unimplemented\n");
			break;

		case MW_CMD_TCP_STAT:
			dprintf("TCP_STAT unimplemented\n");
			break;

		case MW_CMD_TCP_DISC:
			reply.datalen = 0;
			// If channel number OK, disconnect the socket on requested channel
			if ((c->data[0] > 0) && (c->data[0] <= LSD_MAX_CH) &&
					d.chan[c->data[0] - 1]) {
				dprintf("Closing socket %d from channel %d\n",
						d.sock[c->data[0] - 1], c->data[0]);
				MwFsmTcpDis(c->data[0]);
				reply.cmd = MW_CMD_OK;
			} else {
				reply.cmd = MW_CMD_ERROR;
				dprintf("Requested disconnect of not opened channel %d.\n",
						c->data[0]);
			}
			reply.cmd = ByteSwapWord(reply.cmd);
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN, 0);
			break;

		case MW_CMD_UDP_SET:
			dprintf("UDP_SET unimplemented\n");
			break;

		case MW_CMD_UDP_STAT:
			dprintf("UDP_STAT unimplemented\n");
			break;

		case MW_CMD_UDP_CLR:
			dprintf("UDP_CLR unimplemented\n");
			break;

		case MW_CMD_PING:
			dprintf("PING unimplemented\n");
			break;

		case MW_CMD_SNTP_CFG:
			dprintf("SNTP_CFG unimplemented\n");
			break;

		case MW_CMD_DATETIME:
			reply.cmd = MW_CMD_OK;
			ts = time(NULL);
			reply.datalen = StrCpyDst((char*)reply.data, ctime(&ts)) -
				(char*)reply.data;
			LsdSend((uint8_t*)&reply, MW_CMD_HEADLEN + reply.datalen, 0);
			break;

		case MW_CMD_FLASH_WRITE:
			dprintf("FLASH_WRITE unimplemented\n");
			break;

		case MW_CMD_FLASH_READ:
			dprintf("FLASH_READ unimplemented\n");
			break;

		default:
			dprintf("UNKNOWN REQUEST!\n");
			break;
	}
	return MW_OK;
}

// Process messages during ready stage
void MwFsmReady(MwFsmMsg *msg) {
	// Pointer to the message buffer (from RX line).
	MwMsgBuf *b = msg->d;

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
				// TODO: Error handling
				MwFsmCmdProc((MwCmd*)b, b->len);
			} else {
				// Forward message if channel is enabled.
				if (b->ch < LSD_MAX_CH && d.chan[b->ch - 1]) {
					if (send(d.sock[b->ch - 1], b->data, b->len, 0) != b->len) {
						dprintf("DEB: CH %d socket send error!\n", b->ch);
						// TODO throw error event
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
//			LsdSend(, , d.chan[b->ch - 1]);
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
	dprintf("\nThat's all!\n\n");
}

#ifdef _DEBUG_MSGS
#define MwDebBssListPrint(bss)	do{MwBssListPrint(bss);}while(0)
#else
#define MwDebBssListPrint(bss)
#endif

static void MwFsm(MwFsmMsg *msg) {
	// UART events shoulb be processed on the UART receive task, and send
	// proper events to FSM!
//	if (e->sig == MW_EVT_UART_IN) {
//	}
	uint16_t repLen;
	uint16_t tmp;
	struct sdk_bss_info *bss;
	uint8_t scratch[4];

	switch (d.s) {
		case MW_ST_INIT:
			// Ignore all events excepting the INIT DONE one
			if (msg->e == MW_EV_INIT_DONE) {
				d.s = MW_ST_IDLE;
				// TODO Remove AP connection when completing command
			    struct sdk_station_config config = {
			        .ssid = WIFI_SSID,
			        .password = WIFI_PASS,
			    };
			    sdk_wifi_set_opmode(STATION_MODE);
			    sdk_wifi_station_set_config(&config);
				// TODO: esp-open-rtos does not send events for WiFi status
				// changes, so you have to poll
				// sdk_wifi_station_get_connect_status() to know when the
				// module got DHCP IP. So we should set a timer and call the
				// above function each timeout.
				dprintf("INIT DONE!\n");
			}
			break;

		case MW_ST_IDLE:
			// IDLE state is abandoned once connected to an AP
			if (MW_EV_SER_RX == msg->e) {
				// TODO Parse commands on channel 0 only
			} else if (MW_EV_WIFI == msg->e) {
				if (STATION_GOT_IP ==  (uint32_t)msg->d) {
					d.s = MW_ST_READY;
					dprintf("READY!\n");
				}
			}
			break;

		case MW_ST_SCAN:
			// Ignore events until we receive the scan result
			if (MW_EV_SCAN == msg->e) {
				// If debug active, print station list
				bss = (struct sdk_bss_info*)msg->d;
				MwDebBssListPrint(bss);
				// Send station data to client. First obtain frame length,
				// to be able to do a split frame send (and avoid wasting
				// memory). For each found station, the length will be the
				// SSID length + 4 (mode, ch, strength, SSID length)
				repLen = tmp = 0;
				while (bss->next.stqe_next != NULL) {
					bss = bss->next.stqe_next;
					tmp += 4 + strnlen((const char*)bss->ssid, 32);
					// Check we have not exceeded maximum length, truncate
					// output if exceeded.
					if (tmp <= LSD_MAX_LEN) repLen = tmp;
					else break;
				}
				// Start transmission and send data
				tmp = 0;
				LsdSplitStart(NULL, 0, repLen, 0);
				while (bss->next.stqe_next != NULL && (tmp < repLen)) {
					bss = bss->next.stqe_next;
					scratch[0] = bss->authmode;
					scratch[1] = bss->channel;
					scratch[2] = bss->rssi;
					scratch[3] = strnlen((const char*)bss->ssid, 32);
					LsdSplitNext(scratch, 4);
					LsdSplitNext(bss->ssid, scratch[3]);
				}
				LsdSplitEnd(NULL, 0);
			}
			break;

		case MW_ST_TIME_CONF:
			// 
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

/// Polls sockets for data using select()
/// \note Maybe MwWiFiStatPollTsk() task should be merged with this one.
void MwFsmSockTsk(void *pvParameters) {
	fd_set readset;
	int i, max, retval;
	ssize_t recvd;
	struct timeval tv;

	//xQueueHandle *q = (xQueueHandle *)pvParameters;
	UNUSED_PARAM(pvParameters);

	tv.tv_sec = 1;
	tv.tv_usec = 0;

	while (1) {
		// Update list of active sockets (NOTE: investigate if select also
		// works with UDP sockets!)
		// TODO: use a variable to update the list only when there is a change
		FD_ZERO(&readset);
		max = 0;
		for (i = 0; i < MW_MAX_SOCK; i++) {
			if (d.ss[i] > 0) {
				FD_SET(d.sock[i], &readset);
				max = MAX(max, d.sock[i]);
			}
		}
		if ((retval = select(max + 1, &readset, NULL, NULL, &tv)) < 0) {
			// Error.
			dprintf("select() completed with error!\n");
			continue;
		}
		// If select returned 0, there was a timeout
		if (0 == retval) continue;
		dprintf("select()=%d\n", retval);
		// Poll the socket for data, and forward through the associated
		// channel.
		for (i = 0; i < MW_MAX_SOCK; i++) {
			if ((d.ss[i] > 0) && FD_ISSET(d.sock[i], &readset)) {
				dprintf("Rx: sock=%d, ch=%d\n", d.sock[i], i + 1);
				if ((recvd = recv(d.sock[i], buf, LSD_MAX_LEN, 0)) < 0) {
					// TODO: Throw error event
					MwFsmTcpDis(i + 1);
					dprintf("Error %d receiving from socket!\n", recvd);
				} else if (0 == recvd) {
					// Socket closed
					// TODO: Throw event
					MwFsmTcpDis(i + 1);
					dprintf("Socket closed!\n");
				} else {
					LsdSend(buf, (uint16_t)recvd, i + 1);
				}
			} // if (socket_has_data)
		} // for (socket list)
		
	} // while (1)
}

void MwFsmTsk(void *pvParameters) {
	xQueueHandle *q = (xQueueHandle *)pvParameters;
	MwFsmMsg m;

	while(1) {
		if (xQueueReceive(*q, &m, 1000)) {
			dprintf("Recv msg, evt=%d\n", m.e);
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
	uint8_t con_stat, prev_con_stat;
	MwFsmMsg m;
	xQueueHandle *q = (xQueueHandle *)pvParameters;

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
