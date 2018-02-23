/*
 * Example of i2c flash LED driver
 *
 * Copyright (c) 2008-2011 Nokia Corporation
 * Copyright (c) 2011, 2017 Intel Corporation
 * Copyright (c) 2017 Pavel Machek <pavel@ucw.cz>
 *
 * Based on drivers/media/i2c/, by Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#define DEBUG
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/led-class-flash.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <media/i2c/adp1653.h>

#include <media/v4l2-flash-led-class.h>

/* LED numbers for Devicetree */
#define AS_LED_FLASH		0
#define AS_LED_INDICATOR	1

enum as_mode {
	AS_MODE_EXT_TORCH = 0,
	AS_MODE_INDICATOR = 1,
	AS_MODE_ASSIST = 2,
	AS_MODE_FLASH = 3,
	AS_MODE_OFF = 4,
};

struct fl_config {
	u32 flash_timeout_us;
	u32 flash_max_ua;
	u32 assist_max_ua;
	u32 indicator_max_ua;
	u32 voltage_reference;
	u32 peak;
};

struct fl_names {
	char flash[32];
	char indicator[32];
};

struct fl {
	struct i2c_client *client;

	struct mutex mutex;

	struct led_classdev_flash fled;
	struct led_classdev iled_cdev;

	struct v4l2_flash *vf;
	struct v4l2_flash *vfind;

	struct fwnode_handle *flash_node;
	struct fwnode_handle *indicator_node;

	struct fl_config cfg;

	enum as_mode mode;
	unsigned int timeout;
	unsigned int flash_current;
	unsigned int assist_current;
	unsigned int indicator_current;
	enum v4l2_flash_strobe_source strobe_source;

	int indicator_intensity_min;
	int indicator_intensity_step;
	int torch_intensity_min;
	int torch_intensity_step;
	int flash_intensity_min;
	int flash_intensity_step;
	int flash_timeout_min;
	int flash_timeout_step;

	int fault;
	struct gpio_desc *enable_gpio;
};

#define fled_to_fl(__fled) container_of(__fled, struct fl, fled)
#define iled_cdev_to_fl(__iled_cdev) \
	container_of(__iled_cdev, struct fl, iled_cdev)

/* Return negative errno else zero on success */
static int fl_write(struct fl *flash, u8 addr, u8 val)
{
	struct i2c_client *client = flash->client;
	int rval;

	rval = i2c_smbus_write_byte_data(client, addr, val);

	dev_dbg(&client->dev, "Write Addr:%02X Val:%02X %s\n", addr, val,
		rval < 0 ? "fail" : "ok");

	return rval;
}

/* Return negative errno else a data byte received from the device. */
static int fl_read(struct fl *flash, u8 addr)
{
	struct i2c_client *client = flash->client;
	int rval;

	rval = i2c_smbus_read_byte_data(client, addr);

	dev_dbg(&client->dev, "Read Addr:%02X Val:%02X %s\n", addr, rval,
		rval < 0 ? "fail" : "ok");

	return rval;
}

/* -----------------------------------------------------------------------------
 * Hardware configuration and trigger
 */

#define TIMEOUT_MAX		820000
#define TIMEOUT_STEP		54600
#define TIMEOUT_MIN		(TIMEOUT_MAX - ADP1653_REG_CONFIG_TMR_SET_MAX \
				 * TIMEOUT_STEP)
#define TIMEOUT_US_TO_CODE(t)	((TIMEOUT_MAX + (TIMEOUT_STEP / 2) - (t)) \
				 / TIMEOUT_STEP)
#define TIMEOUT_CODE_TO_US(c)	(TIMEOUT_MAX - (c) * TIMEOUT_STEP)

