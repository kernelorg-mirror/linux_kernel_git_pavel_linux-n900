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

#include <linux/platform_data/tsl2563.h>
#include <linux/lis3lv02d.h>

#include <video/omap-panel-data.h>

#include <linux/platform_data/pwm_omap_dmtimer.h>
#include <linux/platform_data/media/ir-rx51.h>

#include <plat/gpio-switch.h>

#include "mux.h"
#include "omap-pm.h"
#include "hsmmc.h"
#include "common-board-devices.h"
#include "soc.h"
#include "omap-secure.h"

#define RX51_WL1251_POWER_GPIO		87
#define RX51_WL1251_IRQ_GPIO		42
#define RX51_FMTX_RESET_GPIO		163
#define RX51_FMTX_IRQ			53
#define RX51_LP5523_CHIP_EN_GPIO	41

#define RX51_USB_TRANSCEIVER_RST_GPIO	67

#define RX51_TSC2005_RESET_GPIO         104
#define RX51_TSC2005_IRQ_GPIO           100

#define LIS302_IRQ1_GPIO 181
#define LIS302_IRQ2_GPIO 180  /* Not yet in use */

#define RX51_LCD_RESET_GPIO	90

#define RX51_GPIO_CAMERA_FOCUS		68
#define RX51_GPIO_CAMERA_CAPTURE	69
#define RX51_GPIO_CAMERA_LENS_COVER	110
#define RX51_GPIO_CMT_APESLPX		70
#define RX51_GPIO_CMT_BSI		157
#define RX51_GPIO_CMT_EN		74
#define RX51_GPIO_CMT_RST		75
#define RX51_GPIO_CMT_RST_RQ		73
#define RX51_GPIO_CMT_WDDIS		13
#define RX51_GPIO_HEADPHONE		177
#define RX51_GPIO_LOCK_BUTTON		113
#define RX51_GPIO_PROXIMITY		89
#define RX51_GPIO_SLEEP_IND		162
#define RX51_GPIO_KEYPAD_SLIDE		71

#define RX51_GPIO_DEBOUNCE_TIMEOUT	10

#if defined(CONFIG_OMAP_GPIO_SWITCH) || defined(CONFIG_OMAP_GPIO_SWITCH_MODULE)

static struct omap_gpio_switch rx51_gpio_switches[] __initdata = {
};

static void __init rx51_add_gpio_switches(void)
{
#if 0
	omap_register_gpio_switches(rx51_gpio_switches,
			ARRAY_SIZE(rx51_gpio_switches));
#endif
}
#else
static void __init rx51_add_gpio_switches(void)
{
}
#endif /* CONFIG_OMAP_GPIO_SWITCH || CONFIG_OMAP_GPIO_SWITCH_MODULE */

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
	rx51_add_gpio_switches();
}

