#include "globals.h"
#include "megawifi.c"
#include "http.h"

#define HTTP_CERT_HASH_ADDR	SPI_FLASH_ADDR(MW_CERT_FLASH_ADDR)

#define HTTP_CERT_LEN_ADDR	SPI_FLASH_ADDR(MW_CERT_FLASH_ADDR + \
		sizeof(uint32_t))

#define HTTP_CERT_ADDR		SPI_FLASH_ADDR(MW_CERT_FLASH_ADDR + \
		(2 * sizeof(uint32_t)))

static esp_http_client_handle_t http_init(const char *url, const char *cert_pem,
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
	uint32_t cert_len = *((uint32_t*)HTTP_CERT_LEN_ADDR);
	const char *cert = (const char*)HTTP_CERT_ADDR;


	if (!cert_len || cert_len > MW_CERT_MAXLEN || !cert[0] ||
			0xFF == (unsigned char)cert[0]) {
		LOGW("no valid certificate found, len %" PRIu32 ", start: %d",
				cert_len, cert[0]);
		cert = NULL;
	}

	return http_init(url, cert, event_cb);
}

void http_parse_url_set(const char *url, MwCmd *reply)
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

void http_parse_method_set(esp_http_client_method_t method, MwCmd *reply)
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

void http_parse_header_add(const char *data, MwCmd *reply)
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

void http_parse_header_del(const char *key, MwCmd *reply)
{
	if (((MW_HTTP_ST_IDLE != d.http.s) && (MW_HTTP_ST_ERROR != d.http.s)) ||
		http_header_del(d.http.h, key)) {
		LOGE("HTTP failed to del header %s", key);
		reply->cmd = htons(MW_CMD_ERROR);
	} else {
		LOGD("HTTP del header: %s", key);
	}
}

void http_parse_open(uint32_t write_len, MwCmd *reply)
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

uint16_t http_parse_finish(MwCmd *reply)
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

void http_parse_cleanup(MwCmd *reply)
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
		// Write certificate hash
		spi_flash_write(MW_CERT_FLASH_ADDR, &d.http.hash_tmp,
				sizeof(uint32_t));
		d.http.s = MW_HTTP_ST_IDLE;
		LOGI("certificate %" PRIu32 " stored", d.http.hash_tmp);
		if (to_write < len) {
			LOGW("ignoring %d certificate bytes", len - to_write);
		}
		LsdChDisable(MW_HTTP_CH);
	}
}

int http_parse_cert_query(MwCmd *reply)
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

int http_cert_erase(void)
{
	int cert_sect = MW_CERT_FLASH_ADDR>>12;
	int err = FALSE;

	for (int i = 0; !err && i < (1 + ((MW_CERT_MAXLEN - 1) /
					MW_FLASH_SECT_LEN)); i++) {
		err = spi_flash_erase_sector(cert_sect + i) != ESP_OK;
	}

	return err;
}

void http_parse_cert_set(uint32_t x509_hash, uint16_t cert_len, MwCmd *reply)
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
	if (!err) {
		// Write certificate length, and store for later the hash
		uint32_t dw_len = cert_len;
		LOGD("write cert hash %08x, len %" PRIu32, x509_hash, dw_len);
		err = spi_flash_write(MW_CERT_FLASH_ADDR + sizeof(uint32_t),
				&dw_len, sizeof(uint32_t));
		d.http.hash_tmp = x509_hash;
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


