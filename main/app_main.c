#include <nvs.h>
#include <nvs_flash.h>
#include "led.h"
#include "util.h"
#include "megawifi.h"

void app_main(void)
{
	// Initialize NVS.
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK( err );

	// UART2 is used for logging.
	LOGI("=== MeGaWiFi firmware version %d.%d-%s ===",
			MW_FW_VERSION_MAJOR, MW_FW_VERSION_MINOR, MW_FW_VARIANT);
	LOGI("            doragasu, 2016 ~ 2020\n");
	// Power the LED on
	led_init();
	led_on();
	// Initialize MeGaWiFi system and FSM
	if (MwInit()) {
		abort();
	}
	LOGI("Init done!");
}
