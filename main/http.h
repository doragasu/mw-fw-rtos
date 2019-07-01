#ifndef _HTTP_H_
#define _HTTP_H_

#include <netdb.h>
#include <stdint.h>
#include <esp_http_client.h>
#include "linux_list.h"
#include "mw-msg.h"

struct http_header {
	char *key;
	char *value;
	struct list_head _head;
};

esp_http_client_handle_t http_init(const char *url, const char *cert_pem,
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

#endif /*_HTTP_H_*/

