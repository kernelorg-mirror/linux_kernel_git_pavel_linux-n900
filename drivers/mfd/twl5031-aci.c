/*
 * This file is part of twl5031 ACI (Accessory Control Interface) driver
 *
 * Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Tapio Vihuri <tapio.vihuri@nokia.com>
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

/* #define DEBUG */
/* #define VERBOSE_DEBUG */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/mfd/twl.h>
#include <linux/mfd/twl4030-audio.h>
#include <linux/mfd/twl5031-aci.h>
#include <linux/input/eci.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <sound/jack.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <sound/n9.h>

#define ACI_DRIVERNAME	"twl5031_aci"

/*
 * plug need to be inserted fully before voltage is valid
 * and user is slow
 */
#define ACI_WAIT_PLUG_ON_MIN		200	/* ms */
#define ACI_WAIT_PLUG_ON_MAX		1000	/* ms */
#define ACI_WAIT_PLUG_ON_DEFAULT	500	/* ms */
#define ACI_WAIT_PLUG_TO_BUTTONS	200	/* ms */
#define ACI_WAIT_CHECK_BUTTONS		20	/* ms */

#define ACI_WAIT_PLUG_OFF		20	/* ms */
#define ACI_WAIT_BUTTON			20	/* ms */
#define ACI_WAIT_IRQ			100	/* ms */
#define ACI_BUS_TIMEOUT			20	/* ms */
#define ACI_BUS_SETTLE			25	/* ms */
#define ACI_COMA_TIMEOUT		20	/* ms */
#define ACI_WATCHDOG			200	/* ms */

#define ACI_OPEN_CABLE_REDETECT_TIMER	1000	/* ms */
#define ACI_DETECTS_NEEDED		2
#define ACI_OPEN_CABLE_REDETECTS_NEEDED	(ACI_DETECTS_NEEDED * 2)
#define ACI_OPEN_CABLE_REDETECTS	3
#define ACI_REDETECTS_BAIL_OUT		(ACI_OPEN_CABLE_REDETECTS_NEEDED * \
						ACI_OPEN_CABLE_REDETECTS * 2)

#define ACI_BUTTON_TRESHOLD		200	/* mV */
#define ACI_HEADPHONE_TRESHOLD		40	/* mV */
#define ACI_AVOUT_TRESHOLD		500	/* mV */
#define ACI_AHJ_NHJ_TRESHOLD		100	/* mV */
#define ACI_BASIC_HEADSET_TRESHOLD	200	/* mV */
#define ACI_BASIC_CARKIT_TRESHOLD	1075	/* mV */
#define ACI_WAIT_VOLTAGE_SETTLING	20	/* ms */
#define ACI_WAIT_VOLTAGE_SETTLING_LO	50000	/* us */
#define ACI_WAIT_VOLTAGE_SETTLING_HI	52000	/* us */
#define ACI_WAIT_COMPARATOR_SETTLING_LO	100	/* us */
#define ACI_WAIT_COMPARATOR_SETTLING_HI	200	/* us */
#define ACI_AVOUT_DETECTION_DELAY	200	/* ms */

#define ACI_BUTTON_BUF_SIZE		32

#define AV_ABOVE_600mV			0
#define AV_BELOW_600mV			STATUS_A1_COMP

#define ECI_IO_CMD			2
#define ECI_DIRECT_MEM			1
#define ECI_IO_CMD_READ			1
#define ECI_IO_CMD_WRITE		3

#define ECI_REAL_BUTTONS	false
#define ECI_FORCE_BUTTONS_UP	true
#define ECI_BUTTONS_LATCHED	true
#define ECI_BUTTONS_NOT_LATCHED	false

#define ACI_DETECTION_NONE	0
#define ACI_DETECTION_BIAS_ON	1

#define IO_READ_RETRY		3

#define MADC_ADCIN10_PRESCALE(x)	((x) * 15 / 10)

/* fixed in HW, do not change */
enum {
	ACICMD_NOP,
	ACICMD_RST_LEARN,
	ACICMD_SEND,
	ACICMD_RECEIVE,
	ACICMD_RECEIVE_LEARN,
	NO_ACICMD,
};

enum {
	ECI_NOP,
	ECI_RST_LEARN,
	ECI_WRITE_IO,
	ECI_READ_IO,
	ECI_READ_DIRECT,
	ECI_OK,
	ECI_ERROR,
};

enum {
	ACI_TASK_NONE,
	ACI_TASK_ENABLE_BUTTON,
	ACI_TASK_GET_BUTTON,
	ACI_TASK_SEND_BUTTON,
	ACI_TASK_DETECTION,
};

enum {
	ACI_AUDIO,
	ACI_VIDEO,
};

/* Do not change order */
enum {
	ACI_BT_UP,
	ACI_BT_DOWN,
	ACI_BT_NONE,
};

enum {
	ACI_NOTYPE,
	ACI_UNKNOWN,
	ACI_HEADPHONE,
	ACI_AVOUT,
	ACI_HEADSET,
	ACI_ECI_HEADSET,
	ACI_OPEN_CABLE,
	ACI_CARKIT,
};

struct aci_irqs {
	u8 accint;
	u8 drec;
	u8 dsent;
	u8 spdset;
	u8 commerr;
	u8 fraerr;
	u8 reserr;
	u8 coll;
	u8 nopint;
};

struct aci_buf {
	u8	*buf;
	int	count;
};

struct twl5031_aci {
	struct device		*dev;
	struct workqueue_struct *aci_wq;
	struct delayed_work	aci_work;
	struct delayed_work	plug_work;
	struct delayed_work	wd_work;
	struct work_struct	irq_work;
	wait_queue_head_t	wait;
	struct mutex		lock;	/* ACI transaction */
	spinlock_t			irqlock;
	struct mutex		iolock; /* IO reg read */
	struct mutex		acc_block; /* accessory block operations */
	int			tvout_gpio;
	int			jack_gpio;
	struct snd_soc_codec *codec;
	struct regulator *v28;
	int			v28_enabled;
	int			vhsmicout;

	bool		plugged;
	int			avplugdet_plugged;

	int			avswitch;
	int			task;
	int			bias_level;
	char			*detected;
	int			detection_latency;
	int			detection_phase;
	int			detection_count;
	bool			open_cable_redetect_enabled;
	int			open_cable_redetects;
	int			accessory;
	int			previous;
	int			count;
	int			needed;
	int			button;
	u8			buttons_buf[ACI_BUTTON_BUF_SIZE];
	u8			windex;
	u8			rindex;
	int			eci_state;
	int			version;
	struct aci_buf		data;
	u8			buf[4];
	int			cmd;
	int			op;
	unsigned long		io_timeout;
	u16			irq_bits;
	u8			io_read_ongoing;
	u8			eci_cmd;
	struct aci_cb	*aci_callback;
	struct eci_cb	*eci_callback;
	struct aci_irqs		irqs;
	bool			use_accint;
	/* MADC measure stuff */
	u16			rbuf[TWL4030_MADC_MAX_CHANNELS];

	int			(*hw_plug_set_state)(struct device *dev,
						bool plugged);
	struct dev_pm_qos_request 	qos_request_acicmd;
	struct dev_pm_qos_request 	qos_request_accint;
};

static int button = ACI_BT_UP;
static struct twl5031_aci *the_aci;

static int twl5031_aci_read(u8 reg);
static void twl5031_aci_write(u8 reg, u8 val);
static int twl5031_v_hs_mic_out(struct twl5031_aci *aci);
static void twl5031_aci_set_av_output(struct twl5031_aci *aci, int audio);

#ifdef CONFIG_DEBUG_FS
static void twl5031_av_plug_on_off(struct twl5031_aci *aci);

static struct dentry *aci_debugfs_dir;

static void aci_dev_pm_qos_remove_request(struct dev_pm_qos_request *request)
{
	if (request->dev) {
		dev_pm_qos_remove_request(request);
		request->dev = NULL;
	}
}

static ssize_t detected_read(struct file *file, char __user *user_buf,
		size_t count, loff_t *ppos)
{
	struct twl5031_aci *aci = file->private_data;
	char buffer[128];
	int len;

	len = snprintf(buffer, sizeof(buffer), "%s\n", aci->detected);
	return simple_read_from_buffer(user_buf, count, ppos, buffer, len);
}

static ssize_t vhsmicout_read(struct file *file, char __user *user_buf,
		size_t count, loff_t *ppos)
{
	struct twl5031_aci *aci = file->private_data;
	char buffer[128];
	int len;

	len = snprintf(buffer, sizeof(buffer), "vhsmicout %04d mV\n",
		twl5031_v_hs_mic_out(aci));
	return simple_read_from_buffer(user_buf, count, ppos, buffer, len);
}

static ssize_t cable_read(struct file *file, char __user *user_buf,
		size_t count, loff_t *ppos)
{
	struct twl5031_aci *aci = file->private_data;
	char buffer[128];
	int len;

	len = snprintf(buffer, sizeof(buffer), "Cable plugged %s\n",
		aci->plugged ? "in" : "out");
	return simple_read_from_buffer(user_buf, count, ppos, buffer, len);
}

