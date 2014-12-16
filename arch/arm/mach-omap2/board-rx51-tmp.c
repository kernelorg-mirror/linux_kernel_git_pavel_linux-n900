#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/serial_reg.h>
#include <linux/skbuff.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/io.h>
#include <linux/completion.h>
#include <linux/sizes.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci.h>

#include "common.h"
#include <linux/omap-dma.h>
#include "gpmc-smc91x.h"
#include <linux/platform_data/ssi.h>

#include "board-rx51.h"
#include "serial.h"

#include <sound/tlv320aic3x.h>
#include <sound/tpa6130a2-plat.h>
#include <media/radio-si4713.h>
#include <media/si4713.h>
#include "../../../drivers/staging/media/bcm2048/radio-bcm2048.h"
#include <linux/platform_data/leds-lp55xx.h>

#include <linux/platform_data/tsl2563.h>
#include <linux/lis3lv02d.h>

#include <video/omap-panel-data.h>

#if defined(CONFIG_IR_RX51) || defined(CONFIG_IR_RX51_MODULE)
#include <media/ir-rx51.h>
#endif

#include <media/et8ek8.h>
#include <media/smiapp.h>
#include <media/ad5820.h>
#include <media/adp1653.h>

#include <plat/gpio-switch.h>

#include "mux.h"
#include "omap-pm.h"
#include "hsmmc.h"
#include "common-board-devices.h"
#include "gpmc.h"
#include "gpmc-onenand.h"
#include "soc.h"
#include "omap-secure.h"

#if 0
/* Allow C6 state {1, 3120, 5788, 10000} */
#define H4P_WAKEUP_LATENCY	5700

/* Use wakeup latency only for now */
static void rx51_bt_set_pm_limits(struct device *dev, bool set)
{
	omap_pm_set_max_mpu_wakeup_lat(dev, set ? H4P_WAKEUP_LATENCY : -1);
}

#define RX51_HCI_H4P_RESET_GPIO		91
#define RX51_HCI_H4P_HOSTWU_GPIO	101
#define RX51_HCI_H4P_BTWU_GPIO		37

struct h4p_platform_data bt_plat_data = {
	.chip_type		= 3,
	.bt_sysclk		= 2,
	.bt_wakeup_gpio		= RX51_HCI_H4P_BTWU_GPIO,
	.host_wakeup_gpio	= RX51_HCI_H4P_HOSTWU_GPIO,
	.reset_gpio		= RX51_HCI_H4P_RESET_GPIO,
	.reset_gpio_shared	= 0,
//	.uart_irq		= 73 + OMAP_INTC_START,
	/* It seems to be 223 in hci_h4p case */
	.uart_irq		= 223,
	.uart_base		= OMAP3_UART2_BASE,
	.uart_iclk		= "uart2_ick",
	.uart_fclk		= "uart2_fck",
	.set_pm_limits		= rx51_bt_set_pm_limits,
};

static struct platform_device rx51_bt_device = {
	.name		= "hci_h4p",
	.id		= -1,
	.num_resources	= 0,
	.dev = {
		.platform_data = &bt_plat_data,
	}
};

int rx51_bt_init(void *foo)
{
	platform_device_register(&rx51_bt_device);
	return 0;
}
#endif

#define ADP1653_GPIO_ENABLE	88	/* Used for resetting ADP1653 */
#define ADP1653_GPIO_INT	167	/* Fault interrupt */
#define ADP1653_GPIO_STROBE	126	/* Pin used in cam_strobe mode ->
					 * control using ISP drivers */

#define STINGRAY_RESET_GPIO	102
#define ACMELITE_RESET_GPIO	97	/* Used also to MUX between cameras */

#define RX51_CAMERA_STINGRAY	(1 << 0)
#define RX51_CAMERA_LENS	(1 << 1)
#define RX51_CAMERA_ACMELITE	(1 << 2)

#define RX51_CAMERA_PRIMARY	(RX51_CAMERA_STINGRAY | RX51_CAMERA_LENS)
#define RX51_CAMERA_SECONDARY	RX51_CAMERA_ACMELITE

static DEFINE_MUTEX(rx51_camera_mutex);
static unsigned int rx51_camera_xshutdown;

