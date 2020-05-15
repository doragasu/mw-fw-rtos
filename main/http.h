#ifndef _HTTP_H_
#define _HTTP_H_

#include <netdb.h>
#include <stdint.h>
#include <esp_http_client.h>

int http_module_init(char *data_buf);

esp_http_client_handle_t http_init(const char *url,
		http_event_handle_cb event_cb);

bool http_url_set(const char *url);

bool http_method_set(esp_http_client_method_t method);

bool http_header_add(const char *data);

bool http_header_del(const char *key);

bool http_open(uint32_t write_len);

bool http_finish(uint16_t *status, uint32_t *body_len);

bool http_cleanup(void);

void http_cert_flash_write(const char *data, uint16_t len);

uint32_t http_cert_query(void);

bool http_cert_erase(void);

bool http_cert_set(uint32_t x509_hash, uint16_t cert_len);

void http_send(const char *data, uint16_t len);

void http_recv(void);

#endif /*_HTTP_H_*/