/* Write values into ADP1653 registers. */
static int adp1653_update_hw(struct fl *flash)
{
	struct i2c_client *client = flash->client;
	u8 out_sel;
	u8 config = 0;
	int rval;

	printk("Using current %d uA, indicator %d uA, assist %d uA\n",
	       flash->flash_current, flash->indicator_current, flash->assist_current);
	if (flash->flash_current > 100000)
		flash->flash_current = 100000;
	if (flash->indicator_current > ADP1653_REG_OUT_SEL_ILED_MAX)
		flash->indicator_current = ADP1653_REG_OUT_SEL_ILED_MAX;
	if (flash->assist_current > ADP1653_REG_OUT_SEL_HPLED_TORCH_MAX)
		flash->assist_current = ADP1653_REG_OUT_SEL_HPLED_TORCH_MAX;
	out_sel = flash->indicator_current << ADP1653_REG_OUT_SEL_ILED_SHIFT;

	/* FIXME: use flash current somewhere? :-) */

	switch (flash->mode) {
	case AS_MODE_OFF:
		break;
	case AS_MODE_FLASH:
		printk("mode: flash\n");
#if 0
		/* Flash mode, light on with strobe, duration from timer */
		config = ADP1653_REG_CONFIG_TMR_CFG;
		config |= TIMEOUT_US_TO_CODE(flash->flash_timeout->val)
			  << ADP1653_REG_CONFIG_TMR_SET_SHIFT;
		break;
#endif
	case AS_MODE_EXT_TORCH:
		printk("mode: torch\n");
	case AS_MODE_INDICATOR:
		printk("mode: indicator\n");
	case AS_MODE_ASSIST:
		printk("mode: assist\n");		
		/* Torch mode, light immediately on, duration indefinite */
		if (flash->assist_current)
			out_sel |= flash->assist_current << ADP1653_REG_OUT_SEL_HPLED_SHIFT;
		break;
	}

	rval = fl_write(flash, ADP1653_REG_OUT_SEL, out_sel);
	if (rval < 0)
		return rval;

	rval = fl_write(flash, ADP1653_REG_CONFIG, config);
	if (rval < 0)
		return rval;

	return 0;
}

/**
 * fl_set_config - Set flash configuration registers
 * @flash: The flash
 *
 * Configure the hardware with flash, assist and indicator currents, as well as
 * flash timeout.
 *
 * Return 0 on success, or a negative error code if an I2C communication error
 * occurred.
 */

/**
 * fl_set_control - Set flash control register
 * @flash: The flash
 * @mode: Desired output mode
 * @on: Desired output state
 *
 * Configure the hardware with output mode and state.
 *
 * Return 0 on success, or a negative error code if an I2C communication error
 * occurred.
 */

static int __fl_get_fault(struct fl *flash, u32 *_fault)
{
	u32 fault;
	int rval;

	fault = fl_read(flash, ADP1653_REG_FAULT);
	if (fault < 0)
		return fault;

	flash->fault |= fault;

	if (!flash->fault)
		return 0;

	/* Clear faults. */
	rval = fl_write(flash, ADP1653_REG_OUT_SEL, 0);
	if (rval < 0)
		return rval;

	printk("Flash: fault %lx\n", fault);
	*_fault = fault;
	flash->mode = AS_MODE_OFF;

	rval = adp1653_update_hw(flash);
	if (rval)
		return rval;

	return flash->fault;
}

static int fl_get_fault(struct led_classdev_flash *fled, u32 *fault)
{
	struct fl *flash = fled_to_fl(fled);

	return __fl_get_fault(flash, fault);
}

static unsigned int __fl_current_to_reg(unsigned int min, unsigned int max,
					     unsigned int step,
					     unsigned int val)
{
	if (val < min)
		val = min;

	if (val > max)
		val = max;

	return (val - min) / step;
}

static unsigned int fl_current_to_reg(struct fl *flash, bool is_flash,
					   unsigned int ua)
{
	if (is_flash)
		return __fl_current_to_reg(flash->torch_intensity_min,
						flash->cfg.assist_max_ua,
						flash->torch_intensity_step, ua);
	else
		return __fl_current_to_reg(flash->flash_intensity_min,
						flash->cfg.flash_max_ua,
						flash->flash_intensity_step, ua);
}

static int fl_set_indicator_brightness(struct led_classdev *iled_cdev,
					    enum led_brightness brightness)
{
	struct fl *flash = iled_cdev_to_fl(iled_cdev);
	int rval;

	flash->indicator_current = brightness;
	flash->mode = AS_MODE_INDICATOR;

	return adp1653_update_hw(flash);
}

static int fl_set_assist_brightness(struct led_classdev *fled_cdev,
					 enum led_brightness brightness)
{
	struct led_classdev_flash *fled = lcdev_to_flcdev(fled_cdev);
	struct fl *flash = fled_to_fl(fled);
	int rval;

	
	flash->assist_current = brightness;
	flash->mode = AS_MODE_ASSIST;

	return adp1653_update_hw(flash);	
}

static int fl_set_flash_brightness(struct led_classdev_flash *fled,
					u32 brightness_ua)
{
	struct fl *flash = fled_to_fl(fled);

	printk("set_flash_brightness: %d uA, %d\n", brightness_ua, fled->brightness);

	flash->flash_current = brightness_ua;

	return adp1653_update_hw(flash);	
}

