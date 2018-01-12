// SPDX-License-Identifier: GPL-2.0
//
// n9.c  --  SoC audio for Nokia N9/N950
//
// Copyright (C) 2008 - 2009 Nokia Corporation
//
// Contact: Peter Ujfalusi <peter.ujfalusi@ti.com>
//          Eduardo Valentin <eduardo.valentin@nokia.com>
//          Jarkko Nikula <jarkko.nikula@bitmer.com>
//

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <linux/platform_data/asoc-ti-mcbsp.h>
#include <linux/mfd/twl4030-audio.h>
#include "../codecs/tlv320dac33.h"
#include "../codecs/tpa6130a2.h"
#include "../codecs/wl1273.h"

#include <asm/mach-types.h>

#include <sound/n9.h>
#include "omap-mcbsp.h"
#include "mcbsp.h"

#define JACK_REPORT_MASK	(SND_JACK_MECHANICAL | SND_JACK_AVOUT | \
							 SND_JACK_HEADSET)

struct dfl61wl1273_audio_pdata {
	struct gpio_desc *power_gpio;
};

struct dfl61twl_audio_pdata {
	struct gpio_desc *speaker_amp_gpio;
};

static int dfl61dac33_interconnect_enable(int);
static struct snd_soc_card dfl61dac33_sound_card;
static struct snd_soc_card dfl61twl_sound_card;
static struct snd_soc_jack dfl61_jack;
static struct dfl61audio_hsmic_event *hsmic_event;

static struct snd_soc_component *find_component(struct snd_soc_card *card) {
	struct snd_soc_component *component;

	if (list_empty(&card->component_dev_list)) {
		pr_err("Can't find codec for %s\n", card->name);
		return NULL;
	}

	component = list_entry(card->component_dev_list.next,
			       struct snd_soc_component, card_list);

	return component;
}

/* TWL4030 */
void dfl61_jack_report(int status)
{
	if (dfl61_jack.card)
		snd_soc_jack_report(&dfl61_jack, status, JACK_REPORT_MASK);
	else
		pr_err("twl4030: Cannot report jack status");
}
EXPORT_SYMBOL_GPL(dfl61_jack_report);

int dfl61_request_hsmicbias(bool enable)
{
	struct snd_soc_component *component;
	struct snd_soc_dapm_context *dapm;
	int ret;

	if (!dfl61twl_sound_card.instantiated) {
		pr_warn("twl4030: sound card not instantiated yet");
		return -EPROBE_DEFER;
	}

	component = find_component(&dfl61twl_sound_card);
	if (!component)
		return -ENODEV;

	dapm = snd_soc_component_get_dapm(component);
	if (!dapm) {
		pr_err("twl4030: Cannot set hsmicbias yet");
		return -ENODEV;
	}

	mutex_lock(&dfl61twl_sound_card.dapm_mutex);
	if (enable)
		snd_soc_dapm_force_enable_pin_unlocked(dapm, "Headset Mic Bias");
	else
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headset Mic Bias");
	ret = snd_soc_dapm_sync_unlocked(dapm);
	mutex_unlock(&dfl61twl_sound_card.dapm_mutex);

	return ret;
}
EXPORT_SYMBOL(dfl61_request_hsmicbias);

void dfl61_register_hsmic_event_cb(struct dfl61audio_hsmic_event *event)
{
	hsmic_event = event;
}
EXPORT_SYMBOL(dfl61_register_hsmic_event_cb);

static int dfl61twl_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	unsigned int fmt;
	int r;

	switch (params_channels(params)) {
	case 2: /* Stereo I2S mode */
		fmt =	SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBM_CFM;
	case 4: /* Four channel TDM mode */
		fmt =	SND_SOC_DAIFMT_DSP_A |
			SND_SOC_DAIFMT_IB_NF |
			SND_SOC_DAIFMT_CBM_CFM;
		break;
	default:
		return -EINVAL;
	}
	/* Set codec DAI configuration */
	r = snd_soc_dai_set_fmt(rtd->codec_dai, fmt);
	if (r < 0) {
		pr_err("Can't set codec DAI configuration for twl4030: %d\n", r);
		return r;
	}

	/* Set cpu DAI configuration */
	r = snd_soc_dai_set_fmt(rtd->cpu_dai, fmt);
	if (r < 0) {
		pr_err("Can't set cpu DAI configuration for twl4030: %d\n", r);
		return r;
	}

	return 0;
}

