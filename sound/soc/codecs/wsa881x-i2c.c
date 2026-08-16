// SPDX-License-Identifier: GPL-2.0-only
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>

#define WSA881X_DIGITAL_BASE		0x3000
#define WSA881X_ANALOG_BASE		0x3100

#define WSA881X_CHIP_ID0			(WSA881X_DIGITAL_BASE + 0x00)
#define WSA881X_CHIP_ID1			(WSA881X_DIGITAL_BASE + 0x01)
#define WSA881X_CHIP_ID2			(WSA881X_DIGITAL_BASE + 0x02)
#define WSA881X_CHIP_ID3			(WSA881X_DIGITAL_BASE + 0x03)
#define WSA881X_CDC_RST_CTL		(WSA881X_DIGITAL_BASE + 0x05)
#define WSA881X_CDC_ANA_CLK_CTL		(WSA881X_DIGITAL_BASE + 0x07)
#define WSA881X_CDC_DIG_CLK_CTL		(WSA881X_DIGITAL_BASE + 0x08)
#define WSA881X_CLOCK_CONFIG		(WSA881X_DIGITAL_BASE + 0x09)
#define WSA881X_ANA_CTL			(WSA881X_DIGITAL_BASE + 0x0a)
#define WSA881X_RESET_CTL		(WSA881X_DIGITAL_BASE + 0x0c)
#define WSA881X_TADC_VALUE_CTL		(WSA881X_DIGITAL_BASE + 0x0f)
#define WSA881X_INTR_MASK		(WSA881X_DIGITAL_BASE + 0x21)
#define WSA881X_IOPAD_CTL		(WSA881X_DIGITAL_BASE + 0x45)
#define WSA881X_OTP_REG_28		(WSA881X_DIGITAL_BASE + 0x9c)
#define WSA881X_OTP_REG_29		(WSA881X_DIGITAL_BASE + 0x9d)
#define WSA881X_OTP_REG_30		(WSA881X_DIGITAL_BASE + 0x9e)
#define WSA881X_OTP_REG_31		(WSA881X_DIGITAL_BASE + 0x9f)

#define WSA881X_TEMP_OP			(WSA881X_ANALOG_BASE + 0x03)
#define WSA881X_TEMP_ADC_CTRL		(WSA881X_ANALOG_BASE + 0x09)
#define WSA881X_ADC_SEL_IBIAS		(WSA881X_ANALOG_BASE + 0x14)
#define WSA881X_SPKR_DRV_EN		(WSA881X_ANALOG_BASE + 0x1a)
#define WSA881X_SPKR_DRV_GAIN		(WSA881X_ANALOG_BASE + 0x1b)
#define WSA881X_SPKR_DAC_CTL		(WSA881X_ANALOG_BASE + 0x1c)
#define WSA881X_SPKR_OCP_CTL		(WSA881X_ANALOG_BASE + 0x1f)
#define WSA881X_SPKR_BBM_CTL		(WSA881X_ANALOG_BASE + 0x21)
#define WSA881X_SPKR_MISC_CTL1		(WSA881X_ANALOG_BASE + 0x22)
#define WSA881X_SPKR_MISC_CTL2		(WSA881X_ANALOG_BASE + 0x23)
#define WSA881X_SPKR_BIAS_INT		(WSA881X_ANALOG_BASE + 0x24)
#define WSA881X_SPKR_PA_INT		(WSA881X_ANALOG_BASE + 0x25)
#define WSA881X_SPKR_BIAS_CAL		(WSA881X_ANALOG_BASE + 0x26)
#define WSA881X_SPKR_BIAS_PSRR		(WSA881X_ANALOG_BASE + 0x27)
#define WSA881X_BOOST_EN_CTL		(WSA881X_ANALOG_BASE + 0x2a)
#define WSA881X_BOOST_CURRENT_LIMIT	(WSA881X_ANALOG_BASE + 0x2b)
#define WSA881X_BOOST_PS_CTL		(WSA881X_ANALOG_BASE + 0x2c)
#define WSA881X_BOOST_PRESET_OUT1	(WSA881X_ANALOG_BASE + 0x2d)
#define WSA881X_BOOST_SLOPE_COMP_ISENSE_FB (WSA881X_ANALOG_BASE + 0x31)
#define WSA881X_BOOST_LOOP_STABILITY	(WSA881X_ANALOG_BASE + 0x33)
#define WSA881X_BOOST_START_CTL		(WSA881X_ANALOG_BASE + 0x35)
#define WSA881X_BOOST_MISC2_CTL		(WSA881X_ANALOG_BASE + 0x37)
#define WSA881X_SPKR_PROT_ATEST2		(WSA881X_ANALOG_BASE + 0x3f)
#define WSA881X_BONGO_RESRV_REG1		(WSA881X_ANALOG_BASE + 0x42)
#define WSA881X_BONGO_RESRV_REG2		(WSA881X_ANALOG_BASE + 0x43)

