// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Paul Kocialkowski <contact@paulk.fr>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>

/* Clock rate */

#define OV4689_EXTCLK_RATE			24000000

/* Register definitions */

/* System */

#define OV4689_SW_STANDBY_REG			0x100
#define OV4689_SW_STANDBY_STREAM_ON		BIT(0)

#define OV4689_SW_RESET_REG			0x103
#define OV4689_SW_RESET_RESET			BIT(0)

#define OV4689_CHIP_ID_H_REG			0x300a
#define OV4689_CHIP_ID_H_VALUE			0x46
#define OV4689_CHIP_ID_L_REG			0x300b
#define OV4689_CHIP_ID_L_VALUE			0x88

/* Macros */

#define ov4689_subdev_sensor(subdev) \
	container_of(subdev, struct ov4689_sensor, subdev)

#define ov4689_ctrl_subdev(ctrl) \
	(&container_of(ctrl->handler, struct ov4689_sensor, ctrls.handler)->subdev)

/* Data structures */

struct ov4689_register_value {
	u16 address;
	u8 value;
	unsigned int delay_ms;
};

/*
 * PLL1 Clock Tree:
 *
 * +-< EXTCLK
 * |
 * +-+ pll_pre_div (0x3037 [3:0], special values: 5: 1.5, 7: 2.5)
 *   |
 *   +-+ pll_mul (0x3036 [7:0])
 *     |
 *     +-+ sys_div (0x3035 [7:4])
 *       |
 *       +-+ mipi_div (0x3035 [3:0])
 *       | |
 *       | +-> MIPI_SCLK
 *       | |
 *       | +-+ mipi_phy_div (2)
 *       |   |
 *       |   +-> MIPI_CLK
 *       |
 *       +-+ root_div (0x3037 [4])
 *         |
 *         +-+ bit_div (0x3034 [3:0], 8 bits: 2, 10 bits: 2.5, other: 1)
 *           |
 *           +-+ sclk_div (0x3106 [3:2])
 *             |
 *             +-> SCLK
 *             |
 *             +-+ mipi_div (0x3035, 1: PCLK = SCLK)
 *               |
 *               +-> PCLK
 */

struct ov4689_pll1_config {
	unsigned int pll_pre_div;
	unsigned int pll_mul;
	unsigned int sys_div;
	unsigned int root_div;
	unsigned int sclk_div;
	unsigned int mipi_div;
};

/*
 * PLL2 Clock Tree:
 *
 * +-< EXTCLK
 * |
 * +-+ plls_pre_div (0x303d [5:4], special values: 0: 1, 1: 1.5)
 *   |
 *   +-+ plls_div_r (0x303d [2])
 *     |
 *     +-+ plls_mul (0x303b [4:0])
 *       |
 *       +-+ sys_div (0x303c [3:0])
 *         |
 *         +-+ sel_div (0x303d [1:0], special values: 0: 1, 3: 2.5)
 *           |
 *           +-> ADCLK
 */

struct ov4689_pll2_config {
	unsigned int plls_pre_div;
	unsigned int plls_div_r;
	unsigned int plls_mul;
	unsigned int sys_div;
	unsigned int sel_div;
};

/*
 * General formulas for (array-centered) mode calculation:
 * - photo_array_width = 2624
 * - crop_start_x = (photo_array_width - output_size_x) / 2
 * - crop_end_x = crop_start_x + offset_x + output_size_x - 1
 *
 * - photo_array_height = 1956
 * - crop_start_y = (photo_array_height - output_size_y) / 2
 * - crop_end_y = crop_start_y + offset_y + output_size_y - 1
 */

struct ov4689_mode {
	unsigned int crop_start_x;
	unsigned int offset_x;
	unsigned int output_size_x;
	unsigned int crop_end_x;
	unsigned int hts;

	unsigned int crop_start_y;
	unsigned int offset_y;
	unsigned int output_size_y;
	unsigned int crop_end_y;
	unsigned int vts;

	bool binning_x;
	bool binning_y;

	unsigned int inc_x_odd;
	unsigned int inc_x_even;
	unsigned int inc_y_odd;
	unsigned int inc_y_even;

	/* 8-bit frame interval followed by 10-bit frame interval. */
	struct v4l2_fract frame_interval[2];

	/* 8-bit config followed by 10-bit config. */
	const struct ov4689_pll1_config *pll1_config[2];
	const struct ov4689_pll2_config *pll2_config;

	const struct ov4689_register_value *register_values;
	unsigned int register_values_count;
};

struct ov4689_state {
	const struct ov4689_mode *mode;
	u32 mbus_code;

