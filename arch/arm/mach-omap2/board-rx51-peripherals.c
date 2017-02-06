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
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/spi/spi.h>
#include <linux/wl12xx.h>
#include <linux/spi/tsc2005.h>
#include <linux/i2c.h>
#include <linux/i2c/twl.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/regulator/machine.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/gpio/machine.h>
#include <linux/omap-gpmc.h>
#include <linux/mmc/host.h>
#include <linux/power/isp1704_charger.h>
#include <linux/platform_data/spi-omap2-mcspi.h>
#include <linux/platform_data/mtd-onenand-omap2.h>

#include <plat/dmtimer.h>

#include <asm/system_info.h>

#include "common.h"
#include <linux/omap-dma.h>

#include "board-rx51.h"

#include <sound/tlv320aic3x.h>
#include <sound/tpa6130a2-plat.h>
#include <linux/platform_data/media/si4713.h>
#include <linux/platform_data/leds-lp55xx.h>

#include <video/omap-panel-data.h>

#include "mux.h"
#include "omap-pm.h"
#include "hsmmc.h"
#include "common-board-devices.h"
#include "soc.h"
#include "omap-secure.h"

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