#define WSA881X_SPKR_OCP_MASK		GENMASK(7, 6)
#define WSA881X_SPKR_OCP_EN		BIT(7)
#define WSA881X_SPKR_OCP_HOLD		BIT(6)
#define WSA881X_BOOST_EN			BIT(7)
#define WSA881X_PA_EN			BIT(7)
#define WSA881X_VDD_LOAD_UA		10000

enum wsa881x_i2c_pa_gain {
	WSA881X_PA_GAIN_0_DB,
	WSA881X_PA_GAIN_12_DB,
};

struct wsa881x_i2c {
	struct regmap *digital_regmap;
	struct regmap *analog_regmap;
	struct i2c_client *analog_client;
	struct gpio_desc *reset_gpio;
	struct pinctrl *pinctrl;
	struct pinctrl_state *active_state;
	struct pinctrl_state *sleep_state;
	struct regulator *vdd;
	enum wsa881x_i2c_pa_gain pa_gain;
	bool boost_enabled;
	bool pa_active;
	bool vdd_enabled;
};

static const struct regmap_config wsa881x_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
	.cache_type = REGCACHE_NONE,
};

static const struct reg_sequence wsa881x_rev_2_0[] = {
	{ WSA881X_RESET_CTL, 0x00 },
	{ WSA881X_TADC_VALUE_CTL, 0x01 },
	{ WSA881X_INTR_MASK, 0x1b },
	{ WSA881X_IOPAD_CTL, 0x00 },
	{ WSA881X_OTP_REG_28, 0x3f },
	{ WSA881X_OTP_REG_29, 0x3f },
	{ WSA881X_OTP_REG_30, 0x01 },
	{ WSA881X_OTP_REG_31, 0x01 },
	{ WSA881X_TEMP_ADC_CTRL, 0x03 },
	{ WSA881X_ADC_SEL_IBIAS, 0x45 },
	{ WSA881X_SPKR_DRV_GAIN, 0xc1 },
	{ WSA881X_SPKR_DAC_CTL, 0x42 },
	{ WSA881X_SPKR_BBM_CTL, 0x02 },
	{ WSA881X_SPKR_MISC_CTL1, 0x40 },
	{ WSA881X_SPKR_MISC_CTL2, 0x07 },
	{ WSA881X_SPKR_BIAS_INT, 0x5f },
	{ WSA881X_SPKR_BIAS_PSRR, 0x44 },
	{ WSA881X_BOOST_PS_CTL, 0xa0 },
	{ WSA881X_BOOST_PRESET_OUT1, 0xb7 },
	{ WSA881X_BOOST_LOOP_STABILITY, 0x8d },
	{ WSA881X_SPKR_PROT_ATEST2, 0x02 },
	{ WSA881X_BONGO_RESRV_REG1, 0x5e },
	{ WSA881X_BONGO_RESRV_REG2, 0x07 },
};

