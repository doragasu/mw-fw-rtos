#ifndef _GAME_API_H_
#define _GAME_API_H_

#include "http.h"

bool ga_init(void);

void ga_deinit(void);

bool ga_endpoint_set(const char *endpoint);

bool ga_private_key_set(const char *private_key);

bool ga_key_value_add(const char *key, const char *value);

bool ga_key_value_clear(void);

bool ga_request(esp_http_client_method_t method, const char *request,
		int32_t body_len);

#endif /*_GAME_API_H_*/