static int dfl61twl_spk_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct dfl61twl_audio_pdata *pdata = snd_soc_card_get_drvdata(card);

	gpiod_set_raw_value_cansleep(pdata->speaker_amp_gpio,
				     !!SND_SOC_DAPM_EVENT_ON(event));

	return 0;
}

static int dfl61twl_tlv320dac33_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, int event)
{
	int r;

	if (SND_SOC_DAPM_EVENT_ON(event))
		r = dfl61dac33_interconnect_enable(1);
	else
		r = dfl61dac33_interconnect_enable(0);

	return r;
}

static int dfl61twl_hsmic_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	if (!hsmic_event || !hsmic_event->event)
		return 0;

	if (SND_SOC_DAPM_EVENT_ON(event))
		hsmic_event->event(hsmic_event->private, 1);
	else
		hsmic_event->event(hsmic_event->private, 0);

	return 0;
}

/* DAPM widgets and routing for TWL4030 */
static const struct snd_soc_dapm_widget dfl61twl_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", dfl61twl_spk_event),
	SND_SOC_DAPM_SPK("Earpiece", NULL),
	SND_SOC_DAPM_SPK("HAC", NULL),
	SND_SOC_DAPM_SPK("Vibra", NULL),
	SND_SOC_DAPM_SPK("DAC33 interconnect", dfl61twl_tlv320dac33_event),

	SND_SOC_DAPM_MIC("Digital Mic", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", dfl61twl_hsmic_event),

	SND_SOC_DAPM_LINE("FMRX Left Line-in", NULL),
	SND_SOC_DAPM_LINE("FMRX Right Line-in", NULL),
};

static const struct snd_soc_dapm_route dfl61twl_audio_map[] = {
	{"Ext Spk", NULL, "PREDRIVER"},
	{"Earpiece", NULL, "EARPIECE"},
	{"HAC", NULL, "HFL"},
	{"Vibra", NULL, "VIBRA"},
	{"DAC33 interconnect", NULL, "PREDRIVEL"},

	{"DIGIMIC0", NULL, "Digital Mic"},
	{"Digital Mic", NULL, "Mic Bias 1"},

	{"HSMIC", NULL, "Headset Mic"},
	{"Headset Mic", NULL, "Headset Mic Bias"},

	{"AUXL", NULL, "FMRX Left Line-in"},
	{"AUXR", NULL, "FMRX Right Line-in"},
};

/* Pre DAC routings for the twl4030 codec */
static const char *twl4030_predacl1_texts[] = {
	"SDRL1", "SDRM1", "SDRL2", "SDRM2",
};
static const char *twl4030_predacr1_texts[] = {
	"SDRR1", "SDRM1", "SDRR2", "SDRM2"
};
static const char *twl4030_predacl2_texts[] = {"SDRL2", "SDRM2"};
static const char *twl4030_predacr2_texts[] = {"SDRR2", "SDRM2"};

static const struct soc_enum twl4030_predacl1_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_RX_PATH_SEL, 2,
			ARRAY_SIZE(twl4030_predacl1_texts),
			twl4030_predacl1_texts);

static const struct soc_enum twl4030_predacr1_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_RX_PATH_SEL, 0,
			ARRAY_SIZE(twl4030_predacr1_texts),
			twl4030_predacr1_texts);

static const struct soc_enum twl4030_predacl2_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_RX_PATH_SEL, 5,
			ARRAY_SIZE(twl4030_predacl2_texts),
			twl4030_predacl2_texts);

static const struct soc_enum twl4030_predacr2_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_RX_PATH_SEL, 4,
			ARRAY_SIZE(twl4030_predacr2_texts),
			twl4030_predacr2_texts);

static const struct snd_kcontrol_new dfl61twl_controls[] = {
	/* Mux controls before the DACs */
	SOC_ENUM("DACL1 Playback Mux", twl4030_predacl1_enum),
	SOC_ENUM("DACR1 Playback Mux", twl4030_predacr1_enum),
	SOC_ENUM("DACL2 Playback Mux", twl4030_predacl2_enum),
	SOC_ENUM("DACR2 Playback Mux", twl4030_predacr2_enum),
};

