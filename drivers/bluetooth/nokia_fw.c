/*
 * This file is part of hci_h4p bluetooth driver
 *
 * Copyright (C) 2005-2008 Nokia Corporation.
 * Copyright (C) 2014 Pavel Machek <pavel@ucw.cz>
 *
 * Contact: Ville Tervo <ville.tervo@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/firmware.h>
#include <linux/clk.h>

#include <net/bluetooth/bluetooth.h>

#include "hci_h4p.h"

#define FW_NAME_BCM2048		"bcmfw.bin"

static int fw_pos;

#define BT_DBG printk

int h4p_send_fw(struct h4p_info *info)
{
	unsigned long time;

	info->fw_error = 0;

	printk("Sending firmware (not really)\n");

	time = jiffies;
	printk("Firmware sent in %d msec\n",
		   jiffies_to_msecs(jiffies-time));

	h4p_set_auto_ctsrts(info, 0, UART_EFR_RTS);
	h4p_set_rts(info, 0);
	h4p_change_speed(info, BC4_MAX_BAUD_RATE);
	h4p_set_auto_ctsrts(info, 1, UART_EFR_RTS);

	printk("Going to final parameters\n");

	return 0;
}

/* Firmware handling */
static int h4p_open_firmware(struct h4p_info *info,
				 const struct firmware **fw_entry)
{
	int err;

	fw_pos = 0;
	BT_DBG("Opening firmware man_id 0x%.2x ver_id 0x%.2x",
			info->man_id, info->ver_id);
	switch (info->man_id) {
	case H4P_ID_BCM2048:
		/* We have this in N900 */
		printk("Firmware: BCM2048\n");
		err = request_firmware(fw_entry, FW_NAME_BCM2048, info->dev);
		break;
	default:
		dev_err(info->dev, "Invalid chip type: %x\n", info->man_id);
		*fw_entry = NULL;
		err = -EINVAL;
	}

	return err;
}

static void h4p_close_firmware(const struct firmware *fw_entry)
{
	release_firmware(fw_entry);
}

/* Read fw. Return length of the command. If no more commands in
 * fw 0 is returned. In error case return value is negative.
 */
static int h4p_read_fw_cmd(struct h4p_info *info, struct sk_buff **skb,
			       const struct firmware *fw_entry, gfp_t how)
{
	unsigned int cmd_len;
	static int num = 0;

	if (fw_pos >= fw_entry->size)
		return 0;

	if (fw_pos + 2 > fw_entry->size) {
		dev_err(info->dev, "Corrupted firmware image 1\n");
		return -EMSGSIZE;
	}

	cmd_len = fw_entry->data[fw_pos++];
	cmd_len += fw_entry->data[fw_pos++] << 8;
	if (cmd_len == 0)
		return 0;

	if (fw_pos + cmd_len > fw_entry->size) {
		dev_err(info->dev, "Corrupted firmware image 2\n");
		return -EMSGSIZE;
	}

	/* Note that this is timing-critical. If sending packets takes too
	   long, initialization will fail. */
	printk("Packet %d...", num);
	if (num > 1) {
		int cmd = fw_entry->data[fw_pos+1] + (fw_entry->data[fw_pos+2] << 8);
		int len = fw_entry->data[fw_pos+3];
		printk("cmd %x, len %d.", cmd, len);
		*skb = __hci_cmd_sync(info->hdev, cmd, len, fw_entry->data+fw_pos+4, 500);
		if (IS_ERR(*skb)) {
			printk("...sending cmd failed %d\n", PTR_ERR(*skb));
			return -EIO;
		}
	}
	num++;

	fw_pos += cmd_len;

	return 1;
}

int h4p_read_fw(struct h4p_info *info)
{
	const struct firmware *fw_entry = NULL;
	struct sk_buff *skb = NULL;
	int err;

	/*
	 * Disable smart-idle as UART TX interrupts
	 * are not wake-up capable
	 */
	h4p_smart_idle(info, 0);
	
	err = h4p_open_firmware(info, &fw_entry);
	if (err < 0 || !fw_entry)
		goto err_clean;

	printk("read firmware\n");
	/* FIXME: remove skb... */
	while ((err = h4p_read_fw_cmd(info, &skb, fw_entry, GFP_KERNEL))) {
	}

	printk("done read firmware\n");

err_clean:
	h4p_close_firmware(fw_entry);
	return err;
}

MODULE_FIRMWARE(FW_NAME_BCM2048);
