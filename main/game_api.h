#ifndef _GAME_API_H_
#define _GAME_API_H_

#include "http.h"

void ga_init(void);

void ga_deinit(void);

bool ga_endpoint_set(const char *endpoint, const char *private_key);

bool ga_private_key_set(const char *private_key);

bool ga_key_value_add(const char *key, const char *value);

uint16_t ga_request(esp_http_client_method_t method, uint8_t num_paths,
		uint8_t num_kv_pairs, const char *data, int32_t *body_len);

#endif /*_GAME_API_H_*/