static int dfl61twl_init(struct snd_soc_pcm_runtime *rtd)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	int r;

	/* Create jack for accessory reporting */
	r = snd_soc_card_jack_new(&dfl61twl_sound_card, "Jack",
				JACK_REPORT_MASK , &dfl61_jack, NULL, 0);
	if (r) {
		pr_err("Failed to add Jack\n");
		return r;
	}

	snd_soc_add_codec_controls(rtd->codec, dfl61twl_controls,
				ARRAY_SIZE(dfl61twl_controls));

	if (omap_mcbsp_st_add_controls(rtd, 3))
		dev_dbg(rtd->codec->dev, "Unable to set Sidetone for McBSP3\n");

	mcbsp->dma_op_mode = MCBSP_DMA_MODE_THRESHOLD;

	return 0;
}

static struct snd_soc_ops dfl61twl_ops = {
	.hw_params = dfl61twl_hw_params,
};

/* TLV320DAC33 */
static int dfl61dac33_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	int r;

	/* Set codec DAI configuration */
	r = snd_soc_dai_set_fmt(rtd->codec_dai,
				SND_SOC_DAIFMT_LEFT_J |
				SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBM_CFM);
	if (r < 0) {
		pr_err("Can't set codec DAI configuration for tlv320dac33: %d\n", r);
		return r;
	}

	/* Set cpu DAI configuration */
	r = snd_soc_dai_set_fmt(rtd->cpu_dai,
				SND_SOC_DAIFMT_LEFT_J |
				SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBM_CFM);
	if (r < 0) {
		pr_err("Can't set cpu DAI configuration for tlv320dac33: %d\n", r);
		return r;
	}

	/* Set the codec system clock for DAC and ADC */
	r = snd_soc_dai_set_sysclk(rtd->codec_dai, TLV320DAC33_SLEEPCLK, 32768,
					    SND_SOC_CLOCK_IN);
	if (r < 0) {
		pr_err("Can't set codec system clock for tlv320dac33\n");
		return r;
	}

	return 0;
}

static int dfl61dac33_interconnect_enable(int enable)
{
	struct snd_soc_component *component =
		find_component(&dfl61dac33_sound_card);
	struct snd_soc_dapm_context *dapm;

	if (!component)
		return -ENODEV;

	dapm = snd_soc_component_get_dapm(component);

	mutex_lock(&dapm->card->dapm_mutex);
	if (enable)
		snd_soc_dapm_enable_pin_unlocked(dapm, "twl4030 interconnect");
	else
		snd_soc_dapm_disable_pin_unlocked(dapm, "twl4030 interconnect");
	snd_soc_dapm_sync_unlocked(dapm);
	mutex_unlock(&dapm->card->dapm_mutex);

	return 0;
}

static void dfl61dac33_hp_enable(struct snd_soc_component *component, int enable)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);

	snd_soc_dapm_mutex_lock(dapm);

	if (enable) {
		snd_soc_dapm_enable_pin_unlocked(dapm, "Headphone");
	} else {
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headphone");
	}

	snd_soc_dapm_sync_unlocked(dapm);

	snd_soc_dapm_mutex_unlock(dapm);
}

int dfl61_request_hp_enable(bool enable)
{
	struct snd_soc_component *component =
		find_component(&dfl61dac33_sound_card);

	if (!component) {
		pr_err("dfl61-request_hp_enable");
		return -ENODEV;
	}

	dfl61dac33_hp_enable(component, enable);

	return 0;
}
EXPORT_SYMBOL(dfl61_request_hp_enable);

static const struct snd_kcontrol_new dfl61dac33_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
};