static ssize_t cable_write(struct file *file, const char __user *user_buf,
		size_t count, loff_t *ppos)
{
	struct twl5031_aci *aci = file->private_data;
	char buf[32];
	int buf_size;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (!memcmp(buf, "in", 2))
		aci->plugged = true;
	else if (!memcmp(buf, "out", 3))
		aci->plugged = false;

	twl5031_av_plug_on_off(aci);

	return count;
}

static ssize_t avswitch_read(struct file *file, char __user *user_buf,
		size_t count, loff_t *ppos)
{
	struct twl5031_aci *aci = file->private_data;
	char buffer[128];
	int len;

	switch (aci->avswitch) {
	case ACI_AUDIO:
		len = snprintf(buffer, sizeof(buffer), "avswitch audio\n");
		break;
	case ACI_VIDEO:
		len = snprintf(buffer, sizeof(buffer), "avswitch video\n");
		break;
	default:
		len = snprintf(buffer, sizeof(buffer),
			"ERROR avswitch unknown state\n");
		break;
	}
	return simple_read_from_buffer(user_buf, count, ppos, buffer, len);
}

static ssize_t avswitch_write(struct file *file, const char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct twl5031_aci *aci = file->private_data;
	char buf[32];
	ssize_t buf_size;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (!memcmp(buf, "audio", 5))
		aci->avswitch = ACI_AUDIO;
	else if (!memcmp(buf, "video", 5))
		aci->avswitch = ACI_VIDEO;

	twl5031_aci_set_av_output(aci, aci->avswitch);

	return count;
}

static int default_open(struct inode *inode, struct file *file)
{
	if (inode->i_private)
		file->private_data = inode->i_private;

	return 0;
}

static const struct file_operations detected_fops = {
	.open		= default_open,
	.read		= detected_read,
};

static const struct file_operations vhsmicout_fops = {
	.open		= default_open,
	.read		= vhsmicout_read,
};

static const struct file_operations cable_fops = {
	.open		= default_open,
	.read		= cable_read,
	.write		= cable_write,
};

static const struct file_operations avswitch_fops = {
	.open		= default_open,
	.read		= avswitch_read,
	.write		= avswitch_write,
};

static void aci_uninitialize_debugfs(void)
{
	if (aci_debugfs_dir)
		debugfs_remove_recursive(aci_debugfs_dir);
}

static long aci_initialize_debugfs(struct twl5031_aci *aci)
{
	void *ok;

	/* /sys/kernel/debug/twl5031_aci */
	aci_debugfs_dir = debugfs_create_dir(dev_name(aci->dev), NULL);
	if (IS_ERR(aci_debugfs_dir)) {
		/* assume -ENODEV */
		dev_err(aci->dev, "debugfs not enabled: %d\n", __LINE__);
		return PTR_ERR(aci_debugfs_dir);
	}
	if (!aci_debugfs_dir)
		return 0;

	ok = debugfs_create_file("detected", S_IRUGO, aci_debugfs_dir,
			aci, &detected_fops);
	if (!ok)
		goto fail;
	ok = debugfs_create_file("vhsmicout", S_IRUGO, aci_debugfs_dir,
			aci, &vhsmicout_fops);
	if (!ok)
		goto fail;
	ok = debugfs_create_file("cable", S_IRUGO | S_IWUSR,
			aci_debugfs_dir, aci, &cable_fops);
	if (!ok)
		goto fail;
	ok = debugfs_create_file("avswitch", S_IRUGO | S_IWUSR, aci_debugfs_dir,
			aci, &avswitch_fops);
	if (!ok)
		goto fail;

	return PTR_ERR(ok);
fail:
	aci_uninitialize_debugfs();
	return PTR_ERR(ok);
}
#else
#define aci_initialize_debugfs(aci)	1
#define aci_uninitialize_debugfs()
#endif

static int twl5031_aci_read(u8 reg)
{
	int ret;
	u8 val;

	ret = twl_i2c_read_u8(TWL5031_MODULE_ACCESSORY, &val, reg);
	if (ret) {
		pr_err("unable to read register 0x%X: %d\n", reg, __LINE__);
		return ret;
	}

	return val;
}

static void twl5031_aci_write(u8 reg, u8 val)
{
	int ret;

	ret = twl_i2c_write_u8(TWL5031_MODULE_ACCESSORY, val, reg);
	if (ret)
		pr_err("unable to write register 0x%X: %d\n", reg, __LINE__);
}

static void twl5031_aci_enable_irqs(u16 mask)
{
	u16 val;

	val = twl5031_aci_read(TWL5031_ACIIMR_MSB) << 8;
	val |= twl5031_aci_read(TWL5031_ACIIMR_LSB);
	val &= ~mask;
	twl5031_aci_write(TWL5031_ACIIMR_LSB, val);
	twl5031_aci_write(TWL5031_ACIIMR_MSB, val >> 8);
}

static void twl5031_aci_disable_irqs(u16 mask)
{
	u16 val;

	val = twl5031_aci_read(TWL5031_ACIIMR_MSB) << 8;
	val |= twl5031_aci_read(TWL5031_ACIIMR_LSB);
	val |= mask;
	twl5031_aci_write(TWL5031_ACIIMR_LSB, val);
	twl5031_aci_write(TWL5031_ACIIMR_MSB, val >> 8);
}

/*
 * ACI ASIC fall into coma giving just ACI_COMMERR status
 * it should return to idle state after any error
 * but this seems not to be the case sometimes with ACI_COMMERR
 * strangly enough, ACI_COMMERR and ACI_FRAERR together gives no problem
 */
static void twl5031_aci_out_of_coma(void)
{
	msleep(ACI_COMA_TIMEOUT);
	twl5031_aci_write(TWL5031_ECI_DBI_CTRL,
			ACI_DISABLE | ACI_ACCESSORY_MODE);
	twl5031_aci_write(TWL5031_ECI_DBI_CTRL,
			ACI_ENABLE | ACI_ACCESSORY_MODE);
	msleep(ACI_COMA_TIMEOUT);
}

/* select mic bias sleep mode versus normal */
static void twl5031_mic_bias_mode(u8 param)
{
	u8 val, on;

	val = twl5031_aci_read(TWL5031_ACI_AV_CTRL);

	if (param == ECI_MIC_OFF) {
		val |= HOOK_DET_EN_SLEEP_MODE;
		on = 0;
	} else {
		val &= ~HOOK_DET_EN_SLEEP_MODE;
		on = 1;
	}

	twl5031_aci_write(TWL5031_ACI_AV_CTRL, val);
	dfl61_request_hsmicbias(on);
}

static int twl5031_aci_cmd(u8 task)
{
	long timein;
	u8 cmd;
	int ret = 0;

	the_aci->op = task;
	switch (task) {
	case ECI_NOP:
		cmd = ACICMD_NOP;
		break;
	case ECI_RST_LEARN:
		cmd = ACICMD_RST_LEARN;
		break;
	case ECI_READ_IO:
	case ECI_WRITE_IO:
	case ECI_READ_DIRECT:
		cmd = ACICMD_SEND;
	break;
	default:
		dev_err(the_aci->dev, "UNUSED ACI op %d: %d\n", the_aci->op,
				__LINE__);
		return -EINVAL;
	}

	the_aci->cmd = cmd;
	twl5031_aci_enable_irqs(ACI_INTERNAL);

	/* clear out "used" interrupt */
	the_aci->eci_state = 0;
	memset(&the_aci->irqs, 0, sizeof(struct aci_irqs));
	dev_pm_qos_add_request(the_aci->dev, &the_aci->qos_request_acicmd,
							PM_QOS_CPU_DMA_LATENCY, 0);

	twl5031_aci_write(TWL5031_ACICOMR_LSB, cmd);

	/*
	 * eci_state set in twl5031_aci_irq_handler
	 * then we sit and wait all ACI/ECI communication to happens.
	 * We need to prevent all kind of idle activity by setting
	 * pm_qos_requirements
	 */
	timein = wait_event_timeout(the_aci->wait,
				(the_aci->op == ECI_ERROR ||
					the_aci->op == ECI_OK),
				msecs_to_jiffies(ACI_WAIT_IRQ));

	twl5031_aci_disable_irqs(ACI_INTERNAL);
	aci_dev_pm_qos_remove_request(&the_aci->qos_request_acicmd);

	/* meaning timeout */
	if (the_aci->cmd != NO_ACICMD)
		return -EAGAIN;

	/* This seems to be necessary. Reason unknown */
	usleep_range(1000, 1100);

	if (the_aci->op == ECI_ERROR) {
		ret = (the_aci->irqs.commerr ? ACI_COMMERR : 0) |
			(the_aci->irqs.fraerr ? ACI_FRAERR : 0) |
			(the_aci->irqs.reserr ? ACI_RESERR : 0) |
			(the_aci->irqs.coll ? ACI_COLL : 0);
	}

	if (ret) {
		msleep(ACI_COMA_TIMEOUT);
		if (ret == ACI_COMMERR)
			twl5031_aci_out_of_coma();
	}

	return ret;
}