	bool streaming;
};

struct ov4689_ctrls {
	struct v4l2_ctrl *exposure_auto;
	struct v4l2_ctrl *exposure;

	struct v4l2_ctrl *gain_auto;
	struct v4l2_ctrl *gain;

	struct v4l2_ctrl *white_balance_auto;
	struct v4l2_ctrl *red_balance;
	struct v4l2_ctrl *blue_balance;

	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;

	struct v4l2_ctrl_handler handler;
} __packed;

struct ov4689_sensor {
	struct device *dev;
	struct i2c_client *i2c_client;
	struct gpio_desc *reset;
	struct gpio_desc *powerdown;
	struct regulator *avdd;
	struct regulator *dvdd;
	struct regulator *dovdd;
	struct clk *extclk;

	struct v4l2_fwnode_endpoint endpoint;
	struct v4l2_subdev subdev;
	struct media_pad pad;

	struct mutex mutex;

	struct ov4689_state state;
	struct ov4689_ctrls ctrls;
};

/* Static definitions */

/*
 * EXTCLK = 24 MHz
 * SCLK  = 84 MHz
 * PCLK  = 84 MHz
 */
static const struct ov4689_pll1_config ov4689_pll1_config_native_8_bits = {
	.pll_pre_div	= 3,
	.pll_mul	= 84,
	.sys_div	= 2,
	.root_div	= 1,
	.sclk_div	= 1,
	.mipi_div	= 1,
};

/*
 * EXTCLK = 24 MHz
 * SCLK  = 84 MHz
 * PCLK  = 84 MHz
 */
static const struct ov4689_pll1_config ov4689_pll1_config_native_10_bits = {
	.pll_pre_div	= 3,
	.pll_mul	= 105,
	.sys_div	= 2,
	.root_div	= 1,
	.sclk_div	= 1,
	.mipi_div	= 1,
};

/*
 * EXTCLK = 24 MHz
 * ADCLK = 200 MHz
 */
static const struct ov4689_pll2_config ov4689_pll2_config_native = {
	.plls_pre_div	= 3,
	.plls_div_r	= 1,
	.plls_mul	= 25,
	.sys_div	= 1,
	.sel_div	= 1,
};