static struct regmap *wsa881x_i2c_regmap(struct wsa881x_i2c *wsa881x,
					 unsigned int reg, unsigned int *offset)
{
	if (reg >= WSA881X_DIGITAL_BASE && reg < WSA881X_ANALOG_BASE) {
		*offset = reg - WSA881X_DIGITAL_BASE;
		return wsa881x->digital_regmap;
	}

	if (reg >= WSA881X_ANALOG_BASE && reg < WSA881X_ANALOG_BASE + 0x100) {
		*offset = reg - WSA881X_ANALOG_BASE;
		return wsa881x->analog_regmap;
	}

	return NULL;
}

static int wsa881x_i2c_read(struct wsa881x_i2c *wsa881x, unsigned int reg,
			    unsigned int *val)
{
	struct regmap *regmap;
	unsigned int offset;

	regmap = wsa881x_i2c_regmap(wsa881x, reg, &offset);
	if (!regmap)
		return -EINVAL;

	return regmap_read(regmap, offset, val);
}

static int wsa881x_i2c_write(struct wsa881x_i2c *wsa881x, unsigned int reg,
			     unsigned int val)
{
	struct regmap *regmap;
	unsigned int offset;

	regmap = wsa881x_i2c_regmap(wsa881x, reg, &offset);
	if (!regmap)
		return -EINVAL;

	return regmap_write(regmap, offset, val);
}

static int wsa881x_i2c_update_bits(struct wsa881x_i2c *wsa881x,
				   unsigned int reg, unsigned int mask,
				   unsigned int val)
{
	struct regmap *regmap;
	unsigned int offset;

	regmap = wsa881x_i2c_regmap(wsa881x, reg, &offset);
	if (!regmap)
		return -EINVAL;

	return regmap_update_bits(regmap, offset, mask, val);
}

static int wsa881x_i2c_write_sequence(struct wsa881x_i2c *wsa881x,
				      const struct reg_sequence *seq,
				      size_t count)
{
	int ret;

	while (count--) {
		ret = wsa881x_i2c_write(wsa881x, seq->reg, seq->def);
		if (ret)
			return ret;
		seq++;
	}

	return 0;
}

static int wsa881x_i2c_hw_init(struct wsa881x_i2c *wsa881x)
{
	int ret;

	ret = wsa881x_i2c_write_sequence(wsa881x, wsa881x_rev_2_0,
					 ARRAY_SIZE(wsa881x_rev_2_0));
	if (ret)
		return ret;

	return 0;
}

static void wsa881x_i2c_reset_pulse(struct wsa881x_i2c *wsa881x)
{
	gpiod_set_value_cansleep(wsa881x->reset_gpio, 0);
	usleep_range(100, 200);
	gpiod_set_value_cansleep(wsa881x->reset_gpio, 1);
	usleep_range(100, 200);
	gpiod_set_value_cansleep(wsa881x->reset_gpio, 0);
	usleep_range(5000, 6000);
}

static unsigned int wsa881x_i2c_component_read(struct snd_soc_component *component,
					       unsigned int reg)
{
	struct wsa881x_i2c *wsa881x = snd_soc_component_get_drvdata(component);
	unsigned int val = 0;

	if (wsa881x_i2c_read(wsa881x, reg, &val))
		dev_err_ratelimited(component->dev, "failed to read register %#x\n", reg);

	return val;
}

static int wsa881x_i2c_component_write(struct snd_soc_component *component,
				       unsigned int reg, unsigned int val)
{
	struct wsa881x_i2c *wsa881x = snd_soc_component_get_drvdata(component);

	return wsa881x_i2c_write(wsa881x, reg, val);
}

static unsigned int wsa881x_i2c_pa_gain_value(struct wsa881x_i2c *wsa881x)
{
	return wsa881x->pa_gain == WSA881X_PA_GAIN_12_DB ? 0x40 : 0xc0;
}

static const char * const wsa881x_i2c_pa_gain_text[] = {
	"0 dB",
	"+12 dB",
};

static const struct soc_enum wsa881x_i2c_pa_gain_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(wsa881x_i2c_pa_gain_text),
			    wsa881x_i2c_pa_gain_text);

