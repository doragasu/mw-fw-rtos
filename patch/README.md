Apply `lwip_set_time.patch` to `ESP8266_RTOS_SDK`, or otherwise the `dt_ok` field in the `MwSysStat` structure will never be updated when time is set.