static int fl_set_flash_timeout(struct led_classdev_flash *fled,
				     u32 timeout_us)
{
	struct fl *flash = fled_to_fl(fled);

	flash->timeout = 0; // FIXME AS_TIMER_US_TO_CODE(timeout_us);
	return adp1653_update_hw(flash);	
}

static int fl_set_strobe(struct led_classdev_flash *fled, bool state)
{
	struct fl *flash = fled_to_fl(fled);

	flash->mode = AS_MODE_FLASH;
	return adp1653_update_hw(flash);	
}

static const struct led_flash_ops fl_led_flash_ops = {
	.flash_brightness_set = fl_set_flash_brightness,
	.timeout_set = fl_set_flash_timeout,
	.strobe_set = fl_set_strobe,
	.fault_get = fl_get_fault,
};

static int fl_setup(struct fl *flash)
{
	struct device *dev = &flash->client->dev;
	u32 fault = 0;
	int rval;

	/* clear errors */
	rval = __fl_get_fault(flash, &fault);
	if (rval < 0)
		return rval;

	dev_dbg(dev, "Fault info: %02x\n", rval);

	flash->mode = AS_MODE_OFF;

	rval = adp1653_update_hw(flash);
	if (rval < 0)
		return rval;

	/* read status */
	rval = fl_get_fault(&flash->fled, &fault);
	if (rval < 0)
		return rval;

	return rval ? -EIO : 0;
}

static int
adp1653_init_device(struct fl *flash)
{
	struct i2c_client *client = flash->client;
	int rval;

	/* Clear FAULT register by writing zero to OUT_SEL */
	rval = fl_write(flash, ADP1653_REG_OUT_SEL, 0);
	if (rval < 0) {
		dev_err(&client->dev, "failed writing fault register\n");
		return -EIO;
	}

	/* Reset faults before reading new ones. */
	flash->fault = 0;
	return 0;
}

static int fl_test(struct fl *flash)
{
	int i;
	flash->flash_current = 0;
	for (i = 0; i<100; i++) {
		flash->assist_current = i*100;
		flash->mode = AS_MODE_ASSIST;
		adp1653_update_hw(flash);
		mdelay(50);
	}
	flash->mode = AS_MODE_OFF;
	adp1653_update_hw(flash);
	
}


static int fl_detect(struct fl *flash)
{
	struct device *dev = &flash->client->dev;
	int rval, man, model, rfu, version;
	const char *vendor;
	u32 fault;

	rval = adp1653_init_device(flash);
	if (rval)
		return rval;

	printk("flash: testing\n");
	fl_test(flash);
	printk("flash: test done\n");	

	rval = __fl_get_fault(flash, &fault);
	return rval;
}

static int fl_parse_node(struct fl *flash,
			      struct fl_names *names,
			      struct fwnode_handle *fwnode)
{
	struct fl_config *cfg = &flash->cfg;
	struct fwnode_handle *child;
	const char *name;
	const char *str;
	int rval;
	int on = 1;

	flash->enable_gpio = devm_gpiod_get(&flash->client->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(flash->enable_gpio)) {
		dev_err(&flash->client->dev, "Error getting GPIO\n");
		return PTR_ERR(flash->enable_gpio);
	}

	gpiod_set_value(flash->enable_gpio, on);
	if (on)
		/* Some delay is apparently required. */
		udelay(20);
	
	fwnode_for_each_child_node(fwnode, child) {
		u32 id = 0;

		fwnode_property_read_u32(
			child, is_of_node(child) ? "reg" : "led", &id);

		switch (id) {
		case AS_LED_FLASH:
			flash->flash_node = child;
			break;
		case AS_LED_INDICATOR:
			flash->indicator_node = child;
			break;
		default:
			dev_warn(&flash->client->dev,
				 "unknown LED %u encountered, ignoring\n", id);
			break;
		}
		fwnode_handle_get(child);
	}

	if (!flash->flash_node) {
		dev_err(&flash->client->dev, "can't find flash node\n");
		return -ENODEV;
	}

	rval = fwnode_property_read_string(flash->flash_node, "label", &name);
	if (!rval) {
		strlcpy(names->flash, name, sizeof(names->flash));
	} else if (is_of_node(fwnode)) {
		snprintf(names->flash, sizeof(names->flash),
			 "%s:flash", to_of_node(fwnode)->name);
	} else {
		dev_err(&flash->client->dev, "flash node has no label!\n");
		return -EINVAL;
	}

