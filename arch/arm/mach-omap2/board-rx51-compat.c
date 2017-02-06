/*
 * Maemo 5 compatibility driver for Nokia RX-51
 *
 * Copyright (C) 2004-2005 Nokia
 * Copyright (C) 2013      Pali Rohár <pali.rohar@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/i2c/twl4030-madc.h>
#include <asm/system_info.h>


/*** /proc/component_version ***/

struct version_config {
	char component[12];
	char version[12];
};

static struct version_config version_configs[] = {
	{"product", "RX-51"},
	{"hw-build", "2101"},
	{"nolo", "1.4.14"},
	{"boot-mode", "normal"},
};

static int component_version_show(struct seq_file *m, void *v)
{
	int i;
	const struct version_config *ver;

	for (i = 0; i < ARRAY_SIZE(version_configs); i++) {
		ver = &version_configs[i];
		seq_printf(m, "%-12s%s\n", ver->component, ver->version);
	}

	return 0;
}

static int component_version_open(struct inode *inode, struct file *file)
{
	return single_open(file, component_version_show, NULL);
}

static const struct file_operations component_version_fops = {
	.open		= component_version_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init component_version_init(void)
{
	if (system_rev != 0)
		snprintf(version_configs[1].version, 12, "%04x", system_rev);

	proc_create("component_version", S_IRUGO, NULL, &component_version_fops);
	return 0;
}

late_initcall(component_version_init);

