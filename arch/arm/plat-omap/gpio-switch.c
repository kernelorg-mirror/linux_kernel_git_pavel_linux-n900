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

struct gpio_switch {
	char		name[14];
	u16		gpio;
	unsigned	flags:4;
	unsigned	type:4;
	unsigned	state:1;
	unsigned	both_edges:1;

	u16		debounce_rising;
	u16		debounce_falling;

	int		disabled;

	void (* notify)(void *data, int state);
	void *notify_data;

	struct work_struct	work;
	struct timer_list	timer;
	struct platform_device	pdev;

	struct list_head	node;

	bool		failed;
};

static LIST_HEAD(gpio_switches);
static struct platform_device *gpio_sw_platform_dev;
static struct platform_driver gpio_sw_driver;

static const struct omap_gpio_switch *board_gpio_sw_table;
static int board_gpio_sw_count;

static void gpio_sw_cleanup(void)
{
	struct gpio_switch *sw = NULL, *old = NULL;
}

static void __init report_initial_state(void)
{
	struct gpio_switch *sw;

	printk("Report initial state...\n");
}

static int gpio_sw_remove(struct platform_device *dev)
{
	return 0;
}

static struct platform_driver gpio_sw_driver = {
	.remove		= gpio_sw_remove,
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

	gpio_sw_platform_dev = platform_device_register_simple("gpio-switch",
							       -1, NULL, 0);
	if (IS_ERR(gpio_sw_platform_dev)) {
		r = PTR_ERR(gpio_sw_platform_dev);
		goto err1;
	}

	//r = add_board_switches();
	if (r < 0)
		goto err2;

	//report_initial_state();

	return 0;
err2:
	gpio_sw_cleanup();
	platform_device_unregister(gpio_sw_platform_dev);
err1:
	platform_driver_unregister(&gpio_sw_driver);
	return r;
}

static void __exit gpio_sw_exit(void)
{
	gpio_sw_cleanup();
	platform_device_unregister(gpio_sw_platform_dev);
	platform_driver_unregister(&gpio_sw_driver);
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
