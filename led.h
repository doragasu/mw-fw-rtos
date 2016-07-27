#ifndef __LED_H__
#define __LED_H__

#include <espressif/esp_common.h>
#include <esp8266.h>

#define LED_GPIO_PIN		4

/// Initializes LED
#define LedInit()		do{gpio_enable(LED_GPIO_PIN, GPIO_OUTPUT);}while(0)

/// Powers the LED on
#define LedOn()			do{gpio_write(LED_GPIO_PIN, 0);}while(0)

/// Powers the LED off
#define LedOff()		do{gpio_write(LED_GPIO_PIN, 1);}while(0)

#endif /*__LED_H__*/