static const struct ov4689_mode ov4689_modes[] = {
	/* 2592x1944 */
	{
		/* Horizontal */
		.crop_start_x	= 16,
		.offset_x	= 0,
		.output_size_x	= 2592,
		.crop_end_x	= 2607,
		.hts		= 2816,

		/* Vertical */
		.crop_start_y	= 6,
		.offset_y	= 0,
		.output_size_y	= 1944,
		.crop_end_y	= 1949,
		.vts		= 1984,

		/* Subsample increase */
		.inc_x_odd	= 1,
		.inc_x_even	= 1,
		.inc_y_odd	= 1,
		.inc_y_even	= 1,

		/* Frame Interval */
		.frame_interval	= {
			{ 1,	15 },
			{ 1,	15 },
		},

		/* PLL */
		.pll1_config	= {
			&ov4689_pll1_config_native_8_bits,
			&ov4689_pll1_config_native_10_bits,
		},
		.pll2_config	= &ov4689_pll2_config_native,
	},
	/* 1600x1200 (UXGA) */
	{
		/* Horizontal */
		.crop_start_x	= 512,
		.offset_x	= 0,
		.output_size_x	= 1600,
		.crop_end_x	= 2111,
		.hts		= 2816,

		/* Vertical */
		.crop_start_y	= 378,
		.offset_y	= 0,
		.output_size_y	= 1200,
		.crop_end_y	= 1577,
		.vts		= 1984,

		/* Subsample increase */
		.inc_x_odd	= 1,
		.inc_x_even	= 1,
		.inc_y_odd	= 1,
		.inc_y_even	= 1,

		/* Frame Interval */
		.frame_interval	= {
			{ 1,	15 },
			{ 1,	15 },
		},

		/* PLL */
		.pll1_config	= {
			&ov4689_pll1_config_native_8_bits,
			&ov4689_pll1_config_native_10_bits,
		},
		.pll2_config	= &ov4689_pll2_config_native,
	},
	/* 1920x1080 (Full HD) */
	{
		/* Horizontal */
		.crop_start_x	= 352,
		.offset_x	= 0,
		.output_size_x	= 1920,
		.crop_end_x	= 2271,
		.hts		= 2816,

		/* Vertical */
		.crop_start_y	= 438,
		.offset_y	= 0,
		.output_size_y	= 1080,
		.crop_end_y	= 1517,
		.vts		= 1984,

		/* Subsample increase */
		.inc_x_odd	= 1,
		.inc_x_even	= 1,
		.inc_y_odd	= 1,
		.inc_y_even	= 1,

		/* Frame Interval */
		.frame_interval	= {
			{ 1,	15 },
			{ 1,	15 },
		},

		/* PLL */
		.pll1_config	= {
			&ov4689_pll1_config_native_8_bits,
			&ov4689_pll1_config_native_10_bits,
		},
		.pll2_config	= &ov4689_pll2_config_native,
	},
	/* 1280x960 */
	{
		/* Horizontal */
		.crop_start_x	= 16,
		.offset_x	= 8,
		.output_size_x	= 1280,
		.crop_end_x	= 2607,
		.hts		= 1912,

		/* Vertical */
		.crop_start_y	= 6,
		.offset_y	= 6,
		.output_size_y	= 960,
		.crop_end_y	= 1949,
		.vts		= 1496,

		/* Binning */
		.binning_x	= true,

		/* Subsample increase */
		.inc_x_odd	= 3,
		.inc_x_even	= 1,
		.inc_y_odd	= 3,
		.inc_y_even	= 1,

		/* Frame Interval */
		.frame_interval	= {
			{ 1,	30 },
			{ 1,	30 },
		},

		/* PLL */
		.pll1_config	= {
			&ov4689_pll1_config_native_8_bits,
			&ov4689_pll1_config_native_10_bits,
		},
		.pll2_config	= &ov4689_pll2_config_native,
	},
	/* 1280x720 (HD) */
	{
		/* Horizontal */
		.crop_start_x	= 16,
		.offset_x	= 8,
		.output_size_x	= 1280,
		.crop_end_x	= 2607,
		.hts		= 1912,

		/* Vertical */
		.crop_start_y	= 254,
		.offset_y	= 2,
		.output_size_y	= 720,
		.crop_end_y	= 1701,
		.vts		= 1496,

		/* Binning */
		.binning_x	= true,

		/* Subsample increase */
		.inc_x_odd	= 3,
		.inc_x_even	= 1,
		.inc_y_odd	= 3,
		.inc_y_even	= 1,

		/* Frame Interval */
		.frame_interval	= {
			{ 1,	30 },
			{ 1,	30 },
		},

		/* PLL */
		.pll1_config	= {
			&ov4689_pll1_config_native_8_bits,
			&ov4689_pll1_config_native_10_bits,
		},
		.pll2_config	= &ov4689_pll2_config_native,
	},
	/* 640x480 (VGA) */
	{
		/* Horizontal */
		.crop_start_x	= 0,
		.offset_x	= 8,
		.output_size_x	= 640,
		.crop_end_x	= 2623,
		.hts		= 1896,

		/* Vertical */
		.crop_start_y	= 0,
		.offset_y	= 2,
		.output_size_y	= 480,
		.crop_end_y	= 1953,
		.vts		= 984,

		/* Binning */
		.binning_x	= true,

		/* Subsample increase */
		.inc_x_odd	= 7,
		.inc_x_even	= 1,
		.inc_y_odd	= 7,
		.inc_y_even	= 1,

		/* Frame Interval */
		.frame_interval	= {
			{ 1,	30 },
			{ 1,	30 },
		},

		/* PLL */
		.pll1_config	= {
			&ov4689_pll1_config_native_8_bits,
			&ov4689_pll1_config_native_10_bits,
		},
		.pll2_config	= &ov4689_pll2_config_native,
	},
};

static const u32 ov4689_mbus_codes[] = {
	MEDIA_BUS_FMT_SBGGR10_1X10,
};

static const s64 ov4689_link_freq_menu[] = {
	210000000,
	168000000,
};

/* Input/Output */

static int ov4689_read(struct ov4689_sensor *sensor, u16 address, u8 *value)
{
	unsigned char data[2] = { address >> 8, address & 0xff };
	struct i2c_client *client = sensor->i2c_client;
	int ret;

	ret = i2c_master_send(client, data, sizeof(data));
	if (ret < 0) {
		dev_dbg(&client->dev, "i2c send error at address %#04x\n",
			address);
		return ret;
	}

	ret = i2c_master_recv(client, value, 1);
	if (ret < 0) {
		dev_dbg(&client->dev, "i2c recv error at address %#04x\n",
			address);
		return ret;
	}

	return 0;
}

