/*
 * linux/arch/arm/mach-omap2/board-rx51-peripherals.c
 *
 * Copyright (C) 2008-2009 Nokia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/gpio/machine.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/timer.h>

static struct gpiod_lookup_table rx51_fmtx_gpios_table = {
	.dev_id = "2-0063",
	.table = {
		GPIO_LOOKUP("gpio.6", 3, "reset", GPIO_ACTIVE_HIGH), /* 163 */
		{ },
	},
};

static __init void rx51_gpio_init(void)
{
	gpiod_add_lookup_table(&rx51_fmtx_gpios_table);
}

void __init rx51_peripherals_init(void)
{
	rx51_gpio_init();
}

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