static int wsa881x_i2c_pa_gain_get(struct snd_kcontrol *control,
				   struct snd_ctl_elem_value *value)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(control);
	struct wsa881x_i2c *wsa881x = snd_soc_component_get_drvdata(component);

	value->value.enumerated.item[0] = wsa881x->pa_gain;
	return 0;
}

static int wsa881x_i2c_pa_gain_put(struct snd_kcontrol *control,
				   struct snd_ctl_elem_value *value)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(control);
	struct wsa881x_i2c *wsa881x = snd_soc_component_get_drvdata(component);
	unsigned int gain = value->value.enumerated.item[0];
	int ret;

	if (gain >= ARRAY_SIZE(wsa881x_i2c_pa_gain_text))
		return -EINVAL;
	if (gain == wsa881x->pa_gain)
		return 0;

	wsa881x->pa_gain = gain;
	if (!wsa881x->pa_active)
		return 1;

	ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_DRV_GAIN,
				      GENMASK(7, 4),
					      wsa881x_i2c_pa_gain_value(wsa881x));
	return ret < 0 ? ret : 1;
}

static int wsa881x_i2c_boost_apply(struct wsa881x_i2c *wsa881x, bool enable)
{
	int ret;

	ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_BOOST_EN_CTL,
				      WSA881X_BOOST_EN,
					      enable ? WSA881X_BOOST_EN : 0);
	if (ret < 0)
		return ret;

	usleep_range(1500, 1600);
	return 0;
}

static int wsa881x_i2c_boost_get(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(control);
	struct wsa881x_i2c *wsa881x = snd_soc_component_get_drvdata(component);

	value->value.integer.value[0] = wsa881x->boost_enabled;
	return 0;
}

static int wsa881x_i2c_boost_put(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(control);
	struct wsa881x_i2c *wsa881x = snd_soc_component_get_drvdata(component);
	bool enable = value->value.integer.value[0];
	int ret;

	if (enable == wsa881x->boost_enabled)
		return 0;

	wsa881x->boost_enabled = enable;
	if (!wsa881x->pa_active)
		return 1;

	ret = wsa881x_i2c_boost_apply(wsa881x, enable);
	return ret < 0 ? ret : 1;
}

static const struct snd_kcontrol_new wsa881x_i2c_controls[] = {
	SOC_ENUM_EXT("PA Gain", wsa881x_i2c_pa_gain_enum,
		     wsa881x_i2c_pa_gain_get, wsa881x_i2c_pa_gain_put),
	SOC_SINGLE_BOOL_EXT("BOOST Switch", 0, wsa881x_i2c_boost_get,
			    wsa881x_i2c_boost_put),
};

static int wsa881x_i2c_pa_event(struct snd_soc_dapm_widget *widget,
				struct snd_kcontrol *control, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(widget->dapm);
	struct wsa881x_i2c *wsa881x = snd_soc_component_get_drvdata(component);
	int ret;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_OCP_CTL,
					      WSA881X_SPKR_OCP_MASK,
					       WSA881X_SPKR_OCP_EN);
		if (ret < 0)
			return ret;
		return 0;

	case SND_SOC_DAPM_POST_PMU:
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_DRV_EN,
					      WSA881X_PA_EN, WSA881X_PA_EN);
		if (ret < 0)
			return ret;
		usleep_range(1000, 1100);
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_DRV_EN,
					      BIT(0), BIT(0));
		if (ret < 0)
			return ret;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_BIAS_CAL,
					      BIT(0), 0);
		if (ret < 0)
			return ret;
		usleep_range(1000, 1100);
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_DRV_GAIN,
					      GENMASK(7, 4),
					       wsa881x_i2c_pa_gain_value(wsa881x));
		if (ret < 0)
			return ret;
		wsa881x->pa_active = true;
		return 0;

	case SND_SOC_DAPM_PRE_PMD:
		wsa881x->pa_active = false;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_BIAS_CAL,
					      BIT(0), BIT(0));
		if (ret < 0)
			return ret;
		usleep_range(1000, 1100);
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_BIAS_CAL,
					      BIT(0), 0);
		if (ret < 0)
			return ret;
		msleep(20);
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_ANA_CTL,
					      0x03, 0);
		if (ret < 0)
			return ret;
		usleep_range(200, 300);
		return wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_DRV_EN,
						WSA881X_PA_EN, 0);

	case SND_SOC_DAPM_POST_PMD:
		return wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_OCP_CTL,
						WSA881X_SPKR_OCP_MASK,
						WSA881X_SPKR_OCP_EN |
						WSA881X_SPKR_OCP_HOLD);
	}

	return 0;
}

