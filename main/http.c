#include <esp_partition.h>
#include <string.h>
#include "globals.h"
#include "megawifi.h"
#include "util.h"
#include "http.h"
#include "lsd.h"

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
	/// Certificate x509 hash, used during CERT_SET
	uint32_t hash_tmp;
	/// Partition with the certificate
	const esp_partition_t *p;
	/// Buffer used to send/recv HTTP data
	char *buf;
};

static struct http_data d;

#define CERT_HASH_OFF	((size_t)0)
#define CERT_LEN_OFF	sizeof(uint32_t)
#define CERT_OFF	(2 * sizeof(uint32_t))

#define CERT_P_PTR(type, off)	((type*)SPI_FLASH_ADDR((d.p->address + (off))))

int http_init(char *data_buf)
{
	memset(&d, 0, sizeof(struct http_data));

	d.p = esp_partition_find_first(MW_DATA_PART_TYPE, MW_CERT_PART_SUBTYPE,
			MW_CERT_PART_LABEL);

	if (!d.p) {
		return 1;
	}

	d.buf = data_buf;

	return 0;
}

static esp_http_client_handle_t http_init_int(const char *url, const char *cert_pem,
		http_event_handle_cb event_cb)
{
	esp_http_client_config_t config = {
		.url = url,
		.cert_pem = cert_pem
	};

	return esp_http_client_init(&config);
}

static inline int http_url_set(esp_http_client_handle_t client,
		const char *url)
{
	return esp_http_client_set_url(client, url);
}

static inline int http_method_set(esp_http_client_handle_t client,
		esp_http_client_method_t method)
{
	return esp_http_client_set_method(client, method);
}

static inline int http_header_add(esp_http_client_handle_t client,
		const char *key, const char *value)
{
	return esp_http_client_set_header(client, key, value);
}

static inline int http_header_del(esp_http_client_handle_t client,
		const char *key)
{
	return esp_http_client_delete_header(client, key);
}

static inline int http_open(esp_http_client_handle_t client, int write_len)
{
	return esp_http_client_open(client, write_len);
}

static inline int http_body_write(esp_http_client_handle_t client,
		const char *data, int len)
{
	return esp_http_client_write(client, data, len);
}

static inline int http_finish(esp_http_client_handle_t client, int *data_len)
{
	int len;

	len = esp_http_client_fetch_headers(client);
	if (ESP_FAIL == len) {
		return ESP_FAIL;
	}
	if (data_len) {
		*data_len = len;
	}
	return esp_http_client_get_status_code(client);
}

static inline int http_cleanup(esp_http_client_handle_t client)
{
	return esp_http_client_cleanup(client);
}

esp_http_client_handle_t http_parse_init(const char *url,
		http_event_handle_cb event_cb)
{
	// No fancy esp_partition_mmap support, so access directly to data
	uint32_t cert_len = *CERT_P_PTR(uint32_t, CERT_LEN_OFF);
	const char *cert = CERT_P_PTR(const char, CERT_OFF);


	if (!cert_len || cert_len > MW_CERT_MAXLEN || !cert[0] ||
			0xFF == (unsigned char)cert[0]) {
		LOGW("no valid certificate found, len %" PRIu32 ", start: %d",
				cert_len, cert[0]);
		cert = NULL;
	}

	return http_init_int(url, cert, event_cb);
}

void http_parse_url_set(const char *url, MwCmd *reply)
{
	LOGD("set url %s", url);
	if (!d.h) {
		LOGD("init, HTTP URL: %s", url);
		d.h = http_parse_init(url, NULL);
		return;
	}

	if (((MW_HTTP_ST_IDLE != d.s) && (MW_HTTP_ST_ERROR != d.s)) ||
		http_url_set(d.h, url)) {
		LOGE("HTTP failed to set URL %s", url);
		reply->cmd = htons(MW_CMD_ERROR);
	} else {
		LOGD("HTTP URL: %s", url);
	}
}