static const struct snd_soc_dapm_widget dfl61dac33_dapm_widgets[] = {
	/* Outputs */
	SND_SOC_DAPM_LINE("FMTX_L Line Out", NULL),
	SND_SOC_DAPM_LINE("FMTX_R Line Out", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	/* Inputs */
	SND_SOC_DAPM_LINE("twl4030 interconnect", NULL),
};

static const struct snd_soc_dapm_route dfl61dac33_audio_map[] = {
	{"Headphone", NULL, "TPA6140A2 HPLEFT"},
	{"Headphone", NULL, "TPA6140A2 HPRIGHT"},
	{"TPA6140A2 HPLEFT", NULL, "LEFT_LO"},
	{"TPA6140A2 HPRIGHT", NULL, "RIGHT_LO"},

	{"FMTX_L Line Out", NULL, "LEFT_LO"},
	{"FMTX_R Line Out", NULL, "RIGHT_LO"},

	{"LINER", NULL, "twl4030 interconnect"},
	{"LINEL", NULL, "twl4030 interconnect"},
};

static int dfl61dac33_init(struct snd_soc_pcm_runtime *rtd)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(rtd->codec);

	snd_soc_limit_volume(rtd->card, "TPA6140A2 Headphone Playback Volume", 21);

	snd_soc_dapm_disable_pin(dapm, "twl4030 interconnect");

	if (omap_mcbsp_st_add_controls(rtd, 2))
		dev_dbg(rtd->codec->dev, "Unable to set Sidetone for McBSP2\n");

	mcbsp->dma_op_mode = MCBSP_DMA_MODE_THRESHOLD;

	return 0;
}

static struct snd_soc_ops dfl61dac33_ops = {
	.hw_params = dfl61dac33_hw_params,
};

/* WL1273 */
static int dfl61wl1273_init(struct snd_soc_pcm_runtime *rtd)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	mcbsp->dma_op_mode = MCBSP_DMA_MODE_THRESHOLD;

	return 0;
}

static int dfl61wl1273_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	unsigned int fmt;
	int r;

	r = wl1273_get_format(rtd->codec, &fmt);
	if (r < 0) {
		pr_err("Can't get fmt for wl1273: %d\n", r);
		return r;
	}

	/* Set cpu DAI configuration */
	r = snd_soc_dai_set_fmt(rtd->cpu_dai, fmt);
	if (r < 0) {
		pr_err("Can't set cpu DAI configuration for wl1273: %d\n", r);
		return r;
	}
	return 0;
}

static struct snd_soc_ops dfl61wl1273_ops = {
	.startup = dfl61wl1273_startup,
};

static int dfl61wl1273_card_probe(struct snd_soc_card *card)
{
	struct dfl61wl1273_audio_pdata *pdata = snd_soc_card_get_drvdata(card);

	gpiod_set_value(pdata->power_gpio, 1);
	return 0;
}


static int dfl61wl1273_card_remove(struct snd_soc_card *card)
{
	struct dfl61wl1273_audio_pdata *pdata = snd_soc_card_get_drvdata(card);

	gpiod_set_value(pdata->power_gpio, 0);
	return 0;
}

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link dfl61twl_dai[] = {
	{
		.name = "TWL4030",
		.stream_name = "TWL4030",
		.cpu_dai_name = "omap-mcbsp.3",
		.codec_dai_name = "twl4030-hifi",
		.platform_name = "omap-pcm-audio",
		.codec_name = "twl4030-codec",
		.dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_IB_NF |
					SND_SOC_DAIFMT_CBM_CFM,
		.init = dfl61twl_init,
		.ops = &dfl61twl_ops,
	},
};

static struct snd_soc_dai_link dfl61dac33_dai[] = {
	{
		.name = "TLV320DAC33",
		.stream_name = "DAC33",
		.cpu_dai_name = "omap-mcbsp.2",
		.codec_dai_name = "tlv320dac33-hifi",
		.platform_name = "omap-pcm-audio",
		.codec_name = "tlv320dac33-codec.1-0019",
		.init = dfl61dac33_init,
		.ops = &dfl61dac33_ops,
	},
};

static struct snd_soc_aux_dev dfl61dac33_aux_dev[] = {
	{
		.name = "TPA6140A2",
		.codec_name = "tpa6130a2.1-0060",
	},
};

static struct snd_soc_codec_conf dfl61dac33_codec_conf[] = {
	{
		.dev_name = "tpa6130a2.2-0060",
		.name_prefix = "TPA6140A2",
	},
};

static struct snd_soc_dai_link dfl61wl1273_dai[] = {
	{
		.name = "BT/FM PCM",
		.stream_name = "BT/FM Stream",
		.cpu_dai_name = "omap-mcbsp.4",
		.codec_dai_name = "wl1273-fm",
		.platform_name = "omap-pcm-audio",
		.codec_name = "wl1273-codec",
		.init = dfl61wl1273_init,
		.ops = &dfl61wl1273_ops,
	},
};

