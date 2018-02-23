/*
 * Example of i2c flash LED driver
 *
 * Copyright (C) 2008-2011 Nokia Corporation
 * Copyright (c) 2011, 2017 Intel Corporation.
 *
 * Based on drivers/media/i2c/fl.c.
 *
 * Contact: Sakari Ailus <sakari.ailus@iki.fi>
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

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/led-class-flash.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/slab.h>

#include <media/v4l2-flash-led-class.h>

/* LED numbers for Devicetree */
#define AS_LED_FLASH		0
#define AS_LED_INDICATOR	1

enum as_mode {
	AS_MODE_EXT_TORCH = 0,
	AS_MODE_INDICATOR = 1,
	AS_MODE_ASSIST = 2,
	AS_MODE_FLASH = 3,
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
static int fl_set_current(struct fl *flash)
{
	printk("flash: set_current\n");

	//return fl_write(flash, AS_CURRENT_SET_REG, val);
	return -EINVAL;
}

static int fl_set_timeout(struct fl *flash)
{
	printk("flash: set_timeout\n");

	//return fl_write(flash, AS_INDICATOR_AND_TIMER_REG, val);
	return -EINVAL;
}

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
static int
fl_set_control(struct fl *flash, enum as_mode mode, bool on)
{
	printk("flash: set_control\n");

	//return fl_write(flash, AS_CONTROL_REG, reg);
}

static int __fl_get_fault(struct fl *flash, u32 *fault)
{
		printk("flash: get_fault\n");
		return 0;
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

	rval = fl_set_timeout(flash);
	if (rval)
		return rval;

	return fl_set_control(flash, AS_MODE_INDICATOR, brightness);
}

static int fl_set_assist_brightness(struct led_classdev *fled_cdev,
					 enum led_brightness brightness)
{
	struct led_classdev_flash *fled = lcdev_to_flcdev(fled_cdev);
	struct fl *flash = fled_to_fl(fled);
	int rval;

	if (brightness) {
		/* Register value 0 is 20 mA. */
		flash->assist_current = brightness - 1;

		rval = fl_set_current(flash);
		if (rval)
			return rval;
	}

	return fl_set_control(flash, AS_MODE_ASSIST, brightness);
}

static int fl_set_flash_brightness(struct led_classdev_flash *fled,
					u32 brightness_ua)
{
	struct fl *flash = fled_to_fl(fled);

	flash->flash_current = fl_current_to_reg(flash, true, brightness_ua);

	return fl_set_current(flash);
}

static int fl_set_flash_timeout(struct led_classdev_flash *fled,
				     u32 timeout_us)
{
	struct fl *flash = fled_to_fl(fled);

	flash->timeout = 0; // FIXME AS_TIMER_US_TO_CODE(timeout_us);

	return fl_set_timeout(flash);
}

static int fl_set_strobe(struct led_classdev_flash *fled, bool state)
{
	struct fl *flash = fled_to_fl(fled);

	return fl_set_control(flash, AS_MODE_FLASH, state);
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
	rval = fl_get_fault(flash, &fault);
	if (rval < 0)
		return rval;

	dev_dbg(dev, "Fault info: %02x\n", rval);

	rval = fl_set_current(flash);
	if (rval < 0)
		return rval;

	rval = fl_set_timeout(flash);
	if (rval < 0)
		return rval;

	rval = fl_set_control(flash, AS_MODE_INDICATOR, false);
	if (rval < 0)
		return rval;

	/* read status */
	rval = fl_get_fault(&flash->fled, &fault);
	if (rval < 0)
		return rval;

	return rval ? -EIO : 0;
}

static int fl_detect(struct fl *flash)
{
	struct device *dev = &flash->client->dev;
	int rval, man, model, rfu, version;
	const char *vendor;

	//return fl_write(flash, AS_BOOST_REG, AS_BOOST_CURRENT_DISABLE);
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
		goto out_err;
	}

	rval = fwnode_property_read_u32(flash->flash_node, "flash-max-microamp",
				    &cfg->flash_max_ua);
	if (rval < 0) {
		dev_err(&flash->client->dev,
			"can't read flash-max-microamp property for flash\n");
		goto out_err;
	}

	rval = fwnode_property_read_u32(flash->flash_node, "led-max-microamp",
				    &cfg->assist_max_ua);
	if (rval < 0) {
		dev_err(&flash->client->dev,
			"can't read led-max-microamp property for flash\n");
		goto out_err;
	}

	fwnode_property_read_u32(flash->flash_node, "voltage-reference",
			     &cfg->voltage_reference);

	if (!flash->indicator_node) {
		dev_warn(&flash->client->dev,
			 "can't find indicator node\n");
		goto out_err;
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

	if (!dev_fwnode(&client->dev))
		return -ENODEV;

	flash = devm_kzalloc(&client->dev, sizeof(*flash), GFP_KERNEL);
	if (flash == NULL)
		return -ENOMEM;

	flash->client = client;

	rval = fl_parse_node(flash, &names, dev_fwnode(&client->dev));
	if (rval < 0)
		return rval;

	rval = fl_detect(flash);
	if (rval < 0)
		goto out_put_nodes;

	mutex_init(&flash->mutex);
	i2c_set_clientdata(client, flash);

	rval = fl_setup(flash);
	if (rval)
		goto out_mutex_destroy;

	rval = fl_led_class_setup(flash, &names);
	if (rval)
		goto out_mutex_destroy;

	rval = fl_v4l2_setup(flash);
	if (rval)
		goto out_led_classdev_flash_unregister;

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

	fl_set_control(flash, AS_MODE_EXT_TORCH, false);

	v4l2_flash_release(flash->vf);

	led_classdev_flash_unregister(&flash->fled);
	led_classdev_unregister(&flash->iled_cdev);

	mutex_destroy(&flash->mutex);

	fwnode_handle_put(flash->flash_node);
	fwnode_handle_put(flash->indicator_node);

	return 0;
}

static const struct i2c_device_id fl_id_table[] = {
	{ "fixme", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, fl_id_table);

static const struct of_device_id fl_of_table[] = {
	{ .compatible = "ams,fl" },
	{ },
};
MODULE_DEVICE_TABLE(of, fl_of_table);

static struct i2c_driver fl_i2c_driver = {
	.driver	= {
		.of_match_table = fl_of_table,
		.name = "fixme",
	},
	.probe_new	= fl_probe,
	.remove	= fl_remove,
	.id_table = fl_id_table,
};

module_i2c_driver(fl_i2c_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_AUTHOR("Sakari Ailus <sakari.ailus@iki.fi>");
MODULE_DESCRIPTION("LED flash driver for FL, LM3555 and their clones");
MODULE_LICENSE("GPL v2");