	rval = fwnode_property_read_u32(flash->flash_node, "flash-timeout-us",
				    &cfg->flash_timeout_us);
	if (rval < 0) {
		dev_err(&flash->client->dev,
			"can't read flash-timeout-us property for flash\n");
		cfg->flash_timeout_us = 1;
		//goto out_err;
	}

	rval = fwnode_property_read_u32(flash->flash_node, "flash-max-microamp",
				    &cfg->flash_max_ua);
	if (rval < 0) {
		dev_err(&flash->client->dev,
			"can't read flash-max-microamp property for flash\n");
		cfg->flash_max_ua = 1;
		//goto out_err;
	}

	rval = fwnode_property_read_u32(flash->flash_node, "led-max-microamp",
				    &cfg->assist_max_ua);
	if (rval < 0) {
		dev_err(&flash->client->dev,
			"can't read led-max-microamp property for flash\n");
		cfg->assist_max_ua = 1;
		//goto out_err;
	}

	fwnode_property_read_u32(flash->flash_node, "voltage-reference",
			     &cfg->voltage_reference);

	if (!flash->indicator_node) {
		dev_warn(&flash->client->dev,
			 "can't find indicator node\n");
		//goto out_err;
	}

	rval = fwnode_property_read_string(flash->indicator_node, "label", &name);
	if (!rval) {
		strlcpy(names->indicator, name, sizeof(names->indicator));
	} else if (is_of_node(fwnode)) {
		snprintf(names->indicator, sizeof(names->indicator),
			 "%s:indicator", to_of_node(fwnode)->name);
	} else {
		dev_err(&flash->client->dev, "flash node has no label!\n");
		return -EINVAL;
	}

	rval = fwnode_property_read_u32(flash->indicator_node, "led-max-microamp",
				    &cfg->indicator_max_ua);
	if (rval < 0) {
		dev_err(&flash->client->dev,
			"can't read led-max-microamp property for indicator\n");
		goto out_err;
	}

	printk("flash: indicator_max_ua %d, assist_max_ua %d, flash_max_ua %d\n",
	       cfg->indicator_max_ua, cfg->assist_max_ua, cfg->flash_max_ua);

	return 0;

out_err:
	fwnode_handle_put(flash->flash_node);
	fwnode_handle_put(flash->indicator_node);

	return rval;
}

static int fl_led_class_setup(struct fl *flash,
				   struct fl_names *names)
{
	struct led_classdev *fled_cdev = &flash->fled.led_cdev;
	struct led_classdev *iled_cdev = &flash->iled_cdev;
	struct led_flash_setting *cfg;
	int rval;

	iled_cdev->name = names->indicator;
	iled_cdev->brightness_set_blocking = fl_set_indicator_brightness;
	iled_cdev->max_brightness =
		flash->cfg.indicator_max_ua / flash->indicator_intensity_step;
	iled_cdev->flags = LED_CORE_SUSPENDRESUME;

	rval = led_classdev_register(&flash->client->dev, iled_cdev);
	if (rval < 0)
		return rval;

	cfg = &flash->fled.brightness;
	cfg->min = flash->flash_intensity_min;
	cfg->max = flash->cfg.flash_max_ua;
	cfg->step = flash->flash_intensity_step;
	cfg->val = flash->cfg.flash_max_ua;

	cfg = &flash->fled.timeout;
	cfg->min = flash->flash_timeout_min;
	cfg->max = flash->cfg.flash_timeout_us;
	cfg->step = flash->flash_timeout_step;
	cfg->val = flash->cfg.flash_timeout_us;

	flash->fled.ops = &fl_led_flash_ops;

	fled_cdev->name = names->flash;
	fled_cdev->brightness_set_blocking = fl_set_assist_brightness;
	/* Value 0 is off in LED class. */
	fled_cdev->max_brightness =
		fl_current_to_reg(flash, false,
				       flash->cfg.assist_max_ua) + 1;
	fled_cdev->flags = LED_DEV_CAP_FLASH | LED_CORE_SUSPENDRESUME;

	rval = led_classdev_flash_register(&flash->client->dev, &flash->fled);
	if (rval) {
		led_classdev_unregister(iled_cdev);
		dev_err(&flash->client->dev,
			"led_classdev_flash_register() failed, error %d\n",
			rval);
	}

	return rval;
}

