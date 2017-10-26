/*
 * linux/arch/arm/mach-omap2/common.c
 *
 * Code common to all OMAP2+ machines.
 *
 * Copyright (C) 2009 Texas Instruments
 * Copyright (C) 2010 Nokia Corporation
 * Tony Lindgren <tony@atomide.com>
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>

#include "common.h"
#include "omap-secure.h"

/*
 * Stub function for OMAP2 so that common files
 * continue to build when custom builds are used
 */
int __weak omap_secure_ram_reserve_memblock(void)
{
	return 0;
}

void __init omap_reserve(void)
{
	omap_secure_ram_reserve_memblock();
	omap_barrier_reserve_memblock();
}

void iam_alive(void)
{
	int i;
	int debug_gpio = 92;
	printk("request: %d\n", gpio_request(debug_gpio, "debug"));
	printk("output: %d\n", gpio_direction_output(debug_gpio, 1));

	for (i=0; i<5; i++) {
		gpio_set_value(debug_gpio, 0);
		msleep(1000);
		gpio_set_value(debug_gpio, 1);
		msleep(1000);
	}
	
}

