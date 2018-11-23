/*
 * Copyright 2018 Pavel Machek <pavel@ucw.cz>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/leds.h>

DEFINE_LED_TRIGGER(ledtrig_micmute);

void ledtrig_mic_muted(bool muted)
{
	led_trigger_event(ledtrig_micmute, muted ? LED_FULL : LED_OFF);
}
EXPORT_SYMBOL(ledtrig_mic_muted);

static int __init ledtrig_micmute_init(void)
{
	led_trigger_register_simple("mic-muted", &ledtrig_micmute);

	return 0;
}
device_initcall(ledtrig_micmute_init);