static int fl_v4l2_setup(struct fl *flash)
{
	struct led_classdev_flash *fled = &flash->fled;
	struct led_classdev *led = &fled->led_cdev;
	struct v4l2_flash_config cfg = {
		.intensity = {
			.min = flash->torch_intensity_min,
			.max = flash->cfg.assist_max_ua,
			.step = flash->torch_intensity_step,
			.val = flash->cfg.assist_max_ua,
		},
	};
	struct v4l2_flash_config cfgind = {
		.intensity = {
			.min = flash->indicator_intensity_min,
			.max = flash->cfg.indicator_max_ua,
			.step = flash->indicator_intensity_step,
			.val = flash->cfg.indicator_max_ua,
		},
	};

	strlcpy(cfg.dev_name, led->name, sizeof(cfg.dev_name));
	strlcpy(cfgind.dev_name, flash->iled_cdev.name, sizeof(cfg.dev_name));

	flash->vf = v4l2_flash_init(
		&flash->client->dev, flash->flash_node, &flash->fled, NULL,
		&cfg);
	if (IS_ERR(flash->vf))
		return PTR_ERR(flash->vf);

	flash->vfind = v4l2_flash_indicator_init(
		&flash->client->dev, flash->indicator_node, &flash->iled_cdev,
		&cfgind);
	if (IS_ERR(flash->vfind)) {
		v4l2_flash_release(flash->vf);
		return PTR_ERR(flash->vfind);
	}

	return 0;
}

static int fl_probe(struct i2c_client *client)
{
	struct fl_names names;
	struct fl *flash;
	int rval;

	printk("flash: probe\n");
	if (!dev_fwnode(&client->dev))
		return -ENODEV;

	flash = devm_kzalloc(&client->dev, sizeof(*flash), GFP_KERNEL);
	if (flash == NULL)
		return -ENOMEM;

	flash->client = client;
	
	printk("flash: parse node\n");
	rval = fl_parse_node(flash, &names, dev_fwnode(&client->dev));
	if (rval < 0)
		return rval;

	printk("flash: detect\n");
	rval = fl_detect(flash);
	if (rval < 0)
		goto out_put_nodes;

	mutex_init(&flash->mutex);
	i2c_set_clientdata(client, flash);

	printk("flash: setup\n");	
	rval = fl_setup(flash);
	if (rval)
		goto out_mutex_destroy;

	flash->indicator_intensity_step = ADP1653_INDICATOR_INTENSITY_STEP;
	flash->flash_intensity_step = ADP1653_FLASH_INTENSITY_STEP;
	flash->flash_intensity_min = ADP1653_FLASH_INTENSITY_MIN;
	flash->torch_intensity_step = ADP1653_FLASH_INTENSITY_STEP;
	flash->torch_intensity_min = ADP1653_TORCH_INTENSITY_MIN;
	flash->flash_timeout_step = 1;

	printk("flash: led class setup\n");		
	rval = fl_led_class_setup(flash, &names);
	if (rval)
		goto out_mutex_destroy;

	printk("flash: v4l2 class setup\n");		
	rval = fl_v4l2_setup(flash);
	if (rval)
		goto out_led_classdev_flash_unregister;
	printk("flash: all done\n");			

	return 0;

out_led_classdev_flash_unregister:
	led_classdev_flash_unregister(&flash->fled);

out_mutex_destroy:
	mutex_destroy(&flash->mutex);

out_put_nodes:
	fwnode_handle_put(flash->flash_node);
	fwnode_handle_put(flash->indicator_node);

	return rval;
}

static int fl_remove(struct i2c_client *client)
{
	struct fl *flash = i2c_get_clientdata(client);

	flash->mode = AS_MODE_OFF;
	adp1653_update_hw(flash);

	v4l2_flash_release(flash->vf);

	led_classdev_flash_unregister(&flash->fled);
	led_classdev_unregister(&flash->iled_cdev);

	mutex_destroy(&flash->mutex);

	fwnode_handle_put(flash->flash_node);
	fwnode_handle_put(flash->indicator_node);

	return 0;
}

static const struct i2c_device_id fl_id_table[] = {
	{ ADP1653_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, fl_id_table);

static const struct of_device_id fl_of_table[] = {
	{ .compatible = "fixme?" },
	{ },
};
MODULE_DEVICE_TABLE(of, fl_of_table);

static struct i2c_driver fl_i2c_driver = {
	.driver	= {
		.of_match_table = fl_of_table,
		.name = ADP1653_NAME,
	},
	.probe_new	= fl_probe,
	.remove	= fl_remove,
	.id_table = fl_id_table,
};

module_i2c_driver(fl_i2c_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_AUTHOR("Sakari Ailus <sakari.ailus@iki.fi>");
MODULE_AUTHOR("Pavel Machek <pavel@ucw.cz>");
MODULE_DESCRIPTION("LED flash driver for ADP1653");
MODULE_LICENSE("GPL v2");