static int ov4689_write(struct ov4689_sensor *sensor, u16 address, u8 value)
{
	unsigned char data[3] = { address >> 8, address & 0xff, value };
	struct i2c_client *client = sensor->i2c_client;
	int ret;

	ret = i2c_master_send(client, data, sizeof(data));
	if (ret < 0) {
		dev_dbg(&client->dev, "i2c send error at address %#04x\n",
			address);
		return ret;
	}

	return 0;
}

static int ov4689_write_sequence(struct ov4689_sensor *sensor,
				 const struct ov4689_register_value *sequence,
				 unsigned int sequence_count)
{
	unsigned int i;
	int ret = 0;

	for (i = 0; i < sequence_count; i++) {
		ret = ov4689_write(sensor, sequence[i].address,
				   sequence[i].value);
		if (ret)
			break;

		if (sequence[i].delay_ms)
			msleep(sequence[i].delay_ms);
	}

	return ret;
}

static int ov4689_update_bits(struct ov4689_sensor *sensor, u16 address,
			      u8 mask, u8 bits)
{
	u8 value = 0;
	int ret;

	ret = ov4689_read(sensor, address, &value);
	if (ret)
		return ret;

	value &= ~mask;
	value |= bits;

	ret = ov4689_write(sensor, address, value);
	if (ret)
		return ret;

	return 0;
}

/* Sensor */

static int ov4689_sw_reset(struct ov4689_sensor *sensor)
{
	int ret;

	ret = ov4689_write(sensor, OV4689_SW_RESET_REG, OV4689_SW_RESET_RESET);
	if (ret < 0)
		return ret;

	return 0;
}

static int ov4689_sw_standby(struct ov4689_sensor *sensor, int standby)
{
	u8 value = 0;
	int ret;

	if (!standby)
		value = OV4689_SW_STANDBY_STREAM_ON;

	ret = ov4689_write(sensor, OV4689_SW_STANDBY_REG, value);
	if (ret < 0)
		return ret;

	return 0;
}

static int ov4689_chip_id_check(struct ov4689_sensor *sensor)
{
	u16 regs[] = { OV4689_CHIP_ID_H_REG, OV4689_CHIP_ID_L_REG };
	u8 values[] = { OV4689_CHIP_ID_H_VALUE, OV4689_CHIP_ID_L_VALUE };
	unsigned int i;
	u8 value;
	int ret;

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		ret = ov4689_read(sensor, regs[i], &value);
		if (ret < 0)
			return ret;

		if (value != values[i]) {
			dev_err(sensor->dev,
				"chip id value mismatch: %#x instead of %#x\n",
				value, values[i]);
			return -EINVAL;
		}
	}

	return 0;
}

/* State */

static int ov4689_state_configure(struct ov4689_sensor *sensor,
				  const struct ov4689_mode *mode,
				  u32 mbus_code)
{
	if (sensor->state.streaming)
		return -EBUSY;

	sensor->state.mode = mode;
	sensor->state.mbus_code = mbus_code;

	return 0;
}

static int ov4689_state_init(struct ov4689_sensor *sensor)
{
	return ov4689_state_configure(sensor, &ov4689_modes[0],
				      ov4689_mbus_codes[0]);
}

/* Sensor Base */

static int ov4689_sensor_init(struct ov4689_sensor *sensor)
{
	int ret;

	ret = ov4689_sw_reset(sensor);
	if (ret) {
		dev_err(sensor->dev, "failed to perform sw reset\n");
		return ret;
	}

	ret = ov4689_sw_standby(sensor, 1);
	if (ret) {
		dev_err(sensor->dev, "failed to set sensor standby\n");
		return ret;
	}

	printk(KERN_ERR "%s: go chip ID check\n", __func__);

	ret = ov4689_chip_id_check(sensor);
	if (ret) {
		dev_err(sensor->dev, "failed to check sensor chip id\n");
		return ret;
	}

	/* Configure current mode. */
	ret = ov4689_state_configure(sensor, sensor->state.mode,
				     sensor->state.mbus_code);
	if (ret) {
		dev_err(sensor->dev, "failed to configure state\n");
		return ret;
	}

	return 0;
}