static int wsa881x_i2c_rdac_event(struct snd_soc_dapm_widget *widget,
				  struct snd_kcontrol *control, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(widget->dapm);
	struct wsa881x_i2c *wsa881x = snd_soc_component_get_drvdata(component);
	int ret;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = pinctrl_select_state(wsa881x->pinctrl, wsa881x->active_state);
		if (ret)
			return ret;

		wsa881x_i2c_reset_pulse(wsa881x);
		ret = wsa881x_i2c_hw_init(wsa881x);
		if (ret)
			goto err_sleep;

		ret = wsa881x_i2c_write(wsa881x, WSA881X_CDC_RST_CTL, 0x02);
		if (ret)
			goto err_sleep;
		ret = wsa881x_i2c_write(wsa881x, WSA881X_CDC_RST_CTL, 0x03);
		if (ret)
			goto err_sleep;
		ret = wsa881x_i2c_write(wsa881x, WSA881X_CLOCK_CONFIG, 0x01);
		if (ret)
			goto err_sleep;
		ret = wsa881x_i2c_write(wsa881x, WSA881X_CDC_DIG_CLK_CTL, 0x01);
		if (ret)
			goto err_sleep;
		ret = wsa881x_i2c_write(wsa881x, WSA881X_CDC_ANA_CLK_CTL, 0x01);
		if (ret)
			goto err_sleep;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_DAC_CTL,
					      BIT(1), BIT(1));
		if (ret < 0)
			goto err_sleep;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_TEMP_OP,
					      BIT(3), BIT(3));
		if (ret < 0)
			goto err_sleep;
		usleep_range(400, 500);
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_TEMP_OP,
					      BIT(2), BIT(2));
		if (ret < 0)
			goto err_sleep;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_PA_INT,
					      0xf0, 0x20);
		if (ret < 0)
			goto err_sleep;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_PA_INT,
					      0x0e, 0x0e);
		if (ret < 0)
			goto err_sleep;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_MISC_CTL1,
					      0xc0, 0x80);
		if (ret < 0)
			goto err_sleep;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_MISC_CTL1,
					      0x06, 0x06);
		if (ret < 0)
			goto err_sleep;
		ret = wsa881x_i2c_update_bits(wsa881x,
					      WSA881X_BOOST_LOOP_STABILITY,
					       0x03, 0x03);
		if (ret < 0)
			goto err_sleep;
		ret = wsa881x_i2c_write(wsa881x, WSA881X_BOOST_MISC2_CTL, 0x14);
		if (ret)
			goto err_sleep;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_BOOST_START_CTL,
					      0x80, 0x80);
		if (ret < 0)
			goto err_sleep;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_BOOST_START_CTL,
					      0x03, 0x00);
		if (ret < 0)
			goto err_sleep;
		ret = wsa881x_i2c_update_bits(wsa881x,
					      WSA881X_BOOST_SLOPE_COMP_ISENSE_FB,
					       0x0c, 0x04);
		if (ret < 0)
			goto err_sleep;
		ret = wsa881x_i2c_update_bits(wsa881x,
					      WSA881X_BOOST_SLOPE_COMP_ISENSE_FB,
					       0x03, 0x00);
		if (ret < 0)
			goto err_sleep;
		ret = wsa881x_i2c_update_bits(wsa881x,
					      WSA881X_BOOST_PRESET_OUT1,
					       0xf0, 0x70);
		if (ret < 0)
			goto err_sleep;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_ANA_CTL,
					      0x03, 0x03);
		if (ret < 0)
			goto err_sleep;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_DRV_EN,
					      BIT(3), BIT(3));
		if (ret < 0)
			goto err_sleep;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_ANA_CTL,
					      BIT(2), BIT(2));
		if (ret < 0)
			goto err_sleep;
		ret = wsa881x_i2c_update_bits(wsa881x,
					      WSA881X_BOOST_CURRENT_LIMIT,
					       0x0f, 0x08);
		if (ret < 0)
			goto err_sleep;
		if (wsa881x->boost_enabled) {
			ret = wsa881x_i2c_boost_apply(wsa881x, true);
			if (ret < 0)
				goto err_sleep;
		}
		return 0;

	case SND_SOC_DAPM_POST_PMU:
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_ANA_CTL,
					      BIT(3), 0);
		if (ret < 0)
			return ret;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_DAC_CTL,
					      BIT(5), BIT(5));
		if (ret < 0)
			return ret;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_DAC_CTL,
					      BIT(5), 0);
		if (ret < 0)
			return ret;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_DAC_CTL,
					      GENMASK(7, 6),
					       BIT(6) | BIT(7));
		if (ret < 0)
			return ret;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_BIAS_CAL,
					      BIT(0), BIT(0));
		if (ret < 0)
			return ret;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_OCP_CTL,
					      0x30, 0x30);
		if (ret < 0)
			return ret;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_OCP_CTL,
					      0x0c, 0x00);
		if (ret < 0)
			return ret;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_DRV_GAIN,
					      BIT(3), BIT(3));
		if (ret < 0)
			return ret;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_DRV_GAIN,
					      GENMASK(7, 4), 0x40);
		if (ret < 0)
			return ret;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_MISC_CTL1,
					      BIT(0), BIT(0));
		if (ret < 0)
			return ret;
		return 0;

	case SND_SOC_DAPM_PRE_PMD:
		return wsa881x_i2c_update_bits(wsa881x, WSA881X_SPKR_DAC_CTL,
						BIT(7), 0);

	case SND_SOC_DAPM_POST_PMD:
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_BOOST_EN_CTL,
					      WSA881X_BOOST_EN, 0);
		usleep_range(1500, 1600);
		if (ret < 0)
			return ret;
		ret = wsa881x_i2c_write(wsa881x, WSA881X_CDC_ANA_CLK_CTL, 0x00);
		if (ret)
			return ret;
		ret = wsa881x_i2c_write(wsa881x, WSA881X_CDC_DIG_CLK_CTL, 0x00);
		if (ret)
			return ret;
		ret = wsa881x_i2c_update_bits(wsa881x, WSA881X_TEMP_OP,
					      BIT(2) | BIT(3), 0);
		if (ret < 0)
			return ret;
		gpiod_set_value_cansleep(wsa881x->reset_gpio, 1);
		return pinctrl_select_state(wsa881x->pinctrl,
					    wsa881x->sleep_state);
	}

	return 0;

