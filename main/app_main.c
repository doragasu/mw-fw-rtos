#include "led.h"
#include "util.h"
#include "megawifi.h"


void app_main(void) {
	// UART2 is used for logging.
	LOGI("=== MeGaWiFi firmware version %d.%d-%s ===",
			MW_FW_VERSION_MAJOR, MW_FW_VERSION_MINOR, MW_FW_VARIANT);
	LOGI("            doragasu, 2016 ~ 2019\n");
	// Power the LED on
	led_init();
	led_on();
	// Initialize MeGaWiFi system and FSM
	MwInit();
	LOGI("Init done!");
}
