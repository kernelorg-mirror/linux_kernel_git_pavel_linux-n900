/*
 * Arbitrary pattern trigger
 *
 * Copyright 2015, Epsiline
 *
 * Author : Raphaël Teysseyre <rteysseyre@gmail.com>
 *
 * Idea discussed with Pavel Machek <pavel@ucw.cz> on
 * <linux-leds@vger.kernel.org> (march 2015, thread title
 * [PATCH RFC] leds: Add status code trigger)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include "../leds.h"

struct pattern_trig_data {
	struct led_classdev *led_cdev;

	struct led_pattern *steps; /* Array describing the pattern */
	struct mutex lock;
	char is_sane;
	struct led_pattern *curr;
	struct led_pattern *next;
	int delta_t; /* Time in current step */
	int nsteps;  /* Number of steps */
	int repeat;  /* < 0 means repeat indefinitely */
	struct timer_list timer;
};

#define MAX_NSTEPS (PAGE_SIZE/4)
/* The "pattern" attribute contains at most PAGE_SIZE characters.
   Each line in this attribute is at least 4 characters long
   (a 1-digit number for the led brighntess, a space,
   a 1-digit number for the time, a semi-colon).
   Therefore, there is at most PAGE_SIZE/4 steps. */

#define UPDATE_INTERVAL 50
/* When doing gradual dimming, the led brightness
   will be updated every UPDATE_INTERVAL milliseconds */

#define PATTERN_SEPARATOR ","

static int pattern_trig_initialize_data(struct pattern_trig_data *data)
{
	mutex_init(&data->lock);
	mutex_lock(&data->lock);

	data->is_sane = 0;
	data->steps = kzalloc(MAX_NSTEPS*sizeof(struct led_pattern),
			GFP_KERNEL);
	if (!data->steps)
		return -ENOMEM;

	data->curr = NULL;
	data->next = NULL;
	data->delta_t = 0;
	data->nsteps = 0;
	data->repeat = -1;
	//data->timer = __TIMER_INITIALIZER(NULL, 0);

	mutex_unlock(&data->lock);
	return 0;
}

static void pattern_trig_clear_data(struct pattern_trig_data *data)
{
	data->is_sane = 0;
	kfree(data->steps);
}

/*
 *  is_sane : pattern checking.
 *  A pattern satisfying these three conditions is reported as sane :
 *    - At least two steps
 *    - At least one step with time >= UPDATE_INTERVAL
 *    - At least two steps with differing brightnesses
 *  When @data isn't sane, a sensible brightness
 *  default is suggested in @brightness
 *
 * DO NOT call pattern_trig_update on a not-sane pattern,
 * you'll be punished with an infinite loop in the kernel.
 */
static int is_sane(struct pattern_trig_data *data, int *brightness)
{
	int i;
	char stept_ok = 0;
	char stepb_ok = 0;

	*brightness = 0;
	if (data->nsteps < 1)
		return 0;

	*brightness = data->steps[0].brightness;
	if (data->nsteps < 2)
		return 0;

	for (i = 0; i < data->nsteps; i++) {
		if (data->steps[i].delta_t >= UPDATE_INTERVAL) {
			/* FIXME: this is wrong */
			if (stepb_ok)
				return 1;
			stept_ok = 1;
		}
		if (data->steps[i].brightness != data->steps[0].brightness) {
			if (stept_ok)
				return 1;
			stepb_ok = 1;
		}
	}

	return 0;
}

static void reset_pattern(struct pattern_trig_data *data,
			struct led_classdev *led_cdev)
{
	int brightness;

	if (led_cdev->pattern_clear) {
		led_cdev->pattern_clear(led_cdev);
	}

	del_timer_sync(&data->timer);

	if (led_cdev->pattern_set && led_cdev->pattern_set(led_cdev, data->steps, data->nsteps)) {
		return;
	}

	if (!is_sane(data, &brightness)) {
		led_set_brightness(led_cdev, brightness);
		return;
	}

	data->curr = data->steps;
	data->next = data->steps + 1;
	data->delta_t = 0;
	data->is_sane = 1;

	data->timer.expires = jiffies;
	add_timer(&data->timer);
}

/* --- Sysfs handling --- */

static ssize_t pattern_trig_show_repeat(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct pattern_trig_data *data = led_cdev->trigger_data;

	return scnprintf(buf, PAGE_SIZE, "%d\n", data->repeat);
}

static ssize_t pattern_trig_store_repeat(
	struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct pattern_trig_data *data = led_cdev->trigger_data;
	long res;
	int err;

	err = kstrtol(buf, 10, &res);
	if (err)
		return err;

	data->repeat = res < 0 ? -1 : res;
	reset_pattern(data, led_cdev);

	return count;
}

DEVICE_ATTR(repeat, S_IRUGO | S_IWUSR,
	pattern_trig_show_repeat, pattern_trig_store_repeat);

static ssize_t pattern_trig_show_pattern(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct pattern_trig_data *data = led_cdev->trigger_data;
	ssize_t count = 0;
	int i;

	if (!data->steps || !data->nsteps)
		return 0;

	for (i = 0; i < data->nsteps; i++)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				"%d %d" PATTERN_SEPARATOR,
				data->steps[i].brightness,
				data->steps[i].delta_t);
	buf[count - 1] = '\n';
	buf[count] = '\0';

	return count + 1;
}

