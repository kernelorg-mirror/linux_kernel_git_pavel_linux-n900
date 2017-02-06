/*
 *  linux/arch/arm/plat-omap/gpio-switch.c
 *
 *  Copyright (C) 2004-2006 Nokia Corporation
 *  Written by Juha Yrjölä <juha.yrjola@nokia.com>
 *         and Paul Mundt <paul.mundt@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
  /my/ofono/plugins/nokia-gpio.c:#define GPIO_SWITCH"/sys/devices/platform/gpio-switch"

  Ugh. Ofono tests for gpio-switch device?!
 */

#include <linux/sched.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <plat/gpio-switch.h>


static struct platform_driver gpio_sw_driver = {
	.driver		= {
		.name	= "gpio-switch",
	},
};

static int __init gpio_sw_init(void)
{
	int r;

	printk(KERN_INFO "OMAP GPIO switch handler initializing\n");

	r = platform_driver_register(&gpio_sw_driver);
	if (r)
		return r;

	platform_device_register_simple("gpio-switch",
							       -1, NULL, 0);
	return 0;
}

static void __exit gpio_sw_exit(void)
{
}

#ifndef MODULE
late_initcall(gpio_sw_init);
#else
module_init(gpio_sw_init);
#endif
module_exit(gpio_sw_exit);

MODULE_AUTHOR("Juha Yrjölä <juha.yrjola@nokia.com>, Paul Mundt <paul.mundt@nokia.com");
MODULE_DESCRIPTION("GPIO switch driver");
MODULE_LICENSE("GPL");