/* reset ECI accessory */
static int twl5031_acc_reset(void)
{
	int err;

	mutex_lock(&the_aci->lock);

	err = twl5031_aci_cmd(ECI_RST_LEARN);

	mutex_unlock(&the_aci->lock);

	return err;
}

/* up to four bytes */
static int twl5031_acc_read_reg(u8 reg, u8 *buf, int count)
{
	int err;

	mutex_lock(&the_aci->lock);

	/* clear out "used" interrupt
	 * eci_state set in twl5031_aci_irq_handler
	 */
	the_aci->eci_state = 0;

	the_aci->eci_cmd = reg << ECI_IO_CMD | ECI_IO_CMD_READ;
	the_aci->data.buf = buf;
	the_aci->data.count = count;

	err = twl5031_aci_cmd(ECI_READ_IO);

	mutex_unlock(&the_aci->lock);

	return err;
}

static int twl5031_acc_write_reg(u8 reg, u8 param)
{
	int err;

	mutex_lock(&the_aci->lock);
	twl5031_aci_disable_irqs(ACI_ACCINT);

	/* clear out "used" interrupt
	 * eci_state set in twl5031_aci_irq_handler
	 */
	the_aci->eci_state = 0;

	the_aci->eci_cmd = reg << ECI_IO_CMD | ECI_IO_CMD_WRITE;
	the_aci->data.buf = &param;

	err = twl5031_aci_cmd(ECI_WRITE_IO);

	mutex_unlock(&the_aci->lock);

	/* use eci_hsmic_event from ECI driver to control micbias sleep mode */
	if (reg == ECICMD_MIC_CTRL) {
		twl5031_mic_bias_mode(param);
		if (the_aci->use_accint) {
			twl5031_aci_write(TWL5031_ACIIDR_LSB, ACI_ACCINT);
			/* We need to wait mic line settling */
			msleep(ACI_BUS_SETTLE);
			twl5031_aci_write(TWL5031_ACIIDR_LSB, ACI_ACCINT);
			twl5031_aci_enable_irqs(ACI_ACCINT);
		}
	}

	return err;
}

/* read always four bytes */
static int twl5031_acc_read_direct(u8 addr, char *buf)
{
	int err;

	mutex_lock(&the_aci->lock);

	the_aci->eci_cmd = addr << ECI_DIRECT_MEM;
	the_aci->data.buf = buf;
	the_aci->data.count = 4;

	/* start action, wait untill communication done
	 * work done in twl5031_aci_irq_handler, ie. update buf
	 */
	err = twl5031_aci_cmd(ECI_READ_DIRECT);

	mutex_unlock(&the_aci->lock);

	return err;
}

void static twl5031_read_buttons(struct twl5031_aci *aci, bool latched)
{
	struct eci_data *eci = aci->eci_callback->priv;
	u8 cmd;

	if (latched)
		cmd = ECICMD_LATCHED_PORT_DATA_0;
	else
		cmd = ECICMD_PORT_DATA_0;

	dev_pm_qos_add_request(the_aci->dev, &aci->qos_request_accint,
							PM_QOS_CPU_DMA_LATENCY, 0);

	aci->io_read_ongoing = IO_READ_RETRY;
	aci->io_timeout = msecs_to_jiffies(ACI_WATCHDOG) + jiffies;

	memset(&aci->irqs, 0, sizeof(struct aci_irqs));
	aci->op = ECI_READ_IO;
	aci->eci_cmd = cmd << ECI_IO_CMD | ECI_IO_CMD_READ;

	aci->data.buf = aci->buf;
	aci->data.count = eci->port_reg_count;

	twl5031_aci_disable_irqs(ACI_COLL);
	twl5031_aci_enable_irqs(ACI_INTERNAL & ~ACI_COLL);

	twl5031_aci_write(TWL5031_ACICOMR_LSB, ACICMD_SEND);

	queue_delayed_work(aci->aci_wq, &aci->wd_work,
			msecs_to_jiffies(ACI_WATCHDOG));
}

void static twl5031_reread_buttons(struct twl5031_aci *aci)
{
	struct eci_data *eci = aci->eci_callback->priv;

	/* debug prints change the behaviour, so be careful */
	dev_vdbg(aci->dev, "ECI button read %s%s%s%sat: %d\n",
			aci->irqs.commerr ? "COMMERR " : "",
			aci->irqs.fraerr ? "FRAERR " : "",
			aci->irqs.reserr ? "RESERR " : "",
			aci->irqs.coll ? "COLLERR " : "",
			__LINE__);

	if (aci->irqs.commerr) {
		twl5031_aci_out_of_coma();
		twl5031_aci_enable_irqs(ACI_INTERNAL);
	}

	memset(&aci->irqs, 0, sizeof(struct aci_irqs));
	aci->io_read_ongoing--;

	aci->op = ECI_READ_IO;
	aci->eci_cmd =
		ECICMD_LATCHED_PORT_DATA_0 << ECI_IO_CMD | ECI_IO_CMD_READ;

	aci->data.buf = aci->buf;
	aci->data.count = eci->port_reg_count;

	twl5031_aci_write(TWL5031_ACICOMR_LSB, ACICMD_SEND);
}

void static twl5031_emit_buttons(struct twl5031_aci *aci, bool force_up)
{
	struct eci_data *eci = aci->eci_callback->priv;
	struct eci_buttons_data *b = &eci->buttons_data;

	aci_dev_pm_qos_remove_request(&aci->qos_request_accint);
	b->buttons = cpu_to_le32(*(u32 *)aci->data.buf);

	if (force_up)
		b->buttons = b->buttons_up_mask;

	aci->eci_callback->event(ECI_EVENT_BUTTON, aci->eci_callback->priv);

	aci->io_read_ongoing = 0;
	twl5031_aci_write(TWL5031_ACIIDR_LSB, ACI_ACCINT);

	twl5031_aci_disable_irqs(ACI_INTERNAL);
	aci->use_accint = true;
	twl5031_aci_enable_irqs(ACI_ACCINT);
}

/*
 * This is ran when driver start or at plug off event.
 * Only other places to touch this buffer are twl5031_get_av_button() and
 * twl5031_send_av_button().
 *
 * It's quaranteed that aci->plugged is false when this is ran.
 */
static void twl5031_buttons_buf_clear(struct twl5031_aci *aci)
{
	int i;

	for (i = 0; i < ACI_BUTTON_BUF_SIZE; i++)
		aci->buttons_buf[i] = ACI_BT_NONE;

	aci->rindex = 0;
	aci->windex = 0;
}

/*
 * Only non ECI headset button case may trigger this.
 *
 * It means ACI_ACCINT interrupt if and only if accessory type is either
 * ACI_HEADSET or ACI_CARKIT.
 *
 * It is OK to add button events even during twl5031_send_av_button().
 */
static void twl5031_get_av_button(struct twl5031_aci *aci)
{
	u16 val;

	/* do not modify buffer if plug is already removed */
	if (!aci->plugged)
		return;

	twl5031_aci_write(TWL5031_ACI_AV_CTRL, AV_COMP1_EN);
	usleep_range(ACI_WAIT_COMPARATOR_SETTLING_LO,
		ACI_WAIT_COMPARATOR_SETTLING_HI);
	val = twl5031_aci_read(TWL5031_ACI_AV_CTRL);

	if (val & STATUS_A1_COMP)
		aci->button = ACI_BT_DOWN;
	else
		aci->button = ACI_BT_UP;

	if (aci->button != button) {
		if (aci->windex < ACI_BUTTON_BUF_SIZE) {
			if (aci->buttons_buf[aci->windex] == ACI_BT_NONE) {
				aci->buttons_buf[aci->windex] = aci->button;
			} else
				dev_err(aci->dev,
						"ACI button queue overflow\n");
		}
		aci->windex++;
		if (aci->windex == ACI_BUTTON_BUF_SIZE)
			aci->windex = 0;
	}

	button = aci->button;

	/* trigger twl5031_send_av_button() */
	aci->task = ACI_TASK_SEND_BUTTON;
	queue_delayed_work(aci->aci_wq, &aci->aci_work,
			msecs_to_jiffies(ACI_WAIT_PLUG_TO_BUTTONS));
}

/* Only task ACI_TASK_SEND_BUTTON may trigger this.
 * Only twl5031_get_av_button() may set ACI_TASK_SEND_BUTTON.
 */