static int rx51_camera_set_xshutdown(unsigned int which, int set)
{
	unsigned int new;
	int ret = 0;

	mutex_lock(&rx51_camera_mutex);

	new = rx51_camera_xshutdown;

	if (set)
		new |= which;
	else
		new &= ~which;

	/* The primary and secondary cameras can't be powered on at the same
	 * time.
	 */
	if ((new & RX51_CAMERA_PRIMARY) && (new & RX51_CAMERA_SECONDARY)) {
		ret = -EBUSY;
		goto out;
	}

	if ((rx51_camera_xshutdown & RX51_CAMERA_PRIMARY) !=
	    (new & RX51_CAMERA_PRIMARY))
		gpio_set_value(STINGRAY_RESET_GPIO,
			       new & RX51_CAMERA_PRIMARY ? 1 : 0);

	if ((rx51_camera_xshutdown & RX51_CAMERA_SECONDARY) !=
	    (new & RX51_CAMERA_SECONDARY))
		gpio_set_value(ACMELITE_RESET_GPIO,
			       new & RX51_CAMERA_SECONDARY ? 1 : 0);

	rx51_camera_xshutdown = new;

out:
	mutex_unlock(&rx51_camera_mutex);
	return ret;
}

struct et8ek8_platform_data rx51_et8ek8_platform_data = {
        .set_xshutdown          = rx51_camera_set_xshutdown,
};

static void __init rx51_stingray_init(void)
{
	if (gpio_request(STINGRAY_RESET_GPIO, "stingray reset") != 0) {
		printk(KERN_INFO "%s: unable to acquire Stingray reset gpio\n",
		       __FUNCTION__);
		return;
	}

	/* XSHUTDOWN off, reset  */
	gpio_direction_output(STINGRAY_RESET_GPIO, 0);
	gpio_set_value(STINGRAY_RESET_GPIO, 0);
}

static void __init rx51_acmelite_init(void)
{
	if (gpio_request(ACMELITE_RESET_GPIO, "acmelite reset") != 0) {
		printk(KERN_INFO "%s: unable to acquire Acme Lite reset gpio\n",
		       __FUNCTION__);
		return;
	}

	/* XSHUTDOWN off, reset  */
	gpio_direction_output(ACMELITE_RESET_GPIO, 0);
	gpio_set_value(ACMELITE_RESET_GPIO, 0);
}

static int __init rx51_adp1653_init(void)
{
	int err;

	err = gpio_request(ADP1653_GPIO_ENABLE, "adp1653 enable");
	if (err) {
		printk(KERN_ERR ADP1653_NAME
		       " Failed to request EN gpio\n");
		err = -ENODEV;
		goto err_omap_request_gpio;
	}

	err = gpio_request(ADP1653_GPIO_INT, "adp1653 interrupt");
	if (err) {
		printk(KERN_ERR ADP1653_NAME " Failed to request IRQ gpio\n");
		err = -ENODEV;
		goto err_omap_request_gpio_2;
	}

	err = gpio_request(ADP1653_GPIO_STROBE, "adp1653 strobe");
	if (err) {
		printk(KERN_ERR ADP1653_NAME
		       " Failed to request STROBE gpio\n");
		err = -ENODEV;
		goto err_omap_request_gpio_3;
	}

	gpio_direction_output(ADP1653_GPIO_ENABLE, 0);
	gpio_direction_input(ADP1653_GPIO_INT);
	gpio_direction_output(ADP1653_GPIO_STROBE, 0);

	return 0;

err_omap_request_gpio_3:
	gpio_free(ADP1653_GPIO_INT);

err_omap_request_gpio_2:
	gpio_free(ADP1653_GPIO_ENABLE);

err_omap_request_gpio:
	return err;
}

static int __init rx51_camera_hw_init(void)
{
	int rval;

	mutex_init(&rx51_camera_mutex);

	rval = rx51_adp1653_init();
	if (rval)
		return rval;

	rx51_stingray_init();
	rx51_acmelite_init();

	return 0;
}

//__initcall(rx51_bt_init);
__initcall(rx51_camera_hw_init);