static int ov4689_sensor_power(struct ov4689_sensor *sensor, bool on)
{
	/* Keep initialized to zero for disable label. */
	int ret = 0;

	/*
	 * General notes about the power sequence:
	 * - power-down GPIO must be active (low) during power-on;
	 * - reset GPIO state does not matter during power-on;
	 * - EXTCLK must be provided 1 ms before register access;
	 * - 10 ms are needed between power-down deassert and register access.
	 */

	printk(KERN_ERR "%s: go enable\n", __func__);

	/* Note that regulator-and-GPIO-based power is untested. */
	if (on) {
		gpiod_set_value_cansleep(sensor->reset, 1);
		gpiod_set_value_cansleep(sensor->powerdown, 1);

		ret = regulator_enable(sensor->dovdd);
		if (ret) {
			dev_err(sensor->dev,
				"failed to enable DOVDD regulator\n");
			goto disable;
		}

		ret = regulator_enable(sensor->avdd);
		if (ret) {
			dev_err(sensor->dev,
				"failed to enable AVDD regulator\n");
			goto disable;
		}

		ret = regulator_enable(sensor->dvdd);
		if (ret) {
			dev_err(sensor->dev,
				"failed to enable DVDD regulator\n");
			goto disable;
		}

		/* According to OV4689 power up diagram. */
		usleep_range(5000, 10000);

		ret = clk_prepare_enable(sensor->extclk);
		if (ret) {
			dev_err(sensor->dev, "failed to enable EXTCLK clock\n");
			goto disable;
		}

		gpiod_set_value_cansleep(sensor->reset, 0);
		gpiod_set_value_cansleep(sensor->powerdown, 0);

		usleep_range(20000, 25000);
	} else {
disable:
		gpiod_set_value_cansleep(sensor->powerdown, 1);
		gpiod_set_value_cansleep(sensor->reset, 1);

		clk_disable_unprepare(sensor->extclk);

		regulator_disable(sensor->dvdd);

		if (sensor->avdd)
			regulator_disable(sensor->avdd);

		regulator_disable(sensor->dovdd);
	}

	return ret;
}

/* Controls */


static const struct v4l2_ctrl_ops ov4689_ctrl_ops = {
};

static int ov4689_ctrls_init(struct ov4689_sensor *sensor)
{
	struct ov4689_ctrls *ctrls = &sensor->ctrls;
	struct v4l2_ctrl_handler *handler = &ctrls->handler;
	const struct v4l2_ctrl_ops *ops = &ov4689_ctrl_ops;
	int ret;

	v4l2_ctrl_handler_init(handler, 32);

	/* Use our mutex for ctrl locking. */
	handler->lock = &sensor->mutex;

	/* Exposure */

	ctrls->exposure_auto = v4l2_ctrl_new_std_menu(handler, ops,
						      V4L2_CID_EXPOSURE_AUTO,
						      V4L2_EXPOSURE_MANUAL, 0,
						      V4L2_EXPOSURE_AUTO);

	ctrls->exposure = v4l2_ctrl_new_std(handler, ops, V4L2_CID_EXPOSURE,
					    16, 1048575, 16, 512);
	ctrls->exposure->flags |= V4L2_CTRL_FLAG_VOLATILE;

	v4l2_ctrl_auto_cluster(2, &ctrls->exposure_auto, 1, true);

	/* Gain */

	ctrls->gain_auto =
		v4l2_ctrl_new_std(handler, ops, V4L2_CID_AUTOGAIN, 0, 1, 1, 1);

	ctrls->gain = v4l2_ctrl_new_std(handler, ops, V4L2_CID_GAIN, 16, 1023,
					16, 16);
	ctrls->gain->flags |= V4L2_CTRL_FLAG_VOLATILE;

	v4l2_ctrl_auto_cluster(2, &ctrls->gain_auto, 0, true);

	/* White Balance */

	ctrls->white_balance_auto =
		v4l2_ctrl_new_std(handler, ops, V4L2_CID_AUTO_WHITE_BALANCE, 0,
				  1, 1, 1);

	ctrls->red_balance = v4l2_ctrl_new_std(handler, ops,
					       V4L2_CID_RED_BALANCE, 0, 4095,
					       1, 1024);

	ctrls->blue_balance = v4l2_ctrl_new_std(handler, ops,
						V4L2_CID_BLUE_BALANCE, 0, 4095,
						1, 1024);

	v4l2_ctrl_auto_cluster(3, &ctrls->white_balance_auto, 0, false);

	/* Flip */

	v4l2_ctrl_new_std(handler, ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_VFLIP, 0, 1, 1, 0);

	/* MIPI CSI-2 */

	ctrls->link_freq =
		v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				       ARRAY_SIZE(ov4689_link_freq_menu) - 1,
				       0, ov4689_link_freq_menu);

	ctrls->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ctrls->pixel_rate =
		v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE, 1,
				  INT_MAX, 1, 168000000);

	ctrls->pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	if (handler->error) {
		ret = handler->error;
		goto error_ctrls;
	}

	sensor->subdev.ctrl_handler = handler;

	return 0;

error_ctrls:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

/* Subdev Video Operations */