static void twl5031_send_av_button(struct twl5031_aci *aci)
{
	int i, btn;

	for (i = 0; i < ACI_BUTTON_BUF_SIZE; i++) {
		btn = aci->buttons_buf[aci->rindex];
		/* do not modify buffer if plug is already removed */
		if (btn == ACI_BT_NONE || !aci->plugged)
			break;

		aci->aci_callback->button_event(btn, aci->aci_callback->priv);

		aci->buttons_buf[aci->rindex] = ACI_BT_NONE;
		aci->rindex++;
		if (aci->rindex == ACI_BUTTON_BUF_SIZE)
			aci->rindex = 0;
	}

	if (aci->button == ACI_BT_DOWN) {
		/* trigger twl5031_get_av_button() */
		aci->task = ACI_TASK_GET_BUTTON;
		queue_delayed_work(aci->aci_wq, &aci->aci_work,
				msecs_to_jiffies(ACI_WAIT_CHECK_BUTTONS));
	} else {
		aci->irqs.accint = 0;
		twl5031_aci_enable_irqs(ACI_ACCINT);
	}
}

/*
 * ACI irq (374)
 * ACI commands responces, errors or accessory interrupts
 */
static irqreturn_t twl5031_aci_irq_thread(int irq, void *_aci)
{
	struct twl5031_aci *aci = _aci;
	unsigned long flags;
	u16 val;
	int res;

	/* read TWL5031_ACIIDR_LSB & TWL5031_ACIIDR_MSB */
	/*twl_set_regcache_bypass(TWL5031_MODULE_ACCESSORY, true);*/
	res = twl_i2c_read_u16(TWL5031_MODULE_ACCESSORY, &val,
							TWL5031_ACIIDR_LSB);
	/*twl_set_regcache_bypass(TWL5031_MODULE_ACCESSORY, false);*/
	if (res) {
		dev_err(aci->dev, "Failed to read ACIIDR\n");
		return IRQ_HANDLED;
	}

	/* write TWL5031_ACIIDR_LSB & TWL5031_ACIIDR_MSB to clear IRQ*/
	res = twl_i2c_write_u16(TWL5031_MODULE_ACCESSORY, val,
							TWL5031_ACIIDR_LSB);
	if (res) {
		dev_err(aci->dev, "Failed to write ACIIDR\n");
	}

	/*
	 * disregard queued ACI_ACCINT irqs if io_read_ongoing
	 */
	if (aci->io_read_ongoing)
		val &= ~ACI_ACCINT;

	if (val) {
		spin_lock_irqsave(&aci->irqlock, flags);
		aci->irq_bits |= val;
		spin_unlock_irqrestore(&aci->irqlock, flags);
		schedule_work(&aci->irq_work);
	}

	return IRQ_HANDLED;
};

static void twl5031_irq_work(struct work_struct *work)
{
	struct twl5031_aci *aci = container_of(work, struct twl5031_aci,
											irq_work);
	u16 val;
	int res;

	spin_lock_bh(&aci->irqlock);
	val = aci->irq_bits;
	aci->irq_bits = 0;
	spin_unlock_bh(&aci->irqlock);

	//printk(KERN_ERR "%s, val: %x\n", __func__, val);

	if (val & ACI_ACCINT)
		aci->irqs.accint++;
	if (val & ACI_DREC)
		aci->irqs.drec++;
	if (val & ACI_DSENT)
		aci->irqs.dsent++;
	if (val & ACI_SPDSET)
		aci->irqs.spdset++;
	if (val & ACI_COMMERR)
		aci->irqs.commerr++;
	if (val & ACI_FRAERR)
		aci->irqs.fraerr++;
	if (val & ACI_RESERR)
		aci->irqs.reserr++;
	if (val & ACI_COLL)
		aci->irqs.coll++;
	if (val & ACI_NOPINT)
		aci->irqs.nopint++;

	/* ECI headset button */
	if (aci->irqs.accint && aci->accessory == ACI_ECI_HEADSET &&
		aci->task == ACI_TASK_NONE) {

		aci->use_accint = false;
		twl5031_aci_disable_irqs(ACI_ACCINT);

		cancel_delayed_work_sync(&aci->wd_work);

		mutex_lock(&aci->iolock);

		if (aci->io_read_ongoing)
			dev_err(aci->dev, "Should not get button interrupt"
				" during IO reg reading\n");
		else
			twl5031_read_buttons(aci, ECI_BUTTONS_LATCHED);

		mutex_unlock(&aci->iolock);
		/* Remove possible pending interrupt */
		spin_lock_bh(&aci->irqlock);
		aci->irq_bits &= ~ACI_ACCINT;
		spin_unlock_bh(&aci->irqlock);
	}

	/* order of if's are important */
	switch (aci->op) {
	case ECI_NOP:
		if (aci->irqs.commerr || aci->irqs.fraerr ||
				aci->irqs.reserr || aci->irqs.coll) {
			aci->cmd = NO_ACICMD;
			aci->op = ECI_ERROR;
			wake_up(&aci->wait);
			goto out;
		}
		if (aci->irqs.nopint) {
			aci->op = ECI_OK;
			aci->cmd = NO_ACICMD;
			wake_up(&aci->wait);
			goto out;
		}
		break;
	case ECI_RST_LEARN:
		if (aci->irqs.spdset) {
			aci->op = ECI_OK;
			aci->cmd = NO_ACICMD;
			wake_up(&aci->wait);
			goto out;
		}
		if (aci->irqs.reserr) {
			aci->op = ECI_ERROR;
			aci->cmd = NO_ACICMD;
			wake_up(&aci->wait);
			goto out;
		}
		break;
	case ECI_WRITE_IO:
		if (aci->irqs.commerr || aci->irqs.fraerr ||
				aci->irqs.reserr || aci->irqs.coll) {
			aci->cmd = NO_ACICMD;
			aci->op = ECI_ERROR;
			wake_up(&aci->wait);
			goto out;
		}
		if (aci->irqs.nopint == 1) {
			aci->op = ECI_OK;
			aci->cmd = NO_ACICMD;
			wake_up(&aci->wait);
			goto out;
		}
		if (aci->irqs.dsent == 3) {
			twl5031_aci_write(TWL5031_ACICOMR_LSB, ACICMD_NOP);
			goto out;
		}
		if (aci->irqs.dsent == 2) {
			twl5031_aci_write(TWL5031_ACITXDAR, aci->data.buf[0]);
			goto out;
		}
		if (aci->irqs.dsent == 1) {
			twl5031_aci_write(TWL5031_ACITXDAR, aci->eci_cmd);
			goto out;
		}
		break;
	case ECI_READ_IO:
		/*
		 * there was error reading IO reg
		 */
		if (aci->irqs.commerr || aci->irqs.fraerr ||
				aci->irqs.reserr || aci->irqs.coll) {
			aci->cmd = NO_ACICMD;
			aci->op = ECI_ERROR;
			/* Wait outside mutex until bus traffic is finished. */
			msleep(ACI_BUS_TIMEOUT);
			mutex_lock(&aci->iolock);
			if (aci->io_read_ongoing > 1)
				twl5031_reread_buttons(aci);
			else
				twl5031_aci_disable_irqs(ACI_INTERNAL);
			mutex_unlock(&aci->iolock);
			wake_up(&aci->wait);
			goto out;
		}
		/* IO reg read OK */
		if (aci->irqs.nopint == 2) {
			aci->cmd = NO_ACICMD;
			aci->op = ECI_OK;
			mutex_lock(&aci->iolock);
			if (aci->io_read_ongoing)
				/* enable ACCINT */
				twl5031_emit_buttons(aci, ECI_REAL_BUTTONS);
			mutex_unlock(&aci->iolock);
			wake_up(&aci->wait);
			goto out;
		}
		if (aci->irqs.drec) {
			res = twl5031_aci_read(TWL5031_ACIRXDAR);
			if (aci->irqs.drec > 4)
				goto out;
			aci->data.buf[aci->irqs.drec-1] = res;
			if (aci->irqs.drec == aci->data.count)
				twl5031_aci_write(TWL5031_ACICOMR_LSB,
						ACICMD_NOP);
			goto out;
		}
		if (aci->irqs.spdset == 1)
			goto out;
		if (aci->irqs.nopint == 1) {
			twl5031_aci_write(TWL5031_ACICOMR_LSB,
					ACICMD_RECEIVE_LEARN);
			goto out;
		}
		if (aci->irqs.dsent == 2) {
			twl5031_aci_write(TWL5031_ACICOMR_LSB, ACICMD_NOP);
			goto out;
		}
		/* IO reg read start with read command */
		if (aci->irqs.dsent == 1) {
			twl5031_aci_write(TWL5031_ACITXDAR, aci->eci_cmd);
			goto out;
		}
		break;
	case ECI_READ_DIRECT:
		if (aci->irqs.commerr || aci->irqs.fraerr ||
				aci->irqs.reserr || aci->irqs.coll) {
			aci->cmd = NO_ACICMD;
			aci->op = ECI_ERROR;
			wake_up(&aci->wait);
			goto out;
		}
		if (aci->irqs.nopint == 2) {
			aci->cmd = NO_ACICMD;
			aci->op = ECI_OK;
			wake_up(&aci->wait);
			goto out;
		}
		if (aci->irqs.drec) {
			res = twl5031_aci_read(TWL5031_ACIRXDAR);
			if (aci->irqs.drec > 4)
				goto out;
			aci->data.buf[aci->irqs.drec-1] = res;
			if (aci->irqs.drec == aci->data.count)
				twl5031_aci_write(TWL5031_ACICOMR_LSB,
						ACICMD_NOP);
			goto out;
		}
		if (aci->irqs.spdset == 1)
			goto out;
		if (aci->irqs.nopint == 1) {
			twl5031_aci_write(TWL5031_ACICOMR_LSB,
					ACICMD_RECEIVE_LEARN);
			goto out;
		}
		if (aci->irqs.dsent == 2) {
			twl5031_aci_write(TWL5031_ACICOMR_LSB, ACICMD_NOP);
			goto out;
		}
		if (aci->irqs.dsent == 1) {
			twl5031_aci_write(TWL5031_ACITXDAR, aci->eci_cmd);
			goto out;
		}
		break;
	case ECI_OK:
		break;
	case ECI_ERROR:
		break;
	default:
		dev_err(aci->dev, "unknown ACI op %d: %d\n", aci->op, __LINE__);
		break;

out:
	return;
	}

	/* non ECI headset button */
	if (aci->irqs.accint && (aci->accessory == ACI_HEADSET ||
				aci->accessory == ACI_CARKIT)) {
		twl5031_aci_disable_irqs(ACI_ACCINT);
		twl5031_get_av_button(aci);
	}
}

