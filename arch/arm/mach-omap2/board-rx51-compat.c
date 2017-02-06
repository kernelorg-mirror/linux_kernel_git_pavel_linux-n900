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