static int ov4689_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct ov4689_sensor *sensor = ov4689_subdev_sensor(subdev);
	struct ov4689_state *state = &sensor->state;
	int ret = 0;

	if (enable) {
		ret = pm_runtime_get_sync(sensor->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(sensor->dev);
			return ret;
		}
	}

	mutex_lock(&sensor->mutex);
	ret = ov4689_sw_standby(sensor, !enable);
	mutex_unlock(&sensor->mutex);

	if (ret)
		return ret;

	state->streaming = !!enable;

	if (!enable)
		pm_runtime_put(sensor->dev);

	return 0;
}

static int ov4689_g_frame_interval(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_frame_interval *interval)
{
	struct ov4689_sensor *sensor = ov4689_subdev_sensor(subdev);
	const struct ov4689_mode *mode;
	int ret = 0;

	mutex_lock(&sensor->mutex);

	mode = sensor->state.mode;

	switch (sensor->state.mbus_code) {
	case MEDIA_BUS_FMT_SBGGR8_1X8:
		interval->interval = mode->frame_interval[0];
		break;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
		interval->interval = mode->frame_interval[1];
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&sensor->mutex);

	return ret;
}

static const struct v4l2_subdev_video_ops ov4689_subdev_video_ops = {
	.s_stream		= ov4689_s_stream,
	.g_frame_interval	= ov4689_g_frame_interval,
	.s_frame_interval	= ov4689_g_frame_interval,
};

/* Subdev Pad Operations */

static int ov4689_enum_mbus_code(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_pad_config *config,
				 struct v4l2_subdev_mbus_code_enum *code_enum)
{
	if (code_enum->index >= ARRAY_SIZE(ov4689_mbus_codes))
		return -EINVAL;

	code_enum->code = ov4689_mbus_codes[code_enum->index];

	return 0;
}

static void ov4689_mbus_format_fill(struct v4l2_mbus_framefmt *mbus_format,
				    u32 mbus_code,
				    const struct ov4689_mode *mode)
{
	mbus_format->width = mode->output_size_x;
	mbus_format->height = mode->output_size_y;
	mbus_format->code = mbus_code;

	mbus_format->field = V4L2_FIELD_NONE;
	mbus_format->colorspace = V4L2_COLORSPACE_RAW;
	mbus_format->ycbcr_enc =
		V4L2_MAP_YCBCR_ENC_DEFAULT(mbus_format->colorspace);
	mbus_format->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	mbus_format->xfer_func =
		V4L2_MAP_XFER_FUNC_DEFAULT(mbus_format->colorspace);
}

static int ov4689_get_fmt(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_pad_config *config,
			  struct v4l2_subdev_format *format)
{
	struct ov4689_sensor *sensor = ov4689_subdev_sensor(subdev);
	struct v4l2_mbus_framefmt *mbus_format = &format->format;

	mutex_lock(&sensor->mutex);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		*mbus_format = *v4l2_subdev_get_try_format(subdev, config,
							   format->pad);
	else
		ov4689_mbus_format_fill(mbus_format, sensor->state.mbus_code,
					sensor->state.mode);

	mutex_unlock(&sensor->mutex);

	return 0;
}

static int ov4689_set_fmt(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_pad_config *config,
			  struct v4l2_subdev_format *format)
{
	struct ov4689_sensor *sensor = ov4689_subdev_sensor(subdev);
	struct v4l2_mbus_framefmt *mbus_format = &format->format;
	const struct ov4689_mode *mode;
	u32 mbus_code = 0;
	unsigned int index;
	int ret = 0;

	mutex_lock(&sensor->mutex);

	if (sensor->state.streaming) {
		ret = -EBUSY;
		goto complete;
	}

	/* Try to find requested mbus code. */
	for (index = 0; index < ARRAY_SIZE(ov4689_mbus_codes); index++) {
		if (ov4689_mbus_codes[index] == mbus_format->code) {
			mbus_code = mbus_format->code;
			break;
		}
	}

	/* Fallback to default. */
	if (!mbus_code)
		mbus_code = ov4689_mbus_codes[0];

	/* Find the mode with nearest dimensions. */
	mode = v4l2_find_nearest_size(ov4689_modes, ARRAY_SIZE(ov4689_modes),
				      output_size_x, output_size_y,
				      mbus_format->width, mbus_format->height);
	if (!mode)
		return -EINVAL;

	ov4689_mbus_format_fill(mbus_format, mbus_code, mode);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_get_try_format(subdev, config, format->pad) =
			*mbus_format;
	} else if (sensor->state.mode != mode ||
		   sensor->state.mbus_code != mbus_code) {
		ret = ov4689_state_configure(sensor, mode, mbus_code);
		if (ret)
			goto complete;
	}

