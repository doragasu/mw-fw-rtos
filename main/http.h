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

esp_http_client_handle_t http_parse_init(const char *url,
		http_event_handle_cb event_cb);

void http_parse_url_set(const char *url, MwCmd *reply);

void http_parse_method_set(esp_http_client_method_t method, MwCmd *reply);

void http_parse_header_add(const char *data, MwCmd *reply);

void http_parse_header_del(const char *key, MwCmd *reply);

void http_parse_open(uint32_t write_len, MwCmd *reply);

uint16_t http_parse_finish(MwCmd *reply);

void http_parse_cleanup(MwCmd *reply);

void http_cert_flash_write(const char *data, uint16_t len);

int http_parse_cert_query(MwCmd *reply);

int http_cert_erase(void);

void http_parse_cert_set(uint32_t x509_hash, uint16_t cert_len, MwCmd *reply);

#endif /*_HTTP_H_*/