/*
 * AvPlugDet irq
 * ie. jack_gpio, polarity stated in avplugdet_plugged
 */
static irqreturn_t twl5031_plugdet_irq_handler(int irq, void *_aci)
{
	struct twl5031_aci *aci = _aci;
	int delay, avplugdet;

	dev_dbg(aci->dev, "AvPlugDet interrupt\n");

	avplugdet = gpio_get_value_cansleep(aci->jack_gpio);
	aci->plugged = (avplugdet == aci->avplugdet_plugged);

	if (aci->plugged) {
		/* Order delayed mic bias on event */
		delay = aci->detection_latency - ACI_WAIT_PLUG_ON_MIN;
		aci->detection_phase = ACI_DETECTION_BIAS_ON;
	} else {
		delay = ACI_WAIT_PLUG_OFF;
	}

	/* start accessory detection */
	queue_delayed_work(aci->aci_wq, &aci->plug_work,
			msecs_to_jiffies(delay));

	return IRQ_HANDLED;
}

static void twl5031_aci_set_av_output(struct twl5031_aci *aci, int mode)
{
	if (mode == ACI_AUDIO)
		gpio_set_value(aci->tvout_gpio, 0);
	else
		gpio_set_value(aci->tvout_gpio, 1);
}

static int twl5031_v_hs_mic_out(struct twl5031_aci *aci)
{
	int ret;

	ret = twl4030_get_madc_conversion(ACI_MADC_CHANNEL);
	aci->vhsmicout = MADC_ADCIN10_PRESCALE(ret);

	return aci->vhsmicout;
}

/*
 * detects these accessory types:
 * headphones
 * video cable
 * ECI
 * open cable
 * basic headset
 * basic carkit
 */
static int twl5031_av_detection(struct twl5031_aci *aci)
{
	int ret, old;
	u8 val;

	dfl61_request_hp_enable(0);
	dev_dbg(aci->dev, "AV detection\n");

	/* We don't need debounce here */
	val = twl5031_aci_read(TWL5031_ACI_AUDIO_CTRL);
	val |= HOOK_DEB_BYPASS;
	twl5031_aci_write(TWL5031_ACI_AUDIO_CTRL, val);

	/* Enable ACI block and disconnect pull ups == DBI MODE */
	twl5031_aci_write(TWL5031_ECI_DBI_CTRL, ACI_ENABLE | ACI_DBI_MODE);
	dfl61_request_hsmicbias(1);
	msleep(ACI_WAIT_VOLTAGE_SETTLING);

	/* MADC measurement path enabled */
	twl5031_aci_write(TWL5031_ACI_AV_CTRL, ADC_SW_EN);

	/* Check for wrong pinout accessories using HP amplifier */
	old = twl5031_v_hs_mic_out(aci);
	dfl61_request_hp_enable(1);
	msleep(ACI_WAIT_VOLTAGE_SETTLING);
	ret = twl5031_v_hs_mic_out(aci);
	dfl61_request_hp_enable(0);
	if (old > ret)
		if (old - ret > ACI_AHJ_NHJ_TRESHOLD)
			return ACI_UNKNOWN;

	/*
	 * MADC measurement path and comparator 1 enabled.
	 * All pull ups disabled.
	 */
	twl5031_aci_write(TWL5031_ACI_AV_CTRL, ADC_SW_EN | AV_COMP1_EN);
	usleep_range(ACI_WAIT_COMPARATOR_SETTLING_LO,
		ACI_WAIT_COMPARATOR_SETTLING_HI);
	val = twl5031_aci_read(TWL5031_ACI_AV_CTRL);

	/* headphones or video cable */
	if (val & STATUS_A1_COMP) {
		ret = twl5031_v_hs_mic_out(aci);
		if (ret < ACI_HEADPHONE_TRESHOLD)
			return ACI_HEADPHONE;
		if (ret < ACI_AVOUT_TRESHOLD)
			return ACI_AVOUT;
	}

	/* was not headphones or video cable, try ECI */
	twl5031_aci_write(TWL5031_ECI_DBI_CTRL,
			ACI_ENABLE | ACI_ACCESSORY_MODE);

	/*
	 * try several times to make sure ECI accessory has initialized
	 * itself after enabling voltage for it
	 * drawback is slowering detection of open cable and basic
	 * headset & carkit
	 */
	mutex_lock(&the_aci->lock);
	for (val = 0; val < 10; val++) {
		ret = twl5031_aci_cmd(ECI_RST_LEARN);
		if (!ret)
			break;
	}
	mutex_unlock(&the_aci->lock);

	/* ECI */
	if (!ret) {
		dfl61_request_hsmicbias(1);
		return ACI_ECI_HEADSET;
	}

	/*
	 * was not ECI, try open cable
	 * Only comparator 2 pull up enabled
	 */
	dfl61_request_hsmicbias(0);
	twl5031_aci_write(TWL5031_ECI_DBI_CTRL, ACI_ENABLE | ACI_DBI_MODE);
	val = AV_COMP2_EN | ADC_SW_EN;
	twl5031_aci_write(TWL5031_ACI_AV_CTRL, val);
	usleep_range(ACI_WAIT_VOLTAGE_SETTLING_LO,
		ACI_WAIT_VOLTAGE_SETTLING_HI);
	val = twl5031_aci_read(TWL5031_ACI_AV_CTRL);

	/* open cable, ie lineout */
	if (!(val & STATUS_A2_COMP))
		return ACI_OPEN_CABLE;

	ret = twl5031_v_hs_mic_out(aci);
	/*
	 * was not open cable, try basic headset or carkit.
	 * Pull ups disabled, regulators off. Regulator output grounded.
	 * i.e. measure voltage drop across serial resistor.
	 */
	twl5031_aci_write(TWL5031_ACI_AV_CTRL, ADC_SW_EN | HSM_GROUNDED_EN);
	msleep(ACI_WAIT_VOLTAGE_SETTLING);

	/* basic headset */
	ret = twl5031_v_hs_mic_out(aci);
	/* sane settings _after_ measuring voltage */
	val = AV_COMP1_EN | HOOK_DET_EN;
	twl5031_aci_write(TWL5031_ACI_AV_CTRL, val);
	twl5031_aci_write(TWL5031_ECI_DBI_CTRL,
			ACI_ENABLE | ACI_ACCESSORY_MODE);
	if (ret < ACI_BASIC_HEADSET_TRESHOLD)
		return ACI_HEADSET;

	/* basic carkit */
	if (ret > ACI_BASIC_CARKIT_TRESHOLD)
		return ACI_CARKIT;

	twl5031_aci_write(TWL5031_ACI_AV_CTRL, 0);
	twl5031_aci_write(TWL5031_ECI_DBI_CTRL, ACI_DISABLE | ACI_DBI_MODE);

	return ACI_UNKNOWN;
}

static void twl5031_disable_pullups(void)
{
	u8 val;

	twl5031_aci_write(TWL5031_ECI_DBI_CTRL, ACI_DISABLE | ACI_DBI_MODE);
	val = twl5031_aci_read(TWL5031_ACI_AV_CTRL);
	val &= ~AV_COMP2_EN;
	val &= ~HOOK_DET_EN_SLEEP_MODE;
	twl5031_aci_write(TWL5031_ACI_AV_CTRL, val);
}