complete:
	mutex_unlock(&sensor->mutex);

	return ret;
}

static int ov4689_enum_frame_size(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_pad_config *config,
				  struct v4l2_subdev_frame_size_enum *size_enum)
{
	const struct ov4689_mode *mode;

	if (size_enum->index >= ARRAY_SIZE(ov4689_modes))
		return -EINVAL;

	mode = &ov4689_modes[size_enum->index];

	size_enum->min_width = size_enum->max_width = mode->output_size_x;
	size_enum->min_height = size_enum->max_height = mode->output_size_y;

	return 0;
}

static int ov4689_enum_frame_interval(struct v4l2_subdev *subdev,
				      struct v4l2_subdev_pad_config *config,
				      struct v4l2_subdev_frame_interval_enum *interval_enum)
{
	const struct ov4689_mode *mode = NULL;
	unsigned int mode_index;
	unsigned int interval_index;

	if (interval_enum->index > 0)
		return -EINVAL;

	/*
	 * Multiple modes with the same dimensions may have different frame
	 * intervals, so look up each relevant mode.
	 */
	for (mode_index = 0, interval_index = 0;
	     mode_index < ARRAY_SIZE(ov4689_modes); mode_index++) {
		mode = &ov4689_modes[mode_index];

		if (mode->output_size_x == interval_enum->width &&
		    mode->output_size_y == interval_enum->height) {
			if (interval_index == interval_enum->index)
				break;

			interval_index++;
		}
	}

	if (mode_index == ARRAY_SIZE(ov4689_modes) || !mode)
		return -EINVAL;

	switch (interval_enum->code) {
	case MEDIA_BUS_FMT_SBGGR8_1X8:
		interval_enum->interval = mode->frame_interval[0];
		break;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
		interval_enum->interval = mode->frame_interval[1];
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_subdev_pad_ops ov4689_subdev_pad_ops = {
	.enum_mbus_code		= ov4689_enum_mbus_code,
	.get_fmt		= ov4689_get_fmt,
	.set_fmt		= ov4689_set_fmt,
	.enum_frame_size	= ov4689_enum_frame_size,
	.enum_frame_interval	= ov4689_enum_frame_interval,
};

static const struct v4l2_subdev_ops ov4689_subdev_ops = {
	.video		= &ov4689_subdev_video_ops,
	.pad		= &ov4689_subdev_pad_ops,
};

static int ov4689_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct ov4689_sensor *sensor = ov4689_subdev_sensor(subdev);
	struct ov4689_state *state = &sensor->state;
	int ret = 0;

	mutex_lock(&sensor->mutex);

	if (state->streaming) {
		ret = ov4689_sw_standby(sensor, true);
		if (ret)
			goto complete;
	}

	ret = ov4689_sensor_power(sensor, false);
	if (ret) {
		ov4689_sw_standby(sensor, false);
		goto complete;
	}

complete:
	mutex_unlock(&sensor->mutex);

	return ret;
}

static int ov4689_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct ov4689_sensor *sensor = ov4689_subdev_sensor(subdev);
	struct ov4689_state *state = &sensor->state;
	int ret = 0;

	mutex_lock(&sensor->mutex);

	ret = ov4689_sensor_power(sensor, true);
	if (ret)
		goto complete;

	ret = ov4689_sensor_init(sensor);
	if (ret)
		goto error_power;

	ret = __v4l2_ctrl_handler_setup(&sensor->ctrls.handler);
	if (ret)
		goto error_power;

	if (state->streaming) {
		ret = ov4689_sw_standby(sensor, false);
		if (ret)
			goto error_power;
	}

	goto complete;

error_power:
	ov4689_sensor_power(sensor, false);

complete:
	mutex_unlock(&sensor->mutex);

	return ret;
}