/* Audio cards */
static struct snd_soc_card dfl61twl_sound_card = {
	.name = "dfl61-twl4030",
	.owner = THIS_MODULE,
	.dai_link = dfl61twl_dai,
	.num_links = ARRAY_SIZE(dfl61twl_dai),
	.fully_routed = true,
	.dapm_widgets = dfl61twl_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(dfl61twl_dapm_widgets),
	.dapm_routes = dfl61twl_audio_map,
	.num_dapm_routes = ARRAY_SIZE(dfl61twl_audio_map),
};

static struct snd_soc_card dfl61dac33_sound_card = {
	.name = "dfl61-dac33",
	.owner = THIS_MODULE,
	.dai_link = dfl61dac33_dai,
	.num_links = ARRAY_SIZE(dfl61dac33_dai),
	.aux_dev = dfl61dac33_aux_dev,
	.num_aux_devs = ARRAY_SIZE(dfl61dac33_aux_dev),
	.codec_conf = dfl61dac33_codec_conf,
	.num_configs = ARRAY_SIZE(dfl61dac33_codec_conf),
	.fully_routed = true,
	.controls = dfl61dac33_controls,
	.num_controls = ARRAY_SIZE(dfl61dac33_controls),
	.dapm_widgets = dfl61dac33_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(dfl61dac33_dapm_widgets),
	.dapm_routes = dfl61dac33_audio_map,
	.num_dapm_routes = ARRAY_SIZE(dfl61dac33_audio_map),
};

static struct snd_soc_card dfl61wl1273_sound_card = {
	.name = "dfl61-wl1273",
	.owner = THIS_MODULE,
	.probe = dfl61wl1273_card_probe,
	.remove = dfl61wl1273_card_remove,
	.dai_link = dfl61wl1273_dai,
	.num_links = ARRAY_SIZE(dfl61wl1273_dai),
	.fully_routed = true,
};

