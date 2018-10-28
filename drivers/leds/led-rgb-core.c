/*
 * LED RGB Class Support
 *
 * Author: Heiner Kallweit <hkallweit1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/leds.h>
#include "leds.h"

struct led_rgb led_hsv_to_rgb(struct led_hsv hsv)
{
	unsigned int h = hsv.hue >> 24;
	unsigned int s = hsv.saturation >> 24;
	unsigned int v = hsv.value >> 24;
	unsigned int f, p, q, t, r, g, b;
	struct led_rgb res;

	if (!v) {
		res.red = 0;
		res.green = 0;
		res.blue = 0;
		return res;
	}
	if (!s) {
		res.red = v << 24;
		res.green = v << 24;
		res.blue = v << 24;
		return res;
	}

	f = DIV_ROUND_CLOSEST((h % 42) * 255, 42);
	p = v - DIV_ROUND_CLOSEST(s * v, 255);
	q = v - DIV_ROUND_CLOSEST(f * s * v, 255 * 255);
	t = v - DIV_ROUND_CLOSEST((255 - f) * s * v, 255 * 255);

	switch (h / 42) {
	case 0:
		r = v; g = t; b = p; break;
	case 1:
		r = q; g = v; b = p; break;
	case 2:
		r = p; g = v; b = t; break;
	case 3:
		r = p; g = q; b = v; break;
	case 4:
		r = t; g = p; b = v; break;
	case 5:
		r = v; g = p; b = q; break;
	}

	printk("hsv_to: h %5d, s %5d, v %5d -> %5d, %5d, %5d\n",
	       h*390, s*390, v*390, r*390, g*390, b*390);
	
	res.red = r << 24;
	res.green = g << 24;
	res.blue = b << 24;
	return res;
}
EXPORT_SYMBOL_GPL(led_hsv_to_rgb);