void http_parse_method_set(esp_http_client_method_t method, MwCmd *reply)
{
	LOGD("set method %d", method);
	if (!d.h) {
		d.h = http_parse_init("", NULL);
	}

	if (((MW_HTTP_ST_IDLE != d.s) && (MW_HTTP_ST_ERROR != d.s)) ||
			http_method_set(d.h, method)) {
		LOGE("HTTP failed to set method %d", method);
		reply->cmd = htons(MW_CMD_ERROR);
	} else {
		LOGD("HTTP method: %d", method);
	}
}

void http_parse_header_add(const char *data, MwCmd *reply)
{
	const char *item[2] = {0};
	int n_items;

	if (!d.h) {
		d.h = http_parse_init("", NULL);
	}

	if (((MW_HTTP_ST_IDLE != d.s) && (MW_HTTP_ST_ERROR != d.s))) {
		goto err;
	}

	n_items = itemizer(data, item, 2);
	LOGD("HTTP header: %s: %s", item[0], item[1]);

	if ((n_items != 2) || http_header_add(d.h, item[0], item[1])) {
		goto err;
	}

	return;
err:
	LOGE("HTTP header add failed");
	reply->cmd = htons(MW_CMD_ERROR);
}

void http_parse_header_del(const char *key, MwCmd *reply)
{
	if (((MW_HTTP_ST_IDLE != d.s) && (MW_HTTP_ST_ERROR != d.s)) ||
		http_header_del(d.h, key)) {
		LOGE("HTTP failed to del header %s", key);
		reply->cmd = htons(MW_CMD_ERROR);
	} else {
		LOGD("HTTP del header: %s", key);
	}
}

void http_parse_open(uint32_t write_len, MwCmd *reply)
{
	LOGD("opening ");
	if (((MW_HTTP_ST_IDLE != d.s) && (MW_HTTP_ST_ERROR != d.s)) ||
			http_open(d.h, write_len)) {
		LOGE("HTTP open failed");
		reply->cmd = htons(MW_CMD_ERROR);
	} else {
		LsdChEnable(MW_HTTP_CH);
		LOGD("HTTP open OK, %" PRIu32 " bytes", write_len);
		if (write_len) {
			d.remaining = write_len;
			d.s = MW_HTTP_ST_OPEN_CONTENT_WAIT;
		} else {
			d.s = MW_HTTP_ST_FINISH_WAIT;
		}
	}
}

uint16_t http_parse_finish(MwCmd *reply)
{
	int status;
	int len = 0;
	uint16_t replen = 0;

	if ((MW_HTTP_ST_FINISH_WAIT != d.s) ||
			((status = http_finish(d.h, &len)) < 0)) {
		LOGE("HTTP finish failed");
		reply->cmd = htons(MW_CMD_ERROR);
	} else {
		LOGD("HTTP finish: %d: %" PRId32 " bytes", status, len);
		reply->dwData[0] = htonl(len);
		reply->wData[2] = htons(status);
		reply->datalen = htons(6);
		replen = 6;
		if (len) {
			d.remaining = len;
			d.s = MW_HTTP_ST_FINISH_CONTENT_WAIT;
		} else {
			d.s = MW_HTTP_ST_IDLE;
			LsdChDisable(MW_HTTP_CH);
		}
	}

	return replen;
}

void http_parse_cleanup(MwCmd *reply)
{
	d.s = MW_HTTP_ST_IDLE;

	if (!d.h) {
		return;
	}

	LsdChDisable(MW_HTTP_CH);
	if (http_cleanup(d.h)) {
		LOGE("HTTP cleanup failed");
		reply->cmd = htons(MW_CMD_ERROR);
	} else {
		LOGD("HTTP cleanup OK");
	}
	d.h = NULL;
}

void http_cert_flash_write(const char *data, uint16_t len)
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
	// Note we are using d.remaining as total (it is not decremented
	// each time we write data)
	to_write = MIN(d.remaining - written, len);
	if (to_write) {
		err = esp_partition_write(d.p, CERT_OFF + written,
				data, to_write);
		if (err) {
			LOGE("flash write failed");
			d.s = MW_HTTP_ST_IDLE;
			LsdChDisable(MW_HTTP_CH);
			return;
		}
		written += to_write;
	}

	if (written >= d.remaining) {
		// Write certificate hash
		esp_partition_write(d.p, 0, &d.hash_tmp, sizeof(uint32_t));
		d.s = MW_HTTP_ST_IDLE;
		LOGI("certificate %08x stored", d.hash_tmp);
		if (to_write < len) {
			LOGW("ignoring %d certificate bytes", len - to_write);
		}
		LsdChDisable(MW_HTTP_CH);
	}
}