static ssize_t pattern_trig_store_pattern(
	struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct pattern_trig_data *data = led_cdev->trigger_data;
	int cr = 0; /* Characters read on one conversion */
	int tcr = 0; /* Total characters read */
	int ccount; /* Number of successful conversions */

	mutex_lock(&data->lock);
	data->is_sane = 0;

	for (data->nsteps = 0; data->nsteps < MAX_NSTEPS; data->nsteps++) {
		cr = 0;
		ccount = sscanf(buf + tcr, "%d %d " PATTERN_SEPARATOR "%n",
			&data->steps[data->nsteps].brightness,
			&data->steps[data->nsteps].delta_t, &cr);

		if (!cr) { /* Invalid syntax or end of pattern */
			if (ccount == 2)
				data->nsteps++;
			mutex_unlock(&data->lock);
			reset_pattern(data, led_cdev);
			return count;
		}

		tcr += cr;
	}

	/* Shouldn't reach that */
	WARN(1, "MAX_NSTEP too small. Please report\n");
	mutex_unlock(&data->lock);
	return count;
}

DEVICE_ATTR(pattern, S_IRUGO | S_IWUSR,
	pattern_trig_show_pattern, pattern_trig_store_pattern);

static int pattern_trig_create_sysfs_files(struct device *dev)
{
	int err;

	err = device_create_file(dev, &dev_attr_repeat);
	if (err)
		return err;

	err = device_create_file(dev, &dev_attr_pattern);
	if (err)
		device_remove_file(dev, &dev_attr_repeat);

	return err;
}

static void pattern_trig_remove_sysfs_files(struct device *dev)
{
	device_remove_file(dev, &dev_attr_pattern);
	device_remove_file(dev, &dev_attr_repeat);
}

/* --- Led intensity updating --- */

static int compute_brightness(struct pattern_trig_data *data)
{
	if (data->delta_t == 0)
		return data->curr->brightness;

	if (data->curr->delta_t == 0)
		return data->next->brightness;

	return data->curr->brightness + data->delta_t
		* (data->next->brightness - data->curr->brightness)
		/ data->curr->delta_t;
}

static void update_to_next_step(struct pattern_trig_data *data)
{
	data->curr = data->next;
	if (data->curr == data->steps)
		data->repeat--;

	if (data->next == data->steps + data->nsteps - 1)
		data->next = data->steps;
	else
		data->next++;

	data->delta_t = 0;
}

static void pattern_trig_update(struct timer_list *t)
{
	struct pattern_trig_data *data = from_timer(data, t, timer);

	mutex_lock(&data->lock);

	if (!data->is_sane || !data->repeat) {
		mutex_unlock(&data->lock);
		return;
	}

	if (data->delta_t > data->curr->delta_t)
		update_to_next_step(data);

	/* is_sane() checked that there is at least
	   one step with delta_t >= UPDATE_INTERVAL
	   so we won't go in an infinite loop */
	while (data->curr->delta_t < UPDATE_INTERVAL)
		update_to_next_step(data);

	if (data->next->brightness == data->curr->brightness) {
		/* Constant brightness for this step */
		led_set_brightness(data->led_cdev, data->curr->brightness);
		mod_timer(&data->timer, jiffies
			+ msecs_to_jiffies(data->curr->delta_t));
		update_to_next_step(data);
	} else {
		/* Gradual dimming */
		led_set_brightness(data->led_cdev, compute_brightness(data));
		data->delta_t += UPDATE_INTERVAL;
		mod_timer(&data->timer, jiffies
			+ msecs_to_jiffies(UPDATE_INTERVAL));
	}

	mutex_unlock(&data->lock);
}

/* --- Trigger activation --- */

static void pattern_trig_activate(struct led_classdev *led_cdev)
{
	struct pattern_trig_data *data = NULL;
	int err;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return;

	err = pattern_trig_initialize_data(data);
	if (err) {
		kfree(data);
		return;
	}

	data->led_cdev = led_cdev;
	led_cdev->trigger_data = data;
	timer_setup(&data->timer, pattern_trig_update, 0);
	pattern_trig_create_sysfs_files(led_cdev->dev);
}

static void pattern_trig_deactivate(struct led_classdev *led_cdev)
{
	struct pattern_trig_data *data = led_cdev->trigger_data;

	if (data) {
		pattern_trig_remove_sysfs_files(led_cdev->dev);
		del_timer_sync(&data->timer);
		led_set_brightness(led_cdev, LED_OFF);
		pattern_trig_clear_data(data);
		kfree(data);
		led_cdev->trigger_data = NULL;
	}
}

static struct led_trigger pattern_led_trigger = {
	.name = "pattern",
	.activate = pattern_trig_activate,
	.deactivate = pattern_trig_deactivate,
};

/* --- Module loading/unloading --- */

static int __init pattern_trig_init(void)
{
	return led_trigger_register(&pattern_led_trigger);
}

static void __exit pattern_trig_exit(void)
{
	led_trigger_unregister(&pattern_led_trigger);
}

module_init(pattern_trig_init);
module_exit(pattern_trig_exit);

MODULE_AUTHOR("Raphael Teysseyre <rteysseyre@gmail.com");
MODULE_DESCRIPTION("Pattern LED trigger");
MODULE_LICENSE("GPL");