static void twl5031_av_plug_on_off(struct twl5031_aci *aci)
{
	mutex_lock(&aci->acc_block);

	twl5031_aci_disable_irqs(ACI_INTERNAL | ACI_ACCINT);
	twl5031_buttons_buf_clear(aci);

	/* plug off */
	if (!aci->plugged) {
		/* force ECI headset buttons up event */
		if (aci->accessory == ACI_ECI_HEADSET)
			twl5031_emit_buttons(aci, ECI_FORCE_BUTTONS_UP);
		twl5031_aci_disable_irqs(ACI_ACCINT);
		/* force basic headset button up event */
		aci->aci_callback->button_event(0, aci->aci_callback->priv);

		button = ACI_BT_UP;

		/* pm settings */
		if (aci->v28_enabled) {
			if (regulator_disable(aci->v28))
				aci->v28_enabled = false;
		}

		dfl61_jack_report(0);
		aci->detected = "AV jack removed";
		dev_emerg(aci->dev, "%s\n", aci->detected);
		dfl61_request_hsmicbias(0);
		twl5031_disable_pullups();

		aci->eci_callback->event(ECI_EVENT_PLUG_OUT,
					 aci->eci_callback->priv);
		cancel_delayed_work_sync(&aci->wd_work);
		aci_dev_pm_qos_remove_request(&aci->qos_request_acicmd);
		aci_dev_pm_qos_remove_request(&aci->qos_request_accint);

		mutex_unlock(&aci->acc_block);
		return;
	}
	/* plug on */
	if (!aci->v28_enabled) {
		if (regulator_enable(aci->v28))
			aci->v28_enabled = true;
	}

	twl5031_aci_set_av_output(aci, ACI_AUDIO);

	aci->accessory = ACI_NOTYPE;
	aci->previous = ACI_NOTYPE;
	aci->count = 0;
	aci->needed = ACI_DETECTS_NEEDED - 1;
	aci->task = ACI_TASK_DETECTION;
	aci->detection_count = 0;
	aci->open_cable_redetects = ACI_OPEN_CABLE_REDETECTS;
	queue_delayed_work(aci->aci_wq, &aci->aci_work, 0);

	mutex_unlock(&aci->acc_block);
}

static void twl5031_report_accessory(struct twl5031_aci *aci)
{
	dfl61_request_hsmicbias(0);

	switch (aci->accessory) {
	case ACI_HEADPHONE:
		twl5031_disable_pullups();
		aci->detected = "SND_JACK_HEADPHONE";
		dfl61_jack_report(SND_JACK_HEADPHONE);
		break;
	case ACI_AVOUT:
		twl5031_disable_pullups();
		aci->detected = "SND_JACK_AVOUT";
		twl5031_aci_set_av_output(aci, ACI_VIDEO);
		dfl61_jack_report(SND_JACK_AVOUT);
		break;
	case ACI_HEADSET:
		aci->detected = "SND_JACK_HEADSET";
		dfl61_jack_report(SND_JACK_HEADSET);
		aci->task = ACI_TASK_ENABLE_BUTTON;
		queue_delayed_work(aci->aci_wq, &aci->aci_work,
				msecs_to_jiffies(ACI_WAIT_PLUG_TO_BUTTONS));
		break;
	case ACI_ECI_HEADSET:
		aci->detected = "ECI as SND_JACK_HEADSET";
		aci->eci_callback->event(ECI_EVENT_PLUG_IN,
					 aci->eci_callback->priv);
		aci->accessory = ACI_ECI_HEADSET;
		msleep(ACI_BUS_TIMEOUT);
		/* Now ECI accessory should be initialized, so check buttons */
		aci->task = ACI_TASK_NONE;
		twl5031_read_buttons(aci, ECI_BUTTONS_NOT_LATCHED);
		break;
	case ACI_OPEN_CABLE:
		/*
		 * open cable, ie lineout
		 * User may plug the other end of the cable in another device.
		 * Thus, try to re-detect it
		 * Input system filter out unnecessary events if open cable
		 * was re-detected
		 */
		twl5031_disable_pullups();
		aci->detected = "AV jack open cable as SND_JACK_LINEOUT";
		dfl61_jack_report(SND_JACK_LINEOUT);

		if (!aci->open_cable_redetect_enabled)
			break;
		if (aci->open_cable_redetects-- > 0) {
			aci->task = ACI_TASK_DETECTION;
			aci->needed = ACI_OPEN_CABLE_REDETECTS_NEEDED - 1;
			queue_delayed_work(aci->aci_wq, &aci->aci_work,
					msecs_to_jiffies(
						ACI_OPEN_CABLE_REDETECT_TIMER));
		}
		break;
	case ACI_CARKIT:
		aci->detected = "Carkit as SND_JACK_HEADPHONE";
		dfl61_jack_report(SND_JACK_HEADPHONE);
		aci->task = ACI_TASK_ENABLE_BUTTON;
		queue_delayed_work(aci->aci_wq, &aci->aci_work,
				msecs_to_jiffies(ACI_WAIT_PLUG_TO_BUTTONS));
		break;
	default:
		twl5031_disable_pullups();
		aci->detected = "Unknown AV accessory";
		dfl61_jack_report(SND_JACK_MECHANICAL);
		break;
	}
	dev_emerg(aci->dev, "%s\n", aci->detected);
	/* REVISIT Let the madc channel 0 measurements continue
	twl4030_madc_resume_ch0();*/
}

/*
 * aci_work
 * general work func for several tasks
 * there is no real race condition using aci->task freely, as tasks can
 * cancels each others
 */
static void twl5031_aci_work(struct work_struct *ws)
{
	struct twl5031_aci *aci = container_of(ws, struct twl5031_aci,
			aci_work.work);

	/* cancel all tasks if plug is already removed */
	if (!aci->plugged)
		return;

	switch (aci->task) {
	case ACI_TASK_ENABLE_BUTTON:
		twl5031_aci_write(TWL5031_ACIIDR_LSB, ACI_ACCINT);
		twl5031_aci_enable_irqs(ACI_ACCINT);
		break;
	case ACI_TASK_GET_BUTTON:
		twl5031_get_av_button(aci);
		break;
	case ACI_TASK_SEND_BUTTON:
		twl5031_send_av_button(aci);
		break;
	case ACI_TASK_DETECTION:
		/* REVISIT During the detection MADC channel0 measurement fails
		twl4030_madc_halt_ch0();*/
		mutex_lock(&aci->acc_block);

		aci->accessory = twl5031_av_detection(aci);

		mutex_unlock(&aci->acc_block);

		if (aci->previous == aci->accessory)
			aci->count++;
		else
			aci->count = 0;

		aci->previous = aci->accessory;

		/* Needed amount of successive detection results */
		if (aci->count == aci->needed) {
			aci->previous = ACI_NOTYPE;
			twl5031_report_accessory(aci);
			break;
		}
		/* Break out if we never get same successive results */
		if (aci->detection_count++ > ACI_REDETECTS_BAIL_OUT) {
			aci->accessory = ACI_UNKNOWN;
			twl5031_report_accessory(aci);
			break;
		}

		/*
		 * UNKNOWN, HEADPHONE and AVOUT detections are fast. Sleep
		 * little bit after them. This helps to avoid false detections
		 */
		if ((aci->accessory == ACI_HEADPHONE) ||
			(aci->accessory == ACI_AVOUT) ||
			(aci->accessory == ACI_UNKNOWN))
			msleep(ACI_AVOUT_DETECTION_DELAY);

		/* Release ACI HW block */
		twl5031_disable_pullups();
		/* REVISIT Let the madc channel 0 measurements continue
		twl4030_madc_resume_ch0();*/
		queue_delayed_work(aci->aci_wq, &aci->aci_work, 0);
		break;
	default:
		dev_err(aci->dev, "unknown task %d: %d\n", aci->task, __LINE__);
		break;
	}
}

/*
 * plug_work - dedicated accessory insert/removal
 */
static void twl5031_plug_work(struct work_struct *ws)
{
	struct twl5031_aci *aci = container_of(ws, struct twl5031_aci,
			plug_work.work);

	if (aci->detection_phase == ACI_DETECTION_BIAS_ON) {
		/* Enable bias to start possible ECI ASIC boot */
		dfl61_request_hsmicbias(1);
		aci->detection_phase = ACI_DETECTION_NONE;
		/* Order actual detection to start */
		queue_delayed_work(aci->aci_wq, &aci->plug_work,
				msecs_to_jiffies(ACI_WAIT_PLUG_ON_MIN));
	} else {
		twl5031_av_plug_on_off(aci);
	}
}

/*
 * wd_work - recovery watch dog
 */
static void twl5031_wd_work(struct work_struct *ws)
{
	struct twl5031_aci *aci = container_of(ws, struct twl5031_aci,
			wd_work.work);

	printk(KERN_ERR "%s\n", __func__);

	mutex_lock(&aci->iolock);
	aci->task = ACI_TASK_NONE;
	/* jammed IO read */
	if (aci->io_read_ongoing) {
		aci_dev_pm_qos_remove_request(&aci->qos_request_accint);

		/*
		 * Wait until previous communication is over
		 * and reset aci block
		 */
		twl5031_aci_out_of_coma();
		aci->cmd = NO_ACICMD;
		aci->op = ECI_OK;
		/* This also enables ACCINT */
		twl5031_emit_buttons(aci, ECI_FORCE_BUTTONS_UP);
	}
	mutex_unlock(&aci->iolock);
}

static ssize_t twl5031_aci_detection_latency_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct twl5031_aci *aci = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", aci->detection_latency);
}

