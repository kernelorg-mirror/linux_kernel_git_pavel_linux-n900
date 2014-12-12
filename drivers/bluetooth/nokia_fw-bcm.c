/*
 * This file is part of Nokia H4P bluetooth driver
 *
 * Copyright (C) 2005-2008 Nokia Corporation.
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

#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/serial_reg.h>

#include "hci_h4p.h"

static int hci_h4p_bcm_set_bdaddr(struct hci_h4p_info *info,
				struct sk_buff *skb)
{
	int i;
	static const u8 nokia_oui[3] = {0x00, 0x1f, 0xdf};
	int not_valid = !bacmp(&info->bd_addr, BDADDR_ANY);

	if (not_valid) {
		dev_info(info->dev, "Valid bluetooth address not found, setting some random\n");
		/* When address is not valid, use some random but Nokia MAC */
		memcpy(info->bd_addr.b, nokia_oui, 3);
		get_random_bytes(info->bd_addr.b + 3, 3);
	}

	for (i = 0; i < 6; i++)
		skb->data[9 - i] = info->bd_addr.b[i];

	return 0;
}

int hci_h4p_bcm_send_fw(struct hci_h4p_info *info,
			struct sk_buff_head *fw_queue)
{
	unsigned long time;

	info->fw_error = 0;

	printk("Sending firmware (not really)\n");

	time = jiffies;
	printk("Firmware sent in %d msec\n",
		   jiffies_to_msecs(jiffies-time));

	hci_h4p_set_auto_ctsrts(info, 0, UART_EFR_RTS);
	hci_h4p_set_rts(info, 0);
	hci_h4p_change_speed(info, BC4_MAX_BAUD_RATE);
	hci_h4p_set_auto_ctsrts(info, 1, UART_EFR_RTS);

	printk("Going to final parameters\n");

	return 0;
}