err_sleep:
	gpiod_set_value_cansleep(wsa881x->reset_gpio, 1);
	pinctrl_select_state(wsa881x->pinctrl, wsa881x->sleep_state);
	return ret;
}

static int wsa881x_i2c_vdd_event(struct snd_soc_dapm_widget *widget,
				 struct snd_kcontrol *control, int event)
{
	struct snd_soc_component *component;
	struct wsa881x_i2c *wsa881x;
	int load_ret;
	int ret;

	component = snd_soc_dapm_to_component(widget->dapm);
	wsa881x = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = regulator_set_load(wsa881x->vdd, WSA881X_VDD_LOAD_UA);
		if (ret)
			return ret;

		ret = regulator_enable(wsa881x->vdd);
		if (ret) {
			regulator_set_load(wsa881x->vdd, 0);
			return ret;
		}

		wsa881x->vdd_enabled = true;
		return 0;

	case SND_SOC_DAPM_POST_PMD:
		ret = regulator_disable(wsa881x->vdd);
		if (ret)
			return ret;

		wsa881x->vdd_enabled = false;
		load_ret = regulator_set_load(wsa881x->vdd, 0);
		return load_ret;
	}

	return 0;
}

static void wsa881x_i2c_disable_vdd(void *data)
{
	struct wsa881x_i2c *wsa881x = data;

	if (wsa881x->vdd_enabled)
		regulator_disable(wsa881x->vdd);
	wsa881x->vdd_enabled = false;
	regulator_set_load(wsa881x->vdd, 0);
}