static int ov4689_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct fwnode_handle *handle;
	struct ov4689_sensor *sensor;
	struct v4l2_subdev *subdev;
	struct media_pad *pad;
	unsigned long rate;
	int ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->dev = dev;
	sensor->i2c_client = client;

	/* Graph Endpoint */

	handle = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!handle) {
		dev_err(dev, "unable to find enpoint node\n");
		return -EINVAL;
	}

	sensor->endpoint.bus_type = V4L2_MBUS_CSI2_DPHY;

	ret = v4l2_fwnode_endpoint_alloc_parse(handle, &sensor->endpoint);
	fwnode_handle_put(handle);
	if (ret) {
		dev_err(dev, "failed to parse endpoint node\n");
		return ret;
	}

	/* GPIOs */

	sensor->powerdown = devm_gpiod_get_optional(dev, "powerdown",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->powerdown)) {
		ret = PTR_ERR(sensor->powerdown);
		goto error_endpoint;
	}

	sensor->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->reset)) {
		ret = PTR_ERR(sensor->reset);
		goto error_endpoint;
	}

	/* Regulators */

	/* DVDD: digital core */
	sensor->dvdd = devm_regulator_get(dev, "dvdd");
	if (IS_ERR(sensor->dvdd)) {
		dev_err(dev, "cannot get DVDD (digital core) regulator\n");
		ret = PTR_ERR(sensor->dvdd);
		goto error_endpoint;
	}

	/* DOVDD: digital I/O */
	sensor->dovdd = devm_regulator_get(dev, "dovdd");
	if (IS_ERR(sensor->dvdd)) {
		dev_err(dev, "cannot get DOVDD (digital I/O) regulator\n");
		ret = PTR_ERR(sensor->dvdd);
		goto error_endpoint;
	}

	/* AVDD: analog */
	sensor->avdd = devm_regulator_get_optional(dev, "avdd");
	if (IS_ERR(sensor->avdd)) {
		dev_info(dev, "no AVDD regulator provided, using internal\n");
		sensor->avdd = NULL;
	}

	/* External Clock */

	sensor->extclk = devm_clk_get(dev, NULL);
	if (IS_ERR(sensor->extclk)) {
		dev_err(dev, "failed to get external clock\n");
		ret = PTR_ERR(sensor->extclk);
		goto error_endpoint;
	}

	rate = clk_get_rate(sensor->extclk);
	if (rate != OV4689_EXTCLK_RATE) {
		dev_err(dev, "clock rate %lu Hz is unsupported\n", rate);
		ret = -EINVAL;
		goto error_endpoint;
	}

	/* Subdev, entity and pad */

	subdev = &sensor->subdev;
	v4l2_i2c_subdev_init(subdev, client, &ov4689_subdev_ops);

	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	subdev->entity.function = MEDIA_ENT_F_CAM_SENSOR;

	pad = &sensor->pad;
	pad->flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&subdev->entity, 1, pad);
	if (ret)
		goto error_entity;

	/* Mutex */

	mutex_init(&sensor->mutex);

	/* Sensor */

	ret = ov4689_ctrls_init(sensor);
	if (ret)
		goto error_mutex;

	ret = ov4689_state_init(sensor);
	if (ret)
		goto error_ctrls;

	/* V4L2 subdev register */

	ret = v4l2_async_register_subdev_sensor_common(subdev);
	if (ret)
		goto error_ctrls;

	/* Runtime PM */

	pm_runtime_enable(sensor->dev);
	pm_runtime_set_suspended(sensor->dev);

	printk(KERN_ERR "%s: all init okay\n", __func__);

	ov4689_sensor_power(sensor, true);

	printk(KERN_ERR "%s: done power on\n", __func__);

	return 0;

error_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrls.handler);

error_mutex:
	mutex_destroy(&sensor->mutex);

error_entity:
	media_entity_cleanup(&sensor->subdev.entity);

error_endpoint:
	v4l2_fwnode_endpoint_free(&sensor->endpoint);

	return ret;
}

static int ov4689_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct ov4689_sensor *sensor = ov4689_subdev_sensor(subdev);

	v4l2_async_unregister_subdev(subdev);
	mutex_destroy(&sensor->mutex);
	media_entity_cleanup(&subdev->entity);
	v4l2_device_unregister_subdev(subdev);
	pm_runtime_disable(sensor->dev);

	ov4689_sensor_power(sensor, false);

	return 0;
}

static const struct dev_pm_ops ov4689_pm_ops = {
	SET_RUNTIME_PM_OPS(ov4689_suspend, ov4689_resume, NULL)
};

static const struct of_device_id ov4689_of_match[] = {
	{ .compatible = "ovti,ov4689" },
	{ }
};
MODULE_DEVICE_TABLE(of, ov4689_of_match);

static struct i2c_driver ov4689_driver = {
	.driver = {
		.name = "ov4689",
		.of_match_table = ov4689_of_match,
		.pm = &ov4689_pm_ops,
	},
	.probe_new = ov4689_probe,
	.remove	 = ov4689_remove,
};

module_i2c_driver(ov4689_driver);

MODULE_AUTHOR("Paul Kocialkowski <paul.kocialkowski@bootlin.com>");
MODULE_DESCRIPTION("V4L2 driver for the OmniVision OV4689 image sensor");
MODULE_LICENSE("GPL v2");
