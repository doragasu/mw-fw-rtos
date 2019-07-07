#include "led.h"

void led_init(void)
{
	gpio_config_t led_conf = {
		.intr_type = GPIO_INTR_DISABLE,
		.mode = GPIO_MODE_OUTPUT,
		.pin_bit_mask = 1<<LED_GPIO_PIN,
		.pull_down_en = 0,
		.pull_up_en = 0
	};

	gpio_config(&led_conf);
}