int http_parse_cert_query(MwCmd *reply)
{
	uint32_t cert = 0xFFFFFFFF;
	int replen = 0;
	esp_err_t err;

	err = esp_partition_read(d.p, 0, &cert, sizeof(uint32_t));

	LOGD("cert hash: %08x", cert);
	if (err || 0xFFFFFFFF == cert) {
		replen = 0;
		reply->cmd = htons(MW_CMD_ERROR);
	} else {
		replen = 4;
		reply->dwData[0] = htonl(cert);
	}

	return replen;
}

int http_cert_erase(void)
{
	return esp_partition_erase_range(d.p, 0, d.p->size);
}

void http_parse_cert_set(uint32_t x509_hash, uint16_t cert_len, MwCmd *reply)
{
	int err = FALSE;
	uint32_t  installed = *CERT_P_PTR(uint32_t, CERT_HASH_OFF);

	if (d.s != MW_HTTP_ST_IDLE && d.s != MW_HTTP_ST_ERROR) {
		LOGE("not allowed in HTTP state %d", d.s);
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
	if (!err) {
		// Write certificate length, and store for later the hash
		uint32_t dw_len = cert_len;
		LOGD("write cert hash %08x, len %" PRIu32, x509_hash, dw_len);
		err = esp_partition_write(d.p, CERT_LEN_OFF, &dw_len,
				sizeof(uint32_t));
		d.hash_tmp = x509_hash;
	}
	if (err) {
		LOGE("failed to erase certificate store");
		goto err_out;
	}

	LOGI("waiting certificate data");
	// Reset written data counter
	LsdChEnable(MW_HTTP_CH);
	http_cert_flash_write(NULL, 0);
	d.s = MW_HTTP_ST_CERT_SET;
	d.remaining = cert_len;

ok_out:
	// Everything OK
	return;

err_out:
	reply->cmd = htons(MW_CMD_ERROR);
}

#define http_err_set(...)	do {	\
	LsdChDisable(MW_HTTP_CH);	\
	LOGE(__VA_ARGS__);		\
	d.s = MW_HTTP_ST_ERROR;	\
} while(0)

void http_recv(void)
{
	int readed;

	if (d.s != MW_HTTP_ST_FINISH_CONTENT_WAIT) {
		http_err_set("ignoring unexpected HTTP data on state %d",
				d.s);
		return;
	}

	while (d.remaining > 0) {
		readed = esp_http_client_read(d.h, d.buf,
				MW_MSG_MAX_BUFLEN);
		if (-1 == readed) {
			http_err_set("HTTP read error, %d remaining",
					d.remaining);
			return;
		}
		LsdSend((uint8_t*)d.buf, readed, MW_HTTP_CH);
		d.remaining -= readed;
	}

	if (d.remaining < 0) {
		LOGW("HTTP ignoring extra %d bytes", -d.remaining);
	}
	LOGD("HTTP request complete");
	d.s = MW_HTTP_ST_IDLE;
	LsdChDisable(MW_HTTP_CH);
}

void http_send(const char *data, uint16_t len)
{
	uint16_t to_write;

	LOGD("HTTP data %" PRIu16 " bytes", len);
	// Writes should only be performed by client during the
	// OPEN_CONTENT_WAIT or CERT_SET states
	switch (d.s) {
	case MW_HTTP_ST_OPEN_CONTENT_WAIT:
		to_write = MIN(d.remaining, len);
		esp_http_client_write(d.h, data, to_write);
		d.remaining -= to_write;
		if (!d.remaining) {
			if (len != to_write) {
				LOGW("ignoring %" PRIu16 " extra bytes",
						len - to_write);
			}
			d.s = MW_HTTP_ST_FINISH_WAIT;
		}
		break;

	case MW_HTTP_ST_CERT_SET:
		// Save cert to allocated slot in flash
		http_cert_flash_write(data, len);
		break;

	default:
		LOGE("unexpected HTTP write attempt at state %d", d.s);
		break;
	}
}

