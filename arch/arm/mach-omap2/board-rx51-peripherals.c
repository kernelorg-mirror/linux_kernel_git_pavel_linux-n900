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
	omap_register_gpio_switches(rx51_gpio_switches,
			ARRAY_SIZE(rx51_gpio_switches));
}
#else
static void __init rx51_add_gpio_switches(void)
{
}
#endif /* CONFIG_OMAP_GPIO_SWITCH || CONFIG_OMAP_GPIO_SWITCH_MODULE */

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)

static struct gpio_keys_button rx51_gpio_keys[] = {
	{
		.desc			= "Camera Lens Cover",
		.type			= EV_SW,
		.code			= SW_CAMERA_LENS_COVER,
		.gpio			= RX51_GPIO_CAMERA_LENS_COVER,
		.active_low		= 1,
		.debounce_interval	= RX51_GPIO_DEBOUNCE_TIMEOUT,
	}, {
		.desc			= "Camera Focus",
		.type			= EV_KEY,
		.code			= KEY_CAMERA_FOCUS,
		.gpio			= RX51_GPIO_CAMERA_FOCUS,
		.active_low		= 1,
		.debounce_interval	= RX51_GPIO_DEBOUNCE_TIMEOUT,
	}, {
		.desc			= "Camera Capture",
		.type			= EV_KEY,
		.code			= KEY_CAMERA,
		.gpio			= RX51_GPIO_CAMERA_CAPTURE,
		.active_low		= 1,
		.debounce_interval	= RX51_GPIO_DEBOUNCE_TIMEOUT,
	}, {
		.desc			= "Lock Button",
		.type			= EV_KEY,
		.code			= KEY_SCREENLOCK,
		.gpio			= RX51_GPIO_LOCK_BUTTON,
		.active_low		= 1,
		.debounce_interval	= RX51_GPIO_DEBOUNCE_TIMEOUT,
	}, {
		.desc			= "Keypad Slide",
		.type			= EV_SW,
		.code			= SW_KEYPAD_SLIDE,
		.gpio			= RX51_GPIO_KEYPAD_SLIDE,
		.active_low		= 1,
		.debounce_interval	= RX51_GPIO_DEBOUNCE_TIMEOUT,
	}, {
		.desc			= "Proximity Sensor",
		.type			= EV_SW,
		.code			= SW_FRONT_PROXIMITY,
		.gpio			= RX51_GPIO_PROXIMITY,
		.active_low		= 0,
		.debounce_interval	= RX51_GPIO_DEBOUNCE_TIMEOUT,
	}
};

static struct gpio_keys_platform_data rx51_gpio_keys_data = {
	.buttons	= rx51_gpio_keys,
	.nbuttons	= ARRAY_SIZE(rx51_gpio_keys),
};

static struct platform_device rx51_gpio_keys_device = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data	= &rx51_gpio_keys_data,
	},
};

static void __init rx51_add_gpio_keys(void)
{
	platform_device_register(&rx51_gpio_keys_device);
}
#else
static void __init rx51_add_gpio_keys(void)
{
}
#endif /* CONFIG_KEYBOARD_GPIO || CONFIG_KEYBOARD_GPIO_MODULE */

static uint32_t board_keymap[] = {
	/*
	 * Note that KEY(x, 8, KEY_XXX) entries represent "entrire row
	 * connected to the ground" matrix state.
	 */
	KEY(0, 0, KEY_Q),
	KEY(0, 1, KEY_O),
	KEY(0, 2, KEY_P),
	KEY(0, 3, KEY_COMMA),
	KEY(0, 4, KEY_BACKSPACE),
	KEY(0, 6, KEY_A),
	KEY(0, 7, KEY_S),

	KEY(1, 0, KEY_W),
	KEY(1, 1, KEY_D),
	KEY(1, 2, KEY_F),
	KEY(1, 3, KEY_G),
	KEY(1, 4, KEY_H),
	KEY(1, 5, KEY_J),
	KEY(1, 6, KEY_K),
	KEY(1, 7, KEY_L),

	KEY(2, 0, KEY_E),
	KEY(2, 1, KEY_DOT),
	KEY(2, 2, KEY_UP),
	KEY(2, 3, KEY_ENTER),
	KEY(2, 5, KEY_Z),
	KEY(2, 6, KEY_X),
	KEY(2, 7, KEY_C),
	KEY(2, 8, KEY_F9),

	KEY(3, 0, KEY_R),
	KEY(3, 1, KEY_V),
	KEY(3, 2, KEY_B),
	KEY(3, 3, KEY_N),
	KEY(3, 4, KEY_M),
	KEY(3, 5, KEY_SPACE),
	KEY(3, 6, KEY_SPACE),
	KEY(3, 7, KEY_LEFT),

	KEY(4, 0, KEY_T),
	KEY(4, 1, KEY_DOWN),
	KEY(4, 2, KEY_RIGHT),
	KEY(4, 4, KEY_LEFTCTRL),
	KEY(4, 5, KEY_RIGHTALT),
	KEY(4, 6, KEY_LEFTSHIFT),
	KEY(4, 8, KEY_F10),

	KEY(5, 0, KEY_Y),
	KEY(5, 8, KEY_F11),

	KEY(6, 0, KEY_U),

	KEY(7, 0, KEY_I),
	KEY(7, 1, KEY_F7),
	KEY(7, 2, KEY_F8),
};

static struct matrix_keymap_data board_map_data = {
	.keymap			= board_keymap,
	.keymap_size		= ARRAY_SIZE(board_keymap),
};

static struct twl4030_keypad_data rx51_kp_data = {
	.keymap_data	= &board_map_data,
	.rows		= 8,
	.cols		= 8,
	.rep		= 1,
};

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

static int rx51_twlgpio_setup(struct device *dev, unsigned gpio, unsigned n)
{
	/* FIXME this gpio setup is just a placeholder for now */
	gpio_request_one(gpio + 6, GPIOF_OUT_INIT_LOW, "backlight_pwm");
	gpio_request_one(gpio + 7, GPIOF_OUT_INIT_LOW, "speaker_en");

	return 0;
}

void __init rx51_peripherals_init(void)
{
	rx51_gpio_init();
//	rx51_add_gpio_keys();
	rx51_add_gpio_switches();
}