static ssize_t twl5031_aci_detection_latency_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t len)
{
	struct  twl5031_aci *aci = dev_get_drvdata(dev);
	unsigned long latency;

	if (kstrtoul(buf, 0, &latency))
		return -EINVAL;

	if (latency < ACI_WAIT_PLUG_ON_MIN || latency > ACI_WAIT_PLUG_ON_MAX)
		return -EINVAL;

	aci->detection_latency = latency;
	return len;
}

static DEVICE_ATTR(detection_latency, S_IRUGO | S_IWUSR,
		twl5031_aci_detection_latency_show,
		twl5031_aci_detection_latency_store);

static ssize_t twl5031_aci_redetection_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct twl5031_aci *aci = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", aci->open_cable_redetect_enabled);
}

static ssize_t twl5031_aci_redetection_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct  twl5031_aci *aci = dev_get_drvdata(dev);
	int enable, ret;

	ret = sscanf(buf, "%d", &enable);
	if (ret < 0)
		return ret;

	if (ret != 1)
		return -EINVAL;

	aci->open_cable_redetect_enabled = enable;
	return len;
}

static DEVICE_ATTR(periodic_open_cable_redetection_enable, S_IRUGO | S_IWUSR,
		twl5031_aci_redetection_show,
		twl5031_aci_redetection_store);

/*
 * Handle concurrency with acc_block. Refuse to run when plug is inserted,
 * so no race condition when AV detection logic is in normal use
 */
static ssize_t twl5031_aci_selftest_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct  twl5031_aci *aci = dev_get_drvdata(dev);
	u8 val, orig_av_ctrl, orig_eci_dbi_ctrl;
	ssize_t len;

	if (aci->plugged)
		return sprintf(buf, "AV plug inserted, not running selftest\n");

	mutex_lock(&aci->acc_block);
	/* REVISIT During the selftest MADC channel0 measurement fails
	twl4030_madc_halt_ch0();*/

	orig_av_ctrl = twl5031_aci_read(TWL5031_ACI_AV_CTRL);
	orig_eci_dbi_ctrl = twl5031_aci_read(TWL5031_ECI_DBI_CTRL);

	/* drive HSMIC.DC low */
	dfl61_request_hsmicbias(0);
	twl5031_aci_write(TWL5031_ECI_DBI_CTRL, ACI_ENABLE | ACI_DBI_MODE);

	/* Ground HSMIC line to discharge capasitors */
	twl5031_aci_write(TWL5031_ACI_AV_CTRL, HSM_GROUNDED_EN);
	msleep(ACI_WAIT_VOLTAGE_SETTLING);
	/* Remove grounding of the line */
	twl5031_aci_write(TWL5031_ACI_AV_CTRL, 0);
	msleep(ACI_WAIT_VOLTAGE_SETTLING);

	/* Measure */
	twl5031_aci_write(TWL5031_ACI_AV_CTRL, AV_COMP1_EN);
	usleep_range(ACI_WAIT_COMPARATOR_SETTLING_LO,
		ACI_WAIT_COMPARATOR_SETTLING_HI);
	val = twl5031_aci_read(TWL5031_ACI_AV_CTRL);

	if ((val & STATUS_A1_COMP) != AV_BELOW_600mV) {
		len = sprintf(buf, "FAILED\n can not drive HSMIC.DC low\n");
		goto out;
	}

	/* drive HSMIC.DC high with sleep mode LDO */
	val = AV_COMP1_EN | HOOK_DET_EN | HOOK_DET_EN_SLEEP_MODE;
	twl5031_aci_write(TWL5031_ACI_AV_CTRL, val);
	usleep_range(ACI_WAIT_COMPARATOR_SETTLING_LO,
		ACI_WAIT_COMPARATOR_SETTLING_HI);
	val = twl5031_aci_read(TWL5031_ACI_AV_CTRL);

	if ((val & STATUS_A1_COMP) != AV_ABOVE_600mV) {
		len = sprintf(buf, "FAILED\n can not drive HSMIC.DC high\n");
		goto out;
	}

	/* drive HSMIC.DC high with MIC bias */
	val &= ~HOOK_DET_EN_SLEEP_MODE;
	twl5031_aci_write(TWL5031_ACI_AV_CTRL, val);
	dfl61_request_hsmicbias(1);
	usleep_range(ACI_WAIT_COMPARATOR_SETTLING_LO,
		ACI_WAIT_COMPARATOR_SETTLING_HI);
	val = twl5031_aci_read(TWL5031_ACI_AV_CTRL);

	if ((val & STATUS_A1_COMP) != AV_ABOVE_600mV) {
		len = sprintf(buf, "FAILED\n can not drive HSMIC.DC high\n");
		goto out;
	}
	len = sprintf(buf, "OK\n");
out:
	dfl61_request_hsmicbias(0);
	twl5031_aci_write(TWL5031_ACI_AV_CTRL, orig_av_ctrl);
	twl5031_aci_write(TWL5031_ECI_DBI_CTRL, orig_eci_dbi_ctrl);

	/* REVISIT Let the madc channel 0 measurements continue
	twl4030_madc_resume_ch0(); */
	mutex_unlock(&aci->acc_block);

	return len;
}

static DEVICE_ATTR(selftest, S_IRUGO, twl5031_aci_selftest_show, NULL);

static struct attribute *sysfs_attrs_ctrl[] = {
	&dev_attr_detection_latency.attr,
	&dev_attr_periodic_open_cable_redetection_enable.attr,
	&dev_attr_selftest.attr,
	NULL
};

static struct attribute_group twl5031_aci_attr_group = {
	.attrs = sysfs_attrs_ctrl
};

static int is_twl5031_aci(struct twl5031_aci *aci)
{
	u8 i;

	/* Do not change order unless ACI HW is changed */
	struct {
		u8 sync0;
		u8 sync1;
		u8 vcpr;
		u8 vcnum;
		u8 vcver;
		u8 masize;
		u8 mdsize;
		u8 mfsize;
		u8 log_byte_count;
		u8 sync0_rep;
	} aciid;

	u8 *byte = &aciid.sync0;

	/* REVISIT:
	 * twl5031 silicon rev. ES 1.0 and ES 1.1 have ACI/ECI bug
	 * Any writes on 0x74-0x7E of any address group
	 * will affect same ACI/ECI registers.
	 * Workaround *MUST* be implemented in twl-core.c
	 */
	if ((twl_get_type() & 0xffff) == 0x802f) {
		int version = twl_get_version();
		if (version == 0x1b || version == 0x0b) {
			dev_err(aci->dev, "Buggy ECI found: patch to twl-core.c needed!\n");
			return -ENODEV;
		};
	}

	/* set index to the ACI ID system */
	twl5031_aci_write(TWL5031_ACIID, 0);
	for (i = 0; i < sizeof(aciid); i++) {
		*byte = twl5031_aci_read(TWL5031_ACIID);
		byte++;
	}

	if ((aciid.sync0 == 0x55) && (aciid.sync1 == 0xaa) &&
						(aciid.sync0_rep == 0x55)) {
		dev_info(aci->dev, "found twl5031 ACI v.%d\n",  aciid.vcver);
		aci->version = aciid.vcver;
		return 0;
	}

	return -ENODEV;
}

static struct miscdevice twl5031_aci_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = ACI_DRIVERNAME,
};

static struct eci_hw_ops twl5031_aci_hw_ops = {
	.acc_reset			= twl5031_acc_reset,
	.acc_read_direct	= twl5031_acc_read_direct,
	.acc_read_reg		= twl5031_acc_read_reg,
	.acc_write_reg		= twl5031_acc_write_reg,
};