static int n9_soc_probe(struct platform_device *pdev)
{
	struct dfl61twl_audio_pdata *pdata_twl;
	struct dfl61wl1273_audio_pdata *pdata_wl1273;
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_card *card_twl = &dfl61twl_sound_card;
	struct snd_soc_card *card_dac33 = &dfl61dac33_sound_card;
	struct snd_soc_card *card_wl1273 = &dfl61wl1273_sound_card;
	int err;

	if (!(machine_is_nokia_rm696() || machine_is_nokia_rm680())
		&& !(of_machine_is_compatible("nokia,omap3-n9")
		|| of_machine_is_compatible("nokia,omap3-n950")))
		return -ENODEV;

	card_twl->dev = &pdev->dev;
	card_dac33->dev = &pdev->dev;
	card_wl1273->dev = &pdev->dev;

	if (np) {
		struct device_node *dai_node;

		dai_node = of_parse_phandle(np, "nokia,twl4030-cpu-dai", 0);
		if (!dai_node) {
			dev_err(&pdev->dev, "McBSP node for TWL4030 is not provided\n");
			return -EINVAL;
		}
		dfl61twl_dai[0].cpu_dai_name = NULL;
		dfl61twl_dai[0].platform_name = NULL;
		dfl61twl_dai[0].cpu_of_node = dai_node;
		dfl61twl_dai[0].platform_of_node = dai_node;

		dai_node = of_parse_phandle(np, "nokia,tlv320dac33-cpu-dai", 0);
		if (!dai_node) {
			dev_err(&pdev->dev, "McBSP node for TLV320DAC33 is not provided\n");
			return -EINVAL;
		}
		dfl61dac33_dai[0].cpu_dai_name = NULL;
		dfl61dac33_dai[0].platform_name = NULL;
		dfl61dac33_dai[0].cpu_of_node = dai_node;
		dfl61dac33_dai[0].platform_of_node = dai_node;

		dai_node = of_parse_phandle(np, "nokia,wl1273-cpu-dai", 0);
		if (!dai_node) {
			dev_err(&pdev->dev, "McBSP node for WL1273 is not provided\n");
			return -EINVAL;
		}
		dfl61wl1273_dai[0].cpu_dai_name = NULL;
		dfl61wl1273_dai[0].platform_name = NULL;
		dfl61wl1273_dai[0].cpu_of_node = dai_node;
		dfl61wl1273_dai[0].platform_of_node = dai_node;

		dai_node = of_parse_phandle(np, "nokia,twl4030-codec", 0);
		if (!dai_node) {
			dev_err(&pdev->dev, "TWL4030 codec node is not provided\n");
			return -EINVAL;
		}
		dfl61twl_dai[0].codec_name = NULL;
		dfl61twl_dai[0].codec_of_node = dai_node;

		dai_node = of_parse_phandle(np, "nokia,tlv320dac33-codec", 0);
		if (!dai_node) {
			dev_err(&pdev->dev, "TLV320DAC33 codec node is not provided\n");
			return -EINVAL;
		}
		dfl61dac33_dai[0].codec_name = NULL;
		dfl61dac33_dai[0].codec_of_node = dai_node;

		dai_node = of_parse_phandle(np, "nokia,headphone-amplifier", 0);
		if (!dai_node) {
			dev_err(&pdev->dev, "Headphone amplifier node is not provided\n");
			return -EINVAL;
		}
		dfl61dac33_aux_dev[0].codec_name = NULL;
		dfl61dac33_aux_dev[0].codec_of_node = dai_node;
		dfl61dac33_codec_conf[0].dev_name = NULL;
		dfl61dac33_codec_conf[0].of_node = dai_node;

		dai_node = of_parse_phandle(np, "nokia,wl1273-codec", 0);
		if (!dai_node) {
			dev_err(&pdev->dev, "WL1273 codec node is not provided\n");
			return -EINVAL;
		}
		dfl61wl1273_dai[0].codec_name = NULL;
		dfl61wl1273_dai[0].codec_of_node = dai_node;
	}

	pdata_twl = devm_kzalloc(&pdev->dev, sizeof(*pdata_twl), GFP_KERNEL);
	if (pdata_twl == NULL) {
		dev_err(card_twl->dev, "failed to create private data for twl4030\n");
		return -ENOMEM;
	}
	snd_soc_card_set_drvdata(card_twl, pdata_twl);

	pdata_twl->speaker_amp_gpio = devm_gpiod_get(card_twl->dev,
						     "speaker-amplifier",
						     GPIOD_OUT_LOW);
	if (IS_ERR(pdata_twl->speaker_amp_gpio)) {
		dev_err(card_twl->dev, "could not get speaker enable gpio\n");
		return PTR_ERR(pdata_twl->speaker_amp_gpio);
	}

	pdata_wl1273 = devm_kzalloc(&pdev->dev, sizeof(*pdata_wl1273), GFP_KERNEL);
	if (pdata_wl1273 == NULL) {
		dev_err(card_wl1273->dev, "failed to create private data for wl1273\n");
		return -ENOMEM;
	}
	snd_soc_card_set_drvdata(card_wl1273, pdata_wl1273);

	pdata_wl1273->power_gpio = devm_gpiod_get(card_wl1273->dev,
						  "wl1273-power",
						  GPIOD_OUT_LOW);
	if (IS_ERR(pdata_wl1273->power_gpio)) {
		dev_err(card_wl1273->dev, "could not get wl1273 enable gpio\n");
		return PTR_ERR(pdata_wl1273->power_gpio);
	}

	err = devm_snd_soc_register_card(&pdev->dev, card_twl);
	if (err < 0) {
		dev_err(card_twl->dev, "failed to register twl4030 card: %d\n", err);
		return err;
	}

	err = devm_snd_soc_register_card(&pdev->dev, card_dac33);
	if (err < 0) {
		dev_err(card_dac33->dev, "failed to register tlv320dac33 card: %d\n", err);
		return err;
	}

	err = devm_snd_soc_register_card(&pdev->dev, card_wl1273);
	if (err < 0) {
		dev_err(card_wl1273->dev, "failed to register wl1273 card\n");
		return err;
	}

	return 0;
}

static const struct of_device_id n9_audio_of_match[] = {
	{ .compatible = "nokia,n9-audio", },
	{},
};
MODULE_DEVICE_TABLE(of, n9_audio_of_match);

static struct platform_driver n9_soc_driver = {
	.driver = {
		.name = "n9-audio",
		.of_match_table = of_match_ptr(n9_audio_of_match),
	},
	.probe = n9_soc_probe,
};

module_platform_driver(n9_soc_driver);

MODULE_AUTHOR("Nokia Corporation");
MODULE_DESCRIPTION("ALSA SoC Nokia N9/N950");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:n9-audio");