static const struct snd_soc_dapm_widget wsa881x_i2c_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("vdd", SND_SOC_NOPM, 0, 0,
			    wsa881x_i2c_vdd_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_INPUT("WSA_IN"),
	SND_SOC_DAPM_DAC_E("RDAC Analog", NULL, SND_SOC_NOPM, 0, 0,
			   wsa881x_i2c_rdac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("WSA_SPKR PGA", SND_SOC_NOPM, 0, 0, NULL, 0,
			   wsa881x_i2c_pa_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUTPUT("WSA_SPKR"),
};

static const struct snd_soc_dapm_route wsa881x_i2c_dapm_routes[] = {
	{ "RDAC Analog", NULL, "vdd" },
	{ "RDAC Analog", NULL, "WSA_IN" },
	{ "WSA_SPKR PGA", NULL, "RDAC Analog" },
	{ "WSA_SPKR", NULL, "WSA_SPKR PGA" },
};

static const struct snd_soc_component_driver wsa881x_i2c_component_driver = {
	.name = "wsa881x-i2c",
	.read = wsa881x_i2c_component_read,
	.write = wsa881x_i2c_component_write,
	.controls = wsa881x_i2c_controls,
	.num_controls = ARRAY_SIZE(wsa881x_i2c_controls),
	.dapm_widgets = wsa881x_i2c_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wsa881x_i2c_dapm_widgets),
	.dapm_routes = wsa881x_i2c_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(wsa881x_i2c_dapm_routes),
};

static void wsa881x_i2c_assert_reset(void *data)
{
	struct gpio_desc *reset_gpio = data;

	gpiod_set_value_cansleep(reset_gpio, 1);
}

static int wsa881x_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct wsa881x_i2c *wsa881x;
	unsigned int chip_id[4];
	unsigned int val;
	u32 analog_addr;
	int ret;

	wsa881x = devm_kzalloc(dev, sizeof(*wsa881x), GFP_KERNEL);
	if (!wsa881x)
		return -ENOMEM;
	wsa881x->pa_gain = WSA881X_PA_GAIN_12_DB;
	wsa881x->boost_enabled = true;

	wsa881x->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(wsa881x->vdd))
		return dev_err_probe(dev, PTR_ERR(wsa881x->vdd),
				     "failed to get vdd regulator\n");

	ret = devm_add_action_or_reset(dev, wsa881x_i2c_disable_vdd, wsa881x);
	if (ret)
		return ret;

	wsa881x->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(wsa881x->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(wsa881x->reset_gpio),
				     "failed to get reset GPIO\n");

	wsa881x->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(wsa881x->pinctrl))
		return dev_err_probe(dev, PTR_ERR(wsa881x->pinctrl),
				     "failed to get WSA pinctrl\n");

	wsa881x->active_state = pinctrl_lookup_state(wsa881x->pinctrl, "active");
	if (IS_ERR(wsa881x->active_state))
		return dev_err_probe(dev, PTR_ERR(wsa881x->active_state),
				     "failed to get active pinctrl state\n");

	wsa881x->sleep_state = pinctrl_lookup_state(wsa881x->pinctrl, "sleep");
	if (IS_ERR(wsa881x->sleep_state))
		return dev_err_probe(dev, PTR_ERR(wsa881x->sleep_state),
				     "failed to get sleep pinctrl state\n");

	ret = pinctrl_select_state(wsa881x->pinctrl, wsa881x->sleep_state);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to select sleep pinctrl state\n");

	ret = devm_add_action_or_reset(dev, wsa881x_i2c_assert_reset,
				       wsa881x->reset_gpio);
	if (ret)
		return ret;

	usleep_range(1000, 1100);
	gpiod_set_value_cansleep(wsa881x->reset_gpio, 0);
	usleep_range(5000, 6000);

	wsa881x->digital_regmap = devm_regmap_init_i2c(client,
						       &wsa881x_i2c_regmap_config);
	if (IS_ERR(wsa881x->digital_regmap))
		return dev_err_probe(dev, PTR_ERR(wsa881x->digital_regmap),
				     "failed to initialize digital regmap\n");

	ret = of_property_read_u32_index(dev->of_node, "reg", 1, &analog_addr);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read analog I2C address\n");

	wsa881x->analog_client = devm_i2c_new_dummy_device(dev, client->adapter,
							   analog_addr);
	if (IS_ERR(wsa881x->analog_client))
		return dev_err_probe(dev, PTR_ERR(wsa881x->analog_client),
				     "failed to create analog I2C client\n");

	wsa881x->analog_regmap = devm_regmap_init_i2c(wsa881x->analog_client,
						      &wsa881x_i2c_regmap_config);
	if (IS_ERR(wsa881x->analog_regmap))
		return dev_err_probe(dev, PTR_ERR(wsa881x->analog_regmap),
				     "failed to initialize analog regmap\n");

	ret = wsa881x_i2c_read(wsa881x, WSA881X_CHIP_ID0, &chip_id[0]);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read chip ID0\n");
	ret = wsa881x_i2c_read(wsa881x, WSA881X_CHIP_ID1, &chip_id[1]);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read chip ID1\n");
	ret = wsa881x_i2c_read(wsa881x, WSA881X_CHIP_ID2, &chip_id[2]);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read chip ID2\n");
	ret = wsa881x_i2c_read(wsa881x, WSA881X_CHIP_ID3, &chip_id[3]);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read chip ID3\n");

	if (chip_id[0] != 0x00 || chip_id[1] != 0x01 ||
	    chip_id[2] != 0x01 || chip_id[3] != 0x02)
		return dev_err_probe(dev, -ENODEV,
				     "unexpected chip ID %02x %02x %02x %02x\n",
				     chip_id[0], chip_id[1], chip_id[2], chip_id[3]);

	ret = wsa881x_i2c_read(wsa881x, WSA881X_TEMP_OP, &val);
	if (ret)
		return dev_err_probe(dev, ret, "failed to access analog register bank\n");

	ret = wsa881x_i2c_hw_init(wsa881x);
	if (ret)
		return dev_err_probe(dev, ret, "failed to initialize WSA881x\n");

	i2c_set_clientdata(client, wsa881x);

	ret = devm_snd_soc_register_component(dev,
					      &wsa881x_i2c_component_driver,
					      NULL, 0);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register ASoC component\n");

	gpiod_set_value_cansleep(wsa881x->reset_gpio, 1);

	dev_info(dev, "WSA881x rev 2.0 initialized at conservative gain\n");

	return 0;
}

static const struct of_device_id wsa881x_i2c_of_match[] = {
	{ .compatible = "qcom,wsa881x-i2c" },
	{ }
};
MODULE_DEVICE_TABLE(of, wsa881x_i2c_of_match);

static struct i2c_driver wsa881x_i2c_driver = {
	.driver = {
		.name = "wsa881x-i2c",
		.of_match_table = wsa881x_i2c_of_match,
	},
	.probe = wsa881x_i2c_probe,
};
module_i2c_driver(wsa881x_i2c_driver);

MODULE_DESCRIPTION("Qualcomm WSA881x I2C/PDM amplifier driver");
MODULE_LICENSE("GPL");