static int twl5031_aci_probe(struct platform_device *pdev)
{
	struct twl5031_aci *aci;
	struct twl5031_aci_data *pdata = pdev->dev.platform_data;
	struct device_node *np = pdev->dev.of_node;
	int irq;
	int ret;

	aci = devm_kzalloc(&pdev->dev, sizeof(*aci), GFP_KERNEL);
	if (!aci)
		return -ENOMEM;

	if (pdata) {
		aci->tvout_gpio = pdata->tvout_gpio;
		aci->jack_gpio = pdata->jack_gpio;
		aci->avplugdet_plugged = pdata->avplugdet_plugged;
	} else if (np) {
		/*struct device_node *audio_node, *codec_node;
		struct platform_device *codec_pdev;*/

		ret = of_get_named_gpio(np, "tvout_en_gpio", 0);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"Failed to request tvout_en_gpio: %d\n", ret);
			return ret;
		}
		aci->tvout_gpio = ret;

		ret = of_get_named_gpio(np, "jack_gpio", 0);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to request jack_gpio: %d\n", ret);
			return ret;
		}
		aci->jack_gpio = ret;

		/*audio_node = of_find_compatible_node(np->parent, NULL,
												"ti,twl4030-audio");
		if (!audio_node) {
			dev_err(&pdev->dev, "Failed to find twl4030-audio node\n");
			return -ENODEV;
		}

		codec_node = of_find_node_by_name(audio_node, "codec");
		if (!codec_node) {
			dev_err(&pdev->dev, "Failed to find twl4030-codec node\n");
			return -ENODEV;
		}

		codec_pdev = of_find_device_by_node(codec_node);
		if (!codec_pdev) {
			dev_warn(&pdev->dev, "Failed to find twl4030-codec node\n");
			return -EPROBE_DEFER;
		}

		codec_component = soc_find_component(codec_node, "twl4030-codec");
		if (!codec_component) {
			dev_err(&pdev->dev,
					"Failed to find twl4030-codec component node\n");
			return -EPROBE_DEFER;
		}*/

		of_property_read_s32(np, "avplugdet_plugged",
			&aci->avplugdet_plugged);
	} else {
		dev_err(&pdev->dev, "platform_data not available\n");
		return -EINVAL;
	}

	platform_set_drvdata(pdev, aci);
	aci->dev = &pdev->dev;

	twl_set_regcache_bypass(TWL5031_MODULE_ACCESSORY, true); //FIXME

	aci->v28 = devm_regulator_get(&pdev->dev, "v28");
	if (IS_ERR(aci->v28)) {
		dev_err(&pdev->dev, "Failed to request v28 supply\n");
		return -ENODEV;
	}
	aci->v28_enabled = false;

	ret = devm_gpio_request(&pdev->dev, aci->tvout_gpio,
							"ACI TVOUT_EN");
	if (ret)
		dev_err(&pdev->dev, "could not request TVOUT_EN gpio: %d\n",
				aci->tvout_gpio);

	gpio_direction_output(aci->tvout_gpio, 0);

	ret = devm_gpio_request(&pdev->dev, aci->jack_gpio, "AvPlugDet");
	if (ret)
		dev_err(&pdev->dev, "could not request AvPlugDet gpio: %d\n",
				aci->jack_gpio);

	aci->detected = "AV jack not plugged yet";
	aci->detection_latency = ACI_WAIT_PLUG_ON_DEFAULT;
	aci->detection_phase = ACI_DETECTION_NONE;
	aci->open_cable_redetects = ACI_OPEN_CABLE_REDETECTS;
	aci->open_cable_redetect_enabled = true;
	aci->data.buf = aci->buf;

	ret = dfl61_request_hsmicbias(0);
	if (ret == -EPROBE_DEFER)
		return ret;

	the_aci = aci;

	/* force reset to ACI ASIC. Disable all ACI irqs by default */
	twl5031_aci_write(TWL5031_ECI_DBI_CTRL, ACI_DISABLE | ACI_DBI_MODE);

	if (is_twl5031_aci(aci)) {
		dev_err(&pdev->dev, "twl5031 ACI not found\n");
		ret = -ENODEV;
		goto err_aci;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &twl5031_aci_attr_group);
	if (ret < 0) {
		dev_err(&pdev->dev, "Sysfs registration failed\n");
		goto err_sysfs;
	}

	ret = misc_register(&twl5031_aci_device);
	if (ret) {
		dev_err(&pdev->dev, "could not register misc_device\n");
		goto err_misc_register;
	}

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
									twl5031_aci_irq_thread,
									0, dev_name(aci->dev)
									,aci);
	if (ret) {
		dev_err(&pdev->dev, "could not request ACI irq %d\n", irq);
		goto err_aci_irq;
	}

	aci->aci_wq = create_singlethread_workqueue("aci");
	if (aci->aci_wq == NULL) {
		dev_err(&pdev->dev, "couldn't create aci workqueue\n");
		ret = -ENOMEM;
		goto err_wq;
	}
	INIT_DELAYED_WORK(&aci->aci_work, twl5031_aci_work);
	INIT_DELAYED_WORK(&aci->plug_work, twl5031_plug_work);
	INIT_DELAYED_WORK(&aci->wd_work, twl5031_wd_work);
	INIT_WORK(&aci->irq_work, twl5031_irq_work);

	ret = aci_initialize_debugfs(aci);
	if (!ret)
		dev_err(&pdev->dev, "could not create debugfs entries\n");

	/* Register to ACI and ECI input drivers */
	aci->aci_callback = aci_register();
	if (IS_ERR(aci->aci_callback)) {
		ret = PTR_ERR(aci->aci_callback);
		goto err_aci_register;
	}

	aci->eci_callback = eci_register(&twl5031_aci_hw_ops);
	if (IS_ERR(aci->eci_callback)) {
		ret = PTR_ERR(aci->eci_callback);
		goto err_eci_register;
	}

	mutex_init(&aci->lock);
	mutex_init(&aci->iolock);
	mutex_init(&aci->acc_block);
	spin_lock_init(&aci->irqlock);
	init_waitqueue_head(&aci->wait);

	aci->avswitch = ACI_AUDIO;
	twl5031_aci_set_av_output(aci, ACI_AUDIO);

	dfl61_request_hsmicbias(0);
	twl5031_aci_write(TWL5031_ECI_DBI_CTRL, ACI_DISABLE | ACI_DBI_MODE);

	aci->io_read_ongoing = 0;

	/*
	 * this trick to get interrupt from already inserted plug
	 * there is serial resistor between gpio out and AV-connector,
	 * so this is OK thing to do
	 */
	gpio_direction_output(aci->jack_gpio, !aci->avplugdet_plugged);

	ret = devm_request_threaded_irq(&pdev->dev,
			gpio_to_irq(aci->jack_gpio), NULL,
			twl5031_plugdet_irq_handler, IRQF_TRIGGER_FALLING |
			IRQF_TRIGGER_RISING, "AvPlugDet", aci);
	if (ret) {
		dev_err(&pdev->dev, "could not request irq: %d\n", irq);
		goto err_jack_irq;
	}

	/*
	 * It seems to be necessary to wait here about a millisecond,
	 * otherwise an interrupt gets lost. The reason is unknown.
	 */
	usleep_range(1000, 1100);
	/*
	 * if plug was already inserted, this should trigger interrupt,
	 * as line was set high earlier
	 */
	aci->accessory = ACI_UNKNOWN;
	gpio_direction_input(aci->jack_gpio);

	return 0;

err_jack_irq:
err_eci_register:
	/* unregister if implemented */
err_aci_register:
	aci_uninitialize_debugfs();
	destroy_workqueue(aci->aci_wq);
err_wq:
	gpio_direction_input(aci->tvout_gpio);
	twl5031_aci_disable_irqs(ACI_INTERNAL | ACI_ACCINT);
err_aci_irq:
	misc_deregister(&twl5031_aci_device);

err_misc_register:
	sysfs_remove_group(&pdev->dev.kobj, &twl5031_aci_attr_group);
err_sysfs:
err_aci:
	twl5031_aci_write(TWL5031_ECI_DBI_CTRL, ACI_DISABLE | ACI_DBI_MODE);

	return ret;
}


static int twl5031_aci_remove(struct platform_device *pdev)
{
	struct twl5031_aci_data *pdata = pdev->dev.platform_data;
	struct twl5031_aci *aci = platform_get_drvdata(pdev);

	twl5031_aci_disable_irqs(ACI_INTERNAL | ACI_ACCINT);
	cancel_delayed_work_sync(&aci->aci_work);
	cancel_delayed_work_sync(&aci->plug_work);
	cancel_delayed_work_sync(&aci->wd_work);
	cancel_work_sync(&aci->irq_work);
	destroy_workqueue(aci->aci_wq);
	aci_dev_pm_qos_remove_request(&aci->qos_request_acicmd);
	aci_dev_pm_qos_remove_request(&aci->qos_request_accint);
	twl5031_aci_set_av_output(aci, ACI_VIDEO);
	dfl61_request_hsmicbias(0);
	twl5031_disable_pullups();
	gpio_direction_input(pdata->tvout_gpio);
	free_irq(gpio_to_irq(pdata->jack_gpio), aci);
	if (aci->v28_enabled)
		regulator_disable(aci->v28);
	aci_uninitialize_debugfs();
	misc_deregister(&twl5031_aci_device);
	sysfs_remove_group(&pdev->dev.kobj, &twl5031_aci_attr_group);

	twl5031_aci_write(TWL5031_ECI_DBI_CTRL, ACI_DISABLE | ACI_DBI_MODE);
	return 0;
}

static const struct of_device_id twl5031_aci_of_match[] = {
	{.compatible = "ti,twl5031-aci", },
	{ },
};
MODULE_DEVICE_TABLE(of, twl5031_aci_of_match);

static struct platform_driver twl5031_aci_driver = {
	.probe		= twl5031_aci_probe,
	.remove		= twl5031_aci_remove,
	.driver		= {
		.name	= ACI_DRIVERNAME,
		.owner	= THIS_MODULE,
		.of_match_table	= twl5031_aci_of_match,
	},
};
module_platform_driver(twl5031_aci_driver);

MODULE_ALIAS("platform:twl5031-aci");
MODULE_AUTHOR("Nokia Corporation");
MODULE_DESCRIPTION("twl5031 ACI driver");
MODULE_LICENSE("GPL");
