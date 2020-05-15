#include "game_api.h"
#include "linux_list.h"

struct key_val {
	struct list_head _head;
	const char *key;
	const char *val;
};

struct ga {
	char *endpoint;
	char *priv_key;
	struct list_head kv_list;
};

bool ga_init(void)
{
	return false;
}

void ga_deinit(void)
{
}

bool ga_endpoint_set(const char *endpoint)
{
	return false;
}

bool ga_private_key_set(const char *private_key)
{
	return false;
}

bool ga_key_value_add(const char *key, const char *value)
{
	return false;
}

bool ga_key_value_clear(void)
{
	return false;
}

bool ga_request(esp_http_client_method_t method, const char *request,
		int32_t body_len)
{
	return false;
}

