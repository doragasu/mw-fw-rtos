#ifndef __LED_H__
#define __LED_H__

#include <driver/gpio.h>

#define LED_GPIO_PIN		4

/// Initializes LED
void led_init(void);

/// Powers the LED on
#define led_on() do{gpio_set_level(LED_GPIO_PIN, 0);}while(0)


/// Powers the LED off
#define led_off() do{gpio_set_level(LED_GPIO_PIN, 1);}while(0)

#endif /*__LED_H__*/

