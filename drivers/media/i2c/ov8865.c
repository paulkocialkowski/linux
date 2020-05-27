// SPDX-License-Identifier: GPL-2.0
/*
 * OV8865 MIPI Camera Subdev Driver 
 * Copyright Kévin L'hôpital (C) 2020
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define OV8865_XCLK_MIN			6000000
#define OV8865_XCLK_MAX			27000000

#define PLL1_MULTIPLIER			0x1e
#define PLL1_MDIVIDER			0x00
#define PLL1_MIPI_DIVIDER		0x03
#define PLL2_SYS_DIVIDER		0x00
#define SCLK_DIVIDER			0x01

#define OV8865_DEFAULT_SLAVE_ID		0x36

#define OV8865_REG_PLL_CTRL2            0x0302
#define OV8865_REG_PLL_CTRL3		0x0303
#define OV8865_REG_PLL_CTRL4		0x0304
#define	OV8865_REG_PLL_CTRLE		0x030e
#define	OV8865_REG_PLL_CTRLF		0x030f
#define	OV8865_REG_PLL_CTRL1E		0x031e
#define OV8865_REG_SLAVE_ID             0x3004
#define OV8865_REG_MIPI_CTRL		0x3018
#define	OV8865_REG_CLOCK_SEL		0x3020
#define OV8865_REG_CHIP_ID		0x300a
#define OV8865_REG_SRB_HOST_INPUT	0x3106
#define OV8865_REG_AEC_PK_MANUAL	0x3503
#define OV8865_REG_X_OUTPUT_SIZE	0x3808
#define OV8865_REG_Y_OUTPUT_SIZE	0x380a
#define OV8865_REG_HTS			0x380c
#define OV8865_REG_VTS			0x380e
#define OV8865_REG_AVG_READOUT		0x568a

enum ov8865_mode_id {
	OV8865_MODE_QUXGA_3264_2448 = 0,
	OV8865_MODE_6M_3264_1836,
	OV8865_MODE_1080P_1920_1080,
	OV8865_MODE_720P_1280_720,
	OV8865_MODE_UXGA_1600_1200,
	OV8865_MODE_SVGA_800_600,
	OV8865_MODE_VGA_640_480,
	OV8865_NUM_MODES,
};


enum ov8865_frame_rate {
	OV8865_30_FPS = 0,
	OV8865_90_FPS,
	OV8865_NUM_FRAMERATES,
};

static const int ov8865_framerates[] = {
	[OV8865_30_FPS] = 30,
	[OV8865_90_FPS] = 90,
};

struct ov8865_pixfmt {
	u32 code;
	u32 colorspace;
};

static const struct ov8865_pixfmt ov8865_formats[] = {
	{ MEDIA_BUS_FMT_SRGGB10_1X10, V4L2_COLORSPACE_RAW,},
};

/* regulator supplies */
static const char * const ov8865_supply_name[] = {
	"AVDD",  /* Analog (2.8V) supply */
	"DOVDD", /* Digital I/O (1,8V/2.8V) supply */
	"VDD2",  /* Digital Core (1.2V) supply */
	"AFVDD",
};

#define OV8865_NUM_SUPPLIES ARRAY_SIZE(ov8865_supply_name)

/*
 * Image size under 1280 * 960 are SUBSAMPLING
 * Image size upper 1280 * 960 are SCALING
 */
/*enum ov8865_downsize_mode {
	SUBSAMPLING,
	SCALING,
};*/

struct reg_value {
	u16 reg_addr;
	u8 val;
	u32 delay_ms;
};

struct ov8865_mode_info {
	enum ov8865_mode_id id;
	//enum ov8865_downsize_mode dn_mode;
	u32 hact;
	u32 htot;
	u32 vact;
	u32 vtot;
	const struct reg_value *reg_data;
	u32 reg_data_size;
};

struct ov8865_ctrls {
	struct v4l2_ctrl_handler handler;
	struct v4l2_ctrl *pixel_rate;
	struct {
		//struct v4l2_ctrl *auto_exp;
		struct v4l2_ctrl *exposure;
	};
	struct {
		struct v4l2_ctrl *auto_wb;
		struct v4l2_ctrl *blue_balance;
		struct v4l2_ctrl *red_balance;
	};
	struct {
		struct v4l2_ctrl *auto_gain;
		struct v4l2_ctrl *gain;
	};
	struct v4l2_ctrl *brightness;
	struct v4l2_ctrl *light_freq;
	struct v4l2_ctrl *saturation;
	struct v4l2_ctrl *contrast;
	struct v4l2_ctrl *hue;
	struct v4l2_ctrl *test_pattern;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
};

struct ov8865_dev {
	struct i2c_client *i2c_client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_fwnode_endpoint ep;
	struct clk *xclk;
	u32 xclk_freq;

	struct regulator_bulk_data supplies[OV8865_NUM_SUPPLIES];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	bool upside_down;

	struct mutex lock;

	int power_count;

	struct v4l2_mbus_framefmt fmt;
	bool pending_fmt_change;

	const struct ov8865_mode_info *current_mode;
	const struct ov8865_mode_info *last_mode;
	enum ov8865_frame_rate current_fr;
	struct v4l2_fract frame_interval;
	struct ov8865_ctrls ctrls;

	bool pending_mode_change;
	bool streaming;
};

static inline struct ov8865_dev *to_ov8865_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov8865_dev, sd);
}

static const struct reg_value ov8865_init_setting_QUXGA[] = {
	{0x0103, 0x01, 16}, {0x0100, 0x00, 0}, {0x0100, 0x00, 0},
	{0x0100, 0x00, 0}, {0x0100, 0x00, 0}, {0x3638, 0xff, 0},
	{0x3638, 0xff, 0}, {0x3015, 0x01, 0}, {0x3022, 0x01, 0},
	{0x3031, 0x0a, 0}, {0x3305, 0xf1, 0}, {0x3308, 0x00, 0},
	{0x3309, 0x28, 0}, {0x330a, 0x00, 0}, {0x330b, 0x20, 0},
	{0x330c, 0x00, 0}, {0x330d, 0x00, 0}, {0x330e, 0x00, 0},
	{0x330f, 0x40, 0}, {0x3307, 0x04, 0}, {0x3604, 0x04, 0},
	{0x3602, 0x30, 0}, {0x3605, 0x00, 0}, {0x3607, 0x20, 0},
	{0x3608, 0x11, 0}, {0x3609, 0x68, 0}, {0x360a, 0x40, 0},
	{0x360c, 0xdd, 0}, {0x360e, 0x0c, 0}, {0x3610, 0x07, 0},
	{0x3612, 0x86, 0}, {0x3613, 0x58, 0}, {0x3614, 0x28, 0},
	{0x3617, 0x40, 0}, {0x3618, 0x5a, 0}, {0x3619, 0x9b, 0},
	{0x361c, 0x00, 0}, {0x361d, 0x60, 0}, {0x3631, 0x60, 0},
	{0x3633, 0x10, 0}, {0x3634, 0x10, 0}, {0x3635, 0x10, 0},
	{0x3636, 0x10, 0}, {0x3641, 0x55, 0}, {0x3646, 0x86, 0},
	{0x3647, 0x27, 0}, {0x364a, 0x1b, 0}, {0x3500, 0x00, 0},
	{0x3501, 0x4c, 0}, {0x3502, 0x00, 0}, {0x3503, 0x00, 0},
	{0x3508, 0x02, 0}, {0x3509, 0x00, 0}, {0x3700, 0x24, 0},
	{0x3701, 0x0c, 0}, {0x3702, 0x28, 0}, {0x3703, 0x19, 0},
	{0x3704, 0x14, 0}, {0x3705, 0x00, 0}, {0x3706, 0x38, 0},
	{0x3707, 0x04, 0}, {0x3708, 0x24, 0}, {0x3709, 0x40, 0},
	{0x370a, 0x00, 0}, {0x370b, 0xb8, 0}, {0x370c, 0x04, 0},
	{0x3718, 0x12, 0}, {0x3719, 0x31, 0}, {0x3712, 0x42, 0},
	{0x3714, 0x12, 0}, {0x371e, 0x19, 0}, {0x371f, 0x40, 0},
	{0x3720, 0x05, 0}, {0x3721, 0x05, 0}, {0x3724, 0x02, 0},
	{0x3725, 0x02, 0}, {0x3726, 0x06, 0}, {0x3728, 0x05, 0},
	{0x3729, 0x02, 0}, {0x372a, 0x03, 0}, {0x372b, 0x53, 0},
	{0x372c, 0xa3, 0}, {0x372d, 0x53, 0}, {0x372e, 0x06, 0},
	{0x372f, 0x10, 0}, {0x3730, 0x01, 0}, {0x3731, 0x06, 0},
	{0x3732, 0x14, 0}, {0x3733, 0x10, 0}, {0x3734, 0x40, 0},
	{0x3736, 0x20, 0}, {0x373a, 0x02, 0}, {0x373b, 0x0c, 0},
	{0x373c, 0x0a, 0}, {0x373e, 0x03, 0}, {0x3755, 0x40, 0},
	{0x3758, 0x00, 0}, {0x3759, 0x4c, 0}, {0x375a, 0x06, 0},
	{0x375b, 0x13, 0}, {0x375c, 0x40, 0}, {0x375d, 0x02, 0},
	{0x375e, 0x00, 0}, {0x375f, 0x14, 0}, {0x3767, 0x1c, 0},
	{0x3768, 0x04, 0}, {0x3769, 0x20, 0}, {0x376c, 0xc0, 0},
	{0x376d, 0xc0, 0}, {0x376a, 0x08, 0}, {0x3761, 0x00, 0},
	{0x3762, 0x00, 0}, {0x3763, 0x00, 0}, {0x3766, 0xff, 0},
	{0x376b, 0x42, 0}, {0x3772, 0x23, 0}, {0x3773, 0x02, 0},
	{0x3774, 0x16, 0}, {0x3775, 0x12, 0}, {0x3776, 0x08, 0},
	{0x37a0, 0x44, 0}, {0x37a1, 0x3d, 0}, {0x37a2, 0x3d, 0},
	{0x37a3, 0x01, 0}, {0x37a4, 0x00, 0}, {0x37a5, 0x08, 0},
	{0x37a6, 0x00, 0}, {0x37a7, 0x44, 0}, {0x37a8, 0x58, 0},
	{0x37a9, 0x58, 0}, {0x3760, 0x00, 0}, {0x376f, 0x01, 0},
	{0x37aa, 0x44, 0}, {0x37ab, 0x2e, 0}, {0x37ac, 0x2e, 0},
	{0x37ad, 0x33, 0}, {0x37ae, 0x0d, 0}, {0x37af, 0x0d, 0},
	{0x37b0, 0x00, 0}, {0x37b1, 0x00, 0}, {0x37b2, 0x00, 0},
	{0x37b3, 0x42, 0}, {0x37b4, 0x42, 0}, {0x37b5, 0x33, 0},
	{0x37b6, 0x00, 0}, {0x37b7, 0x00, 0}, {0x37b8, 0x00, 0},
	{0x37b9, 0xff, 0}, {0x3800, 0x00, 0}, {0x3801, 0x0c, 0},
	{0x3802, 0x00, 0}, {0x3803, 0x0c, 0}, {0x3804, 0x0c, 0},
    	{0x3805, 0xd3, 0}, {0x3806, 0x09, 0}, {0x3807, 0xa3, 0},
    	{0x3810, 0x00, 0}, {0x3811, 0x04, 0}, {0x3813, 0x04, 0},
    	{0x3814, 0x03, 0}, {0x3815, 0x01, 0}, {0x3820, 0x00, 0},
	{0x3821, 0x67, 0}, {0x382a, 0x03, 0}, {0x382b, 0x01, 0},
    	{0x3830, 0x08, 0}, {0x3836, 0x02, 0}, {0x3837, 0x18, 0},
    	{0x3841, 0xff, 0}, {0x3846, 0x88, 0}, {0x3d85, 0x06, 0},
    	{0x3d8c, 0x75, 0}, {0x3d8d, 0xef, 0}, {0x3f08, 0x0b, 0},
    	{0x4000, 0xf1, 0}, {0x4001, 0x14, 0}, {0x4005, 0x10, 0},
    	{0x400b, 0x0c, 0}, {0x400d, 0x10, 0}, {0x401b, 0x00, 0},
    	{0x401d, 0x00, 0}, {0x4020, 0x01, 0}, {0x4021, 0x20, 0},
	{0x4022, 0x01, 0}, {0x4023, 0x9f, 0}, {0x4024, 0x03, 0},
	{0x4025, 0xe0, 0}, {0x4026, 0x04, 0}, {0x4027, 0x5f, 0},
	{0x4028, 0x00, 0}, {0x4029, 0x02, 0}, {0x402a, 0x04, 0},
	{0x402b, 0x04, 0}, {0x402c, 0x02, 0}, {0x402d, 0x02, 0},
    	{0x402e, 0x08, 0}, {0x402f, 0x02, 0}, {0x401f, 0x00, 0},
    	{0x4034, 0x3f, 0}, {0x4300, 0xff, 0}, {0x4301, 0x00, 0},
	{0x4302, 0x0f, 0}, {0x4500, 0x40, 0}, {0x4503, 0x10, 0},
    	{0x4601, 0x74, 0}, {0x481f, 0x32, 0}, {0x4837, 0x16, 0},
    	{0x4850, 0x10, 0}, {0x4851, 0x32, 0}, {0x4b00, 0x2a, 0},
    	{0x4b0d, 0x00, 0}, {0x4d00, 0x04, 0}, {0x4d01, 0x18, 0},
    	{0x4d02, 0xc3, 0}, {0x4d03, 0xff, 0}, {0x4d04, 0xff, 0},
    	{0x4d05, 0xff, 0}, {0x5000, 0x96, 0}, {0x5001, 0x01, 0},
    	{0x5002, 0x08, 0}, {0x5901, 0x00, 0}, {0x5e00, 0x00, 0},
    	{0x5e01, 0x41, 0}, {0x0100, 0x01, 0}, {0x5b00, 0x02, 0},
    	{0x5b01, 0xd0, 0}, {0x5b02, 0x03, 0}, {0x5b03, 0xff, 0},
    	{0x5b05, 0x6c, 0}, {0x5780, 0xfc, 0}, {0x5781, 0xdf, 0},
    	{0x5782, 0x3f, 0}, {0x5783, 0x08, 0}, {0x5784, 0x0c, 0},
    	{0x5786, 0x20, 0}, {0x5787, 0x40, 0}, {0x5788, 0x08, 0},
    	{0x5789, 0x08, 0}, {0x578a, 0x02, 0}, {0x578b, 0x01, 0},
    	{0x578c, 0x01, 0}, {0x578d, 0x0c, 0}, {0x578e, 0x02, 0},
    	{0x578f, 0x01, 0}, {0x5790, 0x01, 0}, {0x5800, 0x1d, 0},
    	{0x5801, 0x0e, 0}, {0x5802, 0x0c, 0}, {0x5803, 0x0c, 0},
    	{0x5804, 0x0f, 0}, {0x5805, 0x22, 0}, {0x5806, 0x0a, 0},
    	{0x5807, 0x06, 0}, {0x5808, 0x05, 0}, {0x5809, 0x05, 0},
    	{0x580a, 0x07, 0}, {0x580b, 0x0a, 0}, {0x580c, 0x06, 0},
    	{0x580d, 0x02, 0}, {0x580e, 0x00, 0}, {0x580f, 0x00, 0},
    	{0x5810, 0x03, 0}, {0x5811, 0x07, 0}, {0x5812, 0x06, 0},
    	{0x5813, 0x02, 0}, {0x5814, 0x00, 0}, {0x5815, 0x00, 0},
    	{0x5816, 0x03, 0}, {0x5817, 0x07, 0}, {0x5818, 0x09, 0},
    	{0x5819, 0x06, 0}, {0x581a, 0x04, 0}, {0x581b, 0x04, 0},
    	{0x581c, 0x06, 0}, {0x581d, 0x0a, 0}, {0x581e, 0x19, 0},
    	{0x581f, 0x0d, 0}, {0x5820, 0x0b, 0}, {0x5821, 0x0b, 0},
    	{0x5822, 0x0e, 0}, {0x5823, 0x22, 0}, {0x5824, 0x23, 0},
    	{0x5825, 0x28, 0}, {0x5826, 0x29, 0}, {0x5827, 0x27, 0},
    	{0x5828, 0x13, 0}, {0x5829, 0x26, 0}, {0x582a, 0x33, 0},
    	{0x582b, 0x32, 0}, {0x582c, 0x33, 0}, {0x582d, 0x16, 0},
    	{0x582e, 0x14, 0}, {0x582f, 0x30, 0}, {0x5830, 0x31, 0},
    	{0x5831, 0x30, 0}, {0x5832, 0x15, 0}, {0x5833, 0x26, 0},
    	{0x5834, 0x23, 0}, {0x5835, 0x21, 0}, {0x5836, 0x23, 0},
    	{0x5837, 0x05, 0}, {0x5838, 0x36, 0}, {0x5839, 0x27, 0},
    	{0x583a, 0x28, 0}, {0x583b, 0x26, 0}, {0x583c, 0x24, 0},
    	{0x583d, 0xdf, 0}, {0x0100, 0x01, 0},
};

static const struct reg_value ov8865_setting_QUXGA[] = {
	{0x0100, 0x00, 5}, {0x3501, 0x98, 0}, {0x3502, 0x60, 0},
    	{0x3700, 0x48, 0}, {0x3701, 0x18, 0}, {0x3702, 0x50, 0},
    	{0x3703, 0x32, 0}, {0x3704, 0x28, 0}, {0x3706, 0x70, 0},
    	{0x3707, 0x08, 0}, {0x3708, 0x48, 0}, {0x3709, 0x80, 0},
    	{0x370a, 0x01, 0}, {0x370b, 0x70, 0}, {0x370c, 0x07, 0},
    	{0x3718, 0x14, 0}, {0x3712, 0x44, 0}, {0x371e, 0x31, 0},
    	{0x371f, 0x7f, 0}, {0x3720, 0x0a, 0}, {0x3721, 0x0a, 0},
    	{0x3724, 0x04, 0}, {0x3725, 0x04, 0}, {0x3726, 0x0c, 0},
    	{0x3728, 0x0a, 0}, {0x3729, 0x03, 0}, {0x372a, 0x06, 0},
    	{0x372b, 0xa6, 0}, {0x372c, 0xa6, 0}, {0x372d, 0xa6, 0},
    	{0x372e, 0x0c, 0}, {0x372f, 0x20, 0}, {0x3730, 0x02, 0},
    	{0x3731, 0x0c, 0}, {0x3732, 0x28, 0}, {0x3736, 0x30, 0},
    	{0x373a, 0x04, 0}, {0x373b, 0x18, 0}, {0x373c, 0x14, 0},
   	{0x373e, 0x06, 0}, {0x375a, 0x0c, 0}, {0x375b, 0x26, 0},
    	{0x375d, 0x04, 0}, {0x375f, 0x28, 0}, {0x3767, 0x1e, 0},
    	{0x3772, 0x46, 0}, {0x3773, 0x04, 0}, {0x3774, 0x2c, 0},
    	{0x3775, 0x13, 0}, {0x3776, 0x10, 0}, {0x37a0, 0x88, 0},
    	{0x37a1, 0x7a, 0}, {0x37a2, 0x7a, 0}, {0x37a3, 0x02, 0},
    	{0x37a5, 0x09, 0}, {0x37a7, 0x88, 0}, {0x37a8, 0xb0, 0},
    	{0x37a9, 0xb0, 0}, {0x37aa, 0x88, 0}, {0x37ab, 0x5c, 0},
    	{0x37ac, 0x5c, 0}, {0x37ad, 0x55, 0}, {0x37ae, 0x19, 0},
    	{0x37af, 0x19, 0}, {0x37b3, 0x84, 0}, {0x37b4, 0x84, 0},
    	{0x37b5, 0x66, 0}, {0x3813, 0x02, 0}, {0x3814, 0x01, 0},
    	{0x3821, 0x46, 0}, {0x382a, 0x01, 0}, {0x382b, 0x01, 0},
    	{0x3830, 0x04, 0}, {0x3836, 0x01, 0}, {0x3846, 0x48, 0},
    	{0x3f08, 0x16, 0}, {0x4000, 0xf1, 0}, {0x4001, 0x04, 0},
    	{0x4020, 0x02, 0}, {0x4021, 0x40, 0}, {0x4022, 0x03, 0},
    	{0x4023, 0x3f, 0}, {0x4024, 0x07, 0}, {0x4025, 0xc0, 0},
    	{0x4026, 0x08, 0}, {0x4027, 0xbf, 0}, {0x402a, 0x04, 0},
    	{0x402b, 0x04, 0}, {0x402c, 0x02, 0}, {0x402d, 0x02, 0},
    	{0x402e, 0x08, 0}, {0x4500, 0x68, 0}, {0x4601, 0x10, 0},
    	{0x5002, 0x08, 0}, {0x5901, 0x00, 0}, {0x0100, 0x01, 0},
};

static const struct reg_value ov8865_setting_6M[] = {
	{0x0100, 0x00, 5}, {0x3501, 0x72, 0}, {0x3502, 0x20, 0},
    	{0x3700, 0x48, 0}, {0x3701, 0x18, 0}, {0x3702, 0x50, 0},
    	{0x3703, 0x32, 0}, {0x3704, 0x28, 0}, {0x3706, 0x70, 0},
    	{0x3707, 0x08, 0}, {0x3708, 0x48, 0}, {0x3709, 0x80, 0},
    	{0x370a, 0x01, 0}, {0x370b, 0x70, 0}, {0x370c, 0x07, 0},
    	{0x3718, 0x14, 0}, {0x3712, 0x44, 0}, {0x371e, 0x31, 0},
    	{0x371f, 0x7f, 0}, {0x3720, 0x0a, 0}, {0x3721, 0x0a, 0},
   	{0x3724, 0x04, 0}, {0x3725, 0x04, 0}, {0x3726, 0x0c, 0},
    	{0x3728, 0x0a, 0}, {0x3729, 0x03, 0}, {0x372a, 0x06, 0},
    	{0x372b, 0xa6, 0}, {0x372c, 0xa6, 0}, {0x372d, 0xa6, 0},
    	{0x372e, 0x0c, 0}, {0x372f, 0x20, 0}, {0x3730, 0x02, 0},
    	{0x3731, 0x0c, 0}, {0x3732, 0x28, 0}, {0x3736, 0x30, 0},
    	{0x373a, 0x04, 0}, {0x373b, 0x18, 0}, {0x373c, 0x14, 0},
    	{0x373e, 0x06, 0}, {0x375a, 0x0c, 0}, {0x375b, 0x26, 0},
    	{0x375d, 0x04, 0}, {0x375f, 0x28, 0}, {0x3767, 0x1e, 0},
    	{0x3772, 0x46, 0}, {0x3773, 0x04, 0}, {0x3774, 0x2c, 0},
   	{0x3775, 0x13, 0}, {0x3776, 0x10, 0}, {0x37a0, 0x88, 0},
    	{0x37a1, 0x7a, 0}, {0x37a2, 0x7a, 0}, {0x37a3, 0x02, 0},
    	{0x37a5, 0x09, 0}, {0x37a7, 0x88, 0}, {0x37a8, 0xb0, 0},
	{0x37a9, 0xb0, 0}, {0x37aa, 0x88, 0}, {0x37ab, 0x5c, 0},
    	{0x37ac, 0x5c, 0}, {0x37ad, 0x55, 0}, {0x37ae, 0x19, 0},
    	{0x37af, 0x19, 0}, {0x37b3, 0x84, 0}, {0x37b4, 0x84, 0},
    	{0x37b5, 0x66, 0}, {0x3813, 0x02, 0}, {0x3814, 0x01, 0},
    	{0x3821, 0x46, 0}, {0x382a, 0x01, 0}, {0x382b, 0x01, 0},
    	{0x3830, 0x04, 0}, {0x3836, 0x01, 0}, {0x3846, 0x48, 0},
    	{0x3f08, 0x16, 0}, {0x4000, 0xf1, 0}, {0x4001, 0x04, 0},
  	{0x4020, 0x02, 0}, {0x4021, 0x40, 0}, {0x4022, 0x03, 0},
    	{0x4023, 0x3f, 0}, {0x4024, 0x07, 0}, {0x4025, 0xc0, 0},
    	{0x4026, 0x08, 0}, {0x4027, 0xbf, 0}, {0x402a, 0x04, 0},
    	{0x402b, 0x04, 0}, {0x402c, 0x02, 0}, {0x402d, 0x02, 0},
    	{0x402e, 0x08, 0}, {0x4500, 0x68, 0}, {0x4601, 0x10, 0},
    	{0x5002, 0x08, 0}, {0x5901, 0x00, 0}, {0x0100, 0x01, 0},
};


static const struct reg_value ov8865_setting_UXGA[] = {
	{0x0100, 0x00, 5}, {0x3501, 0x26, 0}, {0x3502, 0x00, 0},
	{0x3700, 0x24, 0}, {0x3701, 0x0c, 0}, {0x3702, 0x28, 0},
    	{0x3703, 0x19, 0}, {0x3704, 0x14, 0}, {0x3706, 0x38, 0},
    	{0x3707, 0x04, 0}, {0x3708, 0x24, 0}, {0x3709, 0x40, 0},
    	{0x370a, 0x00, 0}, {0x370b, 0xb8, 0}, {0x370c, 0x04, 0},
    	{0x3718, 0x12, 0}, {0x3712, 0x42, 0}, {0x371e, 0x19, 0},
    	{0x371f, 0x40, 0}, {0x3720, 0x05, 0}, {0x3721, 0x05, 0},
    	{0x3724, 0x02, 0}, {0x3725, 0x02, 0}, {0x3726, 0x06, 0},
    	{0x3728, 0x05, 0}, {0x3729, 0x02, 0}, {0x372a, 0x03, 0},
    	{0x372b, 0x53, 0}, {0x372c, 0xa3, 0}, {0x372d, 0x53, 0},
    	{0x372e, 0x06, 0}, {0x372f, 0x10, 0}, {0x3730, 0x01, 0},
    	{0x3731, 0x06, 0}, {0x3732, 0x14, 0}, {0x3736, 0x20, 0},
    	{0x373a, 0x02, 0}, {0x373b, 0x0c, 0}, {0x373c, 0x0a, 0},
    	{0x373e, 0x03, 0}, {0x375a, 0x06, 0}, {0x375b, 0x13, 0},
    	{0x375d, 0x02, 0}, {0x375f, 0x14, 0}, {0x3767, 0x18, 0},
    	{0x3772, 0x23, 0}, {0x3773, 0x02, 0}, {0x3774, 0x16, 0},
    	{0x3775, 0x12, 0}, {0x3776, 0x08, 0}, {0x37a0, 0x44, 0},
    	{0x37a1, 0x3d, 0}, {0x37a2, 0x3d, 0}, {0x37a3, 0x01, 0},
    	{0x37a5, 0x08, 0}, {0x37a7, 0x44, 0}, {0x37a8, 0x58, 0},
    	{0x37a9, 0x58, 0}, {0x37aa, 0x44, 0}, {0x37ab, 0x2e, 0},
    	{0x37ac, 0x2e, 0}, {0x37ad, 0x33, 0}, {0x37ae, 0x0d, 0},
    	{0x37af, 0x0d, 0}, {0x37b3, 0x42, 0}, {0x37b4, 0x42, 0},
    	{0x37b5, 0x33, 0}, {0x3813, 0x04, 0}, {0x3814, 0x03, 0},
    	{0x3821, 0x6f, 0}, {0x382a, 0x05, 0}, {0x382b, 0x03, 0},
    	{0x3830, 0x08, 0}, {0x3836, 0x02, 0}, {0x3846, 0x88, 0},
    	{0x3f08, 0x0b, 0}, {0x4000, 0xf1, 0}, {0x4001, 0x14, 0},
    	{0x4020, 0x01, 0}, {0x4021, 0x20, 0}, {0x4022, 0x01, 0},
   	{0x4023, 0x9f, 0}, {0x4024, 0x03, 0}, {0x4025, 0xe0, 0},
    	{0x4026, 0x04, 0}, {0x4027, 0x5f, 0}, {0x402a, 0x02, 0},
    	{0x402b, 0x02, 0}, {0x402c, 0x00, 0}, {0x402d, 0x00, 0},
    	{0x402e, 0x04, 0}, {0x4500, 0x40, 0}, {0x4601, 0x50, 0},
   	{0x5002, 0x0c, 0}, {0x5901, 0x04, 0}, {0x0100, 0x01, 0},

};

static const struct reg_value ov8865_setting_SVGA[] = {
	{0x0100, 0x00, 5}, {0x3501, 0x26, 0}, {0x3502, 0x00, 0},
    	{0x3700, 0x24, 0}, {0x3701, 0x0c, 0}, {0x3702, 0x28, 0},
    	{0x3703, 0x19, 0}, {0x3704, 0x14, 0}, {0x3706, 0x38, 0},
    	{0x3707, 0x04, 0}, {0x3708, 0x24, 0}, {0x3709, 0x40, 0},
    	{0x370a, 0x00, 0}, {0x370b, 0xb8, 0}, {0x370c, 0x04, 0},
    	{0x3718, 0x12, 0}, {0x3712, 0x42, 0}, {0x371e, 0x19, 0},
    	{0x371f, 0x40, 0}, {0x3720, 0x05, 0}, {0x3721, 0x05, 0},
    	{0x3724, 0x02, 0}, {0x3725, 0x02, 0}, {0x3726, 0x06, 0},
    	{0x3728, 0x05, 0}, {0x3729, 0x02, 0}, {0x372a, 0x03, 0},
    	{0x372b, 0x53, 0}, {0x372c, 0xa3, 0}, {0x372d, 0x53, 0},
    	{0x372e, 0x06, 0}, {0x372f, 0x10, 0}, {0x3730, 0x01, 0},
    	{0x3731, 0x06, 0}, {0x3732, 0x14, 0}, {0x3736, 0x20, 0},
    	{0x373a, 0x02, 0}, {0x373b, 0x0c, 0}, {0x373c, 0x0a, 0},
    	{0x373e, 0x03, 0}, {0x375a, 0x06, 0}, {0x375b, 0x13, 0},
    	{0x375d, 0x02, 0}, {0x375f, 0x14, 0}, {0x3767, 0x18, 0},
    	{0x3772, 0x23, 0}, {0x3773, 0x02, 0}, {0x3774, 0x16, 0},
	{0x3775, 0x12, 0}, {0x3776, 0x08, 0}, {0x37a0, 0x44, 0},
    	{0x37a1, 0x3d, 0}, {0x37a2, 0x3d, 0}, {0x37a3, 0x01, 0},
    	{0x37a5, 0x08, 0}, {0x37a7, 0x44, 0}, {0x37a8, 0x58, 0},
	{0x37a9, 0x58, 0}, {0x37aa, 0x44, 0}, {0x37ab, 0x2e, 0},
    	{0x37ac, 0x2e, 0}, {0x37ad, 0x33, 0}, {0x37ae, 0x0d, 0},
    	{0x37af, 0x0d, 0}, {0x37b3, 0x42, 0}, {0x37b4, 0x42, 0},
    	{0x37b5, 0x33, 0}, {0x3813, 0x04, 0}, {0x3814, 0x03, 0},
    	{0x3821, 0x6f, 0}, {0x382a, 0x05, 0}, {0x382b, 0x03, 0},
    	{0x3830, 0x08, 0}, {0x3836, 0x02, 0}, {0x3846, 0x88, 0},
    	{0x3f08, 0x0b, 0}, {0x4000, 0xf1, 0}, {0x4001, 0x14, 0},
    	{0x4020, 0x01, 0}, {0x4021, 0x20, 0}, {0x4022, 0x01, 0},
    	{0x4023, 0x9f, 0}, {0x4024, 0x03, 0}, {0x4025, 0xe0, 0},
    	{0x4026, 0x04, 0}, {0x4027, 0x5f, 0}, {0x402a, 0x02, 0},
    	{0x402b, 0x02, 0}, {0x402c, 0x00, 0}, {0x402d, 0x00, 0},
    	{0x402e, 0x04, 0}, {0x4500, 0x40, 0}, {0x4601, 0x50, 0},
   	{0x5002, 0x0c, 0}, {0x5901, 0x04, 0}, {0x0100, 0x01, 0},

};

static const struct ov8865_mode_info ov8865_mode_init_data = {
	0, 3264, 1944, 2448, 2470, ov8865_init_setting_QUXGA,
	ARRAY_SIZE(ov8865_init_setting_QUXGA),
};

static const struct ov8865_mode_info ov8865_mode_data[OV8865_NUM_MODES] = {
	{OV8865_MODE_QUXGA_3264_2448, 3264, 1944, 2448, 2470,
		ov8865_setting_QUXGA,
		ARRAY_SIZE(ov8865_setting_QUXGA)},
	{OV8865_MODE_6M_3264_1836, 3264, 2582, 1836, 1858,
		ov8865_setting_6M,
		ARRAY_SIZE(ov8865_setting_6M)},
	{OV8865_MODE_1080P_1920_1080, 1920, 2582, 1080, 1858,
		ov8865_setting_6M,
		ARRAY_SIZE(ov8865_setting_6M)},
	{OV8865_MODE_720P_1280_720, 1280, 1923, 720, 1248,
		ov8865_setting_UXGA,
		ARRAY_SIZE(ov8865_setting_UXGA)},
	{OV8865_MODE_UXGA_1600_1200, 1600, 1923, 1200, 1248,
		ov8865_setting_UXGA,
		ARRAY_SIZE(ov8865_setting_UXGA)},
	{OV8865_MODE_SVGA_800_600, 800, 1250, 600, 640,
		ov8865_setting_SVGA,
		ARRAY_SIZE(ov8865_setting_SVGA)},
	{OV8865_MODE_VGA_640_480, 640, 2582, 480, 1858,
		ov8865_setting_6M,
		ARRAY_SIZE(ov8865_setting_6M)},
};


/* ASK!!*/
static int ov8865_init_slave_id(struct ov8865_dev *sensor)
{
	struct i2c_client *client = sensor->i2c_client;
	struct i2c_msg msg;
	u8 buf[3];
	int ret;

	if (client->addr == OV8865_DEFAULT_SLAVE_ID)
		return 0;

	buf[0] = OV8865_REG_SLAVE_ID >> 8;
	buf[1] = OV8865_REG_SLAVE_ID & 0xff;
	buf[2] = client->addr << 1;

	msg.addr = OV8865_DEFAULT_SLAVE_ID;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		dev_err(&client->dev, "%s: failed with %d\n", __func__, ret);
		return ret;
	}

	return 0;
}

static int ov8865_write_reg(struct ov8865_dev *sensor, u16 reg, u8 val)
{
	struct i2c_client *client = sensor->i2c_client;
	struct i2c_msg msg;
	u8 buf[3];
	int ret;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;
	buf[2] = val;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		dev_err(&client->dev, "%s: error: reg=%x, val=%x\n",
			__func__, reg, val);
		return ret;
	}

	return 0;
}

static int ov8865_write_reg16(struct ov8865_dev *sensor, u16 reg, u16 val)
{
	int ret;

	ret = ov8865_write_reg(sensor, reg, val >> 8);
	if (ret)
		return ret;

	return ov8865_write_reg(sensor, reg +1, val & 0xff);
}

static int ov8865_read_reg(struct ov8865_dev *sensor, u16 reg, u8 *val)
{
	struct i2c_client *client = sensor->i2c_client;
	struct i2c_msg msg[2];
	u8 buf[2];
	int ret = 0;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = client->addr;
	msg[1].flags =  I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s: error: reg=%x\n",__func__, reg);
		return ret;
	}

	*val = buf[0];
	return 0;
}

static int ov8865_mod_reg(struct ov8865_dev *sensor, u16 reg, u8 mask, u8 val)
{
	u8 readval;
	int ret;

	ret = ov8865_read_reg(sensor, reg, &readval);
	if (ret)
		return ret;

	readval &= ~mask;
	val &= mask;
	val |= readval;

	return ov8865_write_reg(sensor, reg, val);
}

static int ov8865_set_timings(struct ov8865_dev *sensor,
			      const struct ov8865_mode_info *mode)
{
	int ret;

	ret = ov8865_write_reg16(sensor, OV8865_REG_X_OUTPUT_SIZE, mode->hact);
	if (ret < 0)
		return ret;

	ret = ov8865_write_reg16(sensor, OV8865_REG_Y_OUTPUT_SIZE, mode->vact);
	if (ret < 0)
		return ret;

	ret = ov8865_write_reg16(sensor, OV8865_REG_HTS, mode->htot);
	if (ret < 0)
		return ret;

	return ov8865_write_reg16(sensor, OV8865_REG_VTS, mode->vtot);
}

static int ov8865_load_regs(struct ov8865_dev *sensor,
			     const struct ov8865_mode_info *mode)
{
	const struct reg_value *regs = mode->reg_data;
	unsigned int i;
	u32 delay_ms;
	u16 reg_addr;
	u8 val;
	int ret = 0;

	for (i=0; i < mode->reg_data_size; i++, regs++) {//++i,++regs
		delay_ms = regs->delay_ms;
		reg_addr = regs->reg_addr;
		val = regs->val;

		ret = ov8865_write_reg(sensor, reg_addr, val);
		if (ret)
			break;

		if (delay_ms)
			usleep_range(1000 * delay_ms, 1000 * delay_ms + 100);
	}

	return ov8865_set_timings(sensor, mode);
}

static const struct ov8865_mode_info *
ov8865_find_mode(struct ov8865_dev *sensor, enum ov8865_frame_rate fr, 
		 int width, int height, bool nearest)
{
	const struct ov8865_mode_info *mode;

	mode = v4l2_find_nearest_size(ov8865_mode_data,
				      ARRAY_SIZE(ov8865_mode_data),
				      hact, vact, width, height);

	if (!mode || (!nearest && (mode->hact != width || mode->vact !=
				   height)))
		return NULL;

	/* ONLY SVGA can operate 90fps (for now) */
	if (fr == OV8865_90_FPS &&
	    !(mode->hact == 800 && mode->vact == 600))
	    return NULL;

	return mode;
}

/*static ov8865_calc_pixel_rate(struct ov8865_dev *sensor)
{
	u64 rate;

	rate = sensor->current_mode->vtot * sensor->current_mode_>htot;
	rate *= ov8865_framerates[sensor->current_fr];

	return rate;
}
*/
/*
static int ov8865_set_mode_exposure_calc(struct ov8865_dev * sensor,
					 const struct ov8865_mode_info *mode)
{
	u32 prev_shutter, prev_gain16;
	u32 cap_shutter, cap_gain16;
	u32 cap_sysclk, cap_hts, cap_vts;
	u32 light_freq, cap_bandfilt, cap_maxband;
	u32 cap_gain16_shutter;
	u8 average;

	int ret;

	if (!mode->reg_data)
		return -EINVAL;

*/	/* read preview shutter */
/*	ret = ov8865_get_exposure(sensor);
	if (ret < 0)
		return ret;
	prev_shutter = ret;
	ret = ov8865_get_binning(sensor);
	if (ret < 0)
		return ret;

*/	/* read preview gain */
/*	ret = ov8865_get_gain(sensor);
	if (ret < 0)
		return ret;
	prev_gain16 = ret;

*/	/* get average */
/*	ret = ov8865_read_reg(sensor, OV8865_REG_AVG_READOUT, &average);
	if (ret)
		return ret;

*/	/* turn off night mode for capture */
/*	ret = ov8865_set_night_mode(sensor);
	if (ret < 0)
		return ret;

*/	/* Write capture setting */
/*	ret = ov8865_load_regs(sensor, mode);
	if (ret < 0)
		return ret;

*/	/* read capture VTS */
/*	ret = ov8865_get_vts(sensor);
	if (ret < 0)
		return ret;
	cap_vts = ret;
	ret = ov8865_get_hts(sensor);
	if (ret < 0)
		return ret;
	if (ret == 0)
		return -EINVAL;
	cap_hts = ret;

	ret = ov8865_get_sysclk(sensor);
	if (ret < 0)
		return ret;
	if (ret < 0)
		return ret;
	if (ret == 0)
		return -EINVAL;
	cap_sysclk = ret;

*/	/* calculate capture banding filter */
/*	ret = ov8865_get_light_freq(sensor);
	if (ret < 0)
		return ret;
	light_freq = ret;

	if (light_freq == 60) {


*/

static int ov8865_set_mode_direct(struct ov8865_dev *sensor,
			      const struct ov8865_mode_info *mode)
{
	if (!mode->reg_data)
		return -EINVAL;

	/*Write capture setting*/
	return ov8865_load_regs(sensor, mode);
}

static int ov8865_set_pclk(struct ov8865_dev *sensor)
{
	int ret;

	ret = ov8865_write_reg(sensor, OV8865_REG_PLL_CTRL2, PLL1_MULTIPLIER);
	if (ret)
		return ret;

	ret = ov8865_write_reg(sensor, OV8865_REG_PLL_CTRL3, PLL1_MDIVIDER);
	if (ret)
		return ret;

	ret = ov8865_write_reg(sensor, OV8865_REG_PLL_CTRL4, PLL1_MIPI_DIVIDER);
	if (ret)
		return ret;

	ret = ov8865_write_reg(sensor, OV8865_REG_PLL_CTRL1E, 0x0c);
	if (ret)
		return ret;
	return ov8865_write_reg(sensor, OV8865_REG_CLOCK_SEL, 0x93);
}

static int ov8865_set_sclk(struct ov8865_dev *sensor)
{
	const struct ov8865_mode_info * mode = sensor->current_mode;
	int ret;

	if ((mode->id  == OV8865_MODE_UXGA_1600_1200)||
	    (mode->id == OV8865_MODE_720P_1280_720)||
	    (mode->id == OV8865_MODE_SVGA_800_600))
		ret = ov8865_write_reg(sensor, OV8865_REG_PLL_CTRLF, 0x09);
	else
		ret = ov8865_write_reg(sensor, OV8865_REG_PLL_CTRLF, 0x04);

	if (ret)
		return ret;

	ret = ov8865_write_reg(sensor, OV8865_REG_PLL_CTRLE,
			       PLL2_SYS_DIVIDER);
	if (ret)
		return ret;

	return ov8865_write_reg(sensor, OV8865_REG_SRB_HOST_INPUT,
				SCLK_DIVIDER);
}
/*
static int ov8865_set_autoexposure(struct ov8865_dev *sensor, bool on)
{
	return ov8865_mod_reg(sensor, OV8865_REG_AEC_PK_MANUAL,
*/
static int ov8865_set_autogain(struct ov8865_dev *sensor, bool on)
{
	return ov8865_mod_reg(sensor, OV8865_REG_AEC_PK_MANUAL,
			      BIT(2), on ? 0 : BIT(2));
}

static int ov8865_set_mode(struct ov8865_dev *sensor)
{
	const struct ov8865_mode_info *mode = sensor->current_mode;
	const struct ov8865_mode_info *orig_mode = sensor->last_mode;
	//enum ov8865_downsize_mode dn_mode, orig_dn_mode;
	bool auto_gain = sensor->ctrls.auto_gain->val == 1;
	//bool auto_exp =  sensor->ctrls.auto_exp->val == V4L2_EXPOSURE_AUTO;
	unsigned long rate;
	int ret;

	//dn_mode = mode->dn_mode;
	//orig_dn_mode = orig_mode->dn_mode;

	if (auto_gain) {
		ret = ov8865_set_autogain(sensor, false);
		if (ret)
			return ret;
	}

/*	if (auto_exp) {
		ret = ov8865_set_autoexposure(sensor, false);
		if (ret)
			goto restore_auto_gain;
	}*/
/*check*/
	/*
	 * All the formats we support have 10 bits per pixel, seems to require
	 * the same rate than Raw RGB Bayer, so we can just use 10 bpp all the
	 * time.
	 */
	//rate = ov8865_calc_pixel_rate(sensor) * 10;
	//rate = rate / sensor->ep.bus.mipi_csi2.num_data_lanes; /*check*/
	ret = ov8865_set_pclk(sensor);
	ret = ov8865_set_sclk(sensor);
	if (ret < 0)
		return 0; /*ret ou 0 ?*/
/*Understand*/
/*	if ((dn_mode == SUBSAMPLING && orig_dn_mode == SCALING) ||
	    (dn_mode == SCALING && orig_dn_mode == SUBSAMPLING)) {	
		ret = ov8865_set_mode_exposure_calc(sensor, mode);
	} else {*/
		ret = ov8865_set_mode_direct(sensor, mode);
	if (ret < 0)
		goto restore_auto_exp_gain;

	if (auto_gain)
		ov8865_set_autogain(sensor, true);
/*	if (auto_exp)
		ov8865_set_autoexposure(sensor, true);
*/
/*	ret = ov8865_set_binning(sensor, dn_mode != SCALING);
	if (ret < 0)
		return ret;
*//*	ret = ov8865_set_ae_target(sensor, sensor->ae_target);
	if (ret < 0)
		return ret;
*//*	ret = ov8865_get_light_freq(sensor);
	if (ret < 0)
*/		return ret;
/*	ret = ov8865_set_bandingfilter(sensor);
	if (ret < 0)
		return ret;
*//*	ret = ov8865_set_virtual_channel(sensor);
	if (ret < 0)
		return ret;
*/
	sensor->pending_mode_change = false;
	sensor->last_mode = mode;

	return 0;
restore_auto_exp_gain:
/*	if (auto_exp)
		ov8865_set_autoexposure(sensor, true);*/
restore_auto_gain:
	if (auto_gain)
		ov8865_set_autogain(sensor, true);

	return ret;
}

/*static int ov8865_set_framefmt(struct ov8865_dev *sensor,
			       struct v4l2_mbus_framefmt *format)
{
	int ret = 0
	u8 fmt, mux;

	switch (format->code) {

*/
static int ov8865_restore_mode(struct ov8865_dev *sensor)
{
	int ret;

	ret = ov8865_load_regs(sensor, &ov8865_mode_init_data);
	if (ret)
		return ret;
	sensor->last_mode = &ov8865_mode_init_data;

	return ov8865_set_mode(sensor);
	
//	return ov8865_set_framefmt(sensor, &sensor->fmt);
}

static void ov8865_power(struct ov8865_dev *sensor, bool enable)
{
	gpiod_set_value_cansleep(sensor->pwdn_gpio, enable ? 0 : 1);
}

static void ov8865_reset(struct ov8865_dev *sensor, bool enable)
{
	gpiod_set_value_cansleep(sensor->reset_gpio, enable ? 0 : 1);
}

static int ov8865_set_power_on(struct ov8865_dev *sensor)
{
	struct i2c_client *client = sensor->i2c_client;
	int ret = 0;

	ov8865_power(sensor, false);
	ov8865_reset(sensor,false);

	ret = clk_prepare_enable(sensor->xclk);
	if(ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		return ret;
	}

	ov8865_power(sensor, true);

	ret = regulator_bulk_enable(OV8865_NUM_SUPPLIES, sensor->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		goto xclk_off;
	}

	ov8865_reset(sensor,true);

	usleep_range(10000,12000);
	ret = ov8865_init_slave_id(sensor);
	if(ret)
		goto power_off;
	return 0;

power_off:
	ov8865_power(sensor, false);
	regulator_bulk_disable(OV8865_NUM_SUPPLIES, sensor->supplies);
xclk_off:
	clk_disable_unprepare(sensor->xclk);
	return ret;
}

static void ov8865_set_power_off(struct ov8865_dev *sensor)
{
	ov8865_power(sensor, false);
	regulator_bulk_disable(OV8865_NUM_SUPPLIES, sensor->supplies);
	clk_disable_unprepare(sensor->xclk);
}

static int ov8865_set_power(struct ov8865_dev *sensor, bool on)
{
	int ret = 0;

	if (on){
		ret = ov8865_set_power_on(sensor);
		if (ret)
			return ret;

		ret = ov8865_restore_mode(sensor);
		if (ret)
			goto power_off;
	}else {
		ov8865_set_power_off(sensor);
	}

	return 0;

power_off:
	ov8865_set_power_off(sensor);
	return ret;
}
/* --------------- Subdev Operations --------------- */

static int ov8865_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov8865_dev *sensor = to_ov8865_dev(sd);
	int ret = 0;

	mutex_lock(&sensor->lock);
	if (sensor->power_count == !on) {
		ret = ov8865_set_power(sensor, !!on);
		if (ret)
			goto out;
	}
	/* Update the power count. */
	sensor->power_count += on ? 1 : -1;
	WARN_ON(sensor->power_count < 0);
out:
	mutex_unlock(&sensor->lock);

	if (on && !ret && sensor->power_count == 1) {
		/*initialize hardware*/
		ret = v4l2_ctrl_handler_setup(&sensor->ctrls.handler);
	}

	return ret;
}

static int ov8865_try_frame_interval(struct ov8865_dev *sensor,
				     struct v4l2_fract *fi,
				     u32 width, u32 height)
{
	const struct ov8865_mode_info *mode;
	enum ov8865_frame_rate rate = OV8865_30_FPS;
	int minfps, maxfps, best_fps, fps;
	int i;

	minfps = ov8865_framerates[OV8865_30_FPS];
	maxfps = ov8865_framerates[OV8865_90_FPS];

	if (fi->numerator == 0) {
		fi->denominator = maxfps;
		fi->numerator = 1;
		rate = OV8865_90_FPS;
		goto find_mode;
	}

	fps = clamp_val(DIV_ROUND_CLOSEST(fi->denominator, fi->numerator),
			minfps, maxfps);

	best_fps = minfps;
	for (i = 0; i < ARRAY_SIZE(ov8865_framerates); i++) {
		int curr_fps = ov8865_framerates[i];

		if (abs(curr_fps - fps) < abs(best_fps - fps)) {
			best_fps = curr_fps;
			rate = i;
		}
	}

	fi->numerator = 1;
	fi->denominator = best_fps;
find_mode:
	mode = ov8865_find_mode(sensor, rate, width, height, false);
	return mode ? rate : -EINVAL;
}

static int ov8865_try_fmt_internal(struct v4l2_subdev *sd,
				   struct v4l2_mbus_framefmt *fmt,
				   enum ov8865_frame_rate fr,
				   const struct ov8865_mode_info **new_mode)
{
	struct ov8865_dev *sensor = to_ov8865_dev(sd);
	const struct ov8865_mode_info *mode;
	int i;

	mode = ov8865_find_mode(sensor, fr, fmt->width, fmt->height, true);
	if (!mode)
		return -EINVAL;
	fmt->width = mode->hact;
	fmt->height = mode->vact;

	if (new_mode)
		*new_mode = mode;

	for (i = 0; i < ARRAY_SIZE(ov8865_formats); i++)
		if (ov8865_formats[i].code == fmt->code)
			break;
	if (i >= ARRAY_SIZE(ov8865_formats))
		i = 0;

	fmt->code = ov8865_formats[i].code;
	fmt->colorspace = ov8865_formats[i].colorspace;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);

	return 0;
}

static int ov8865_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct ov8865_dev *sensor = to_ov8865_dev(sd);
	struct v4l2_mbus_framefmt *fmt;

	if (format->pad != 0)
		return -EINVAL;

	mutex_lock(&sensor->lock);
	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt = v4l2_subdev_get_try_format(&sensor->sd, cfg,
						 format->pad);
	else
		fmt = &sensor->fmt;
	format->format = *fmt;
		mutex_unlock(&sensor->lock);
	return 0;
}

static int ov8865_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct ov8865_dev *sensor = to_ov8865_dev(sd);
	const struct ov8865_mode_info *new_mode;
	struct v4l2_mbus_framefmt *mbus_fmt = &format->format;
	struct v4l2_mbus_framefmt *fmt;
	int ret;

	if (format->pad != 0)
		return -EINVAL;

	mutex_lock(&sensor->lock);

	if (sensor->streaming) {
		ret = -EBUSY;
		goto out;
	}

	ret = ov8865_try_fmt_internal(sd, mbus_fmt, sensor->current_fr,
				      &new_mode);
	if (ret)
		goto out;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt = v4l2_subdev_get_try_format(sd, cfg, 0);
	else
		fmt = &sensor->fmt;

	*fmt = *mbus_fmt;

	if (new_mode != sensor->current_mode) {
		sensor->current_mode = new_mode;
		sensor->pending_mode_change = true;
	}
	if (mbus_fmt->code != sensor->fmt.code)
		sensor->pending_fmt_change = true;

	/*__v4l2_ctrl_s_ctrl_int64(sensor->ctrls.pixel_rate,
				 ov8865_calc_pixel_rate(sensor));
*/
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

/*static int ov8865_init_controls(struct ov8865_dev *sensor)
{
	const struct v4l2_ctrl_ops *ops = &ov8865_ctrl_ops;
	struct ov8865_ctrls *ctrls = &sensor->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	int ret;

*/

static int ov8865_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->pad != 0)
		return -EINVAL;
	if (fse->index >= OV8865_NUM_MODES)
		return -EINVAL;

	fse->min_width = ov8865_mode_data[fse->index].hact;
	fse->max_width = fse->min_width;
	fse->min_height = ov8865_mode_data[fse->index].vact;
	fse->max_height = fse->min_height;

	return 0;
}

static int ov8865_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_subdev_frame_interval_enum
				      *fie)
{
	struct ov8865_dev *sensor = to_ov8865_dev(sd);
	struct v4l2_fract tpf;
	int ret;

	if (fie->pad != 0)
		return -EINVAL;
	if (fie->index >= OV8865_NUM_FRAMERATES)
		return -EINVAL;

	tpf.numerator = 1;
	tpf.denominator = ov8865_framerates[fie->index];

	ret = ov8865_try_frame_interval(sensor, &tpf,
					fie->width, fie->height);
	if (ret < 0)
		return -EINVAL;

	fie->interval = tpf;
	return 0;
}

static int ov8865_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct ov8865_dev *sensor = to_ov8865_dev(sd);

	mutex_lock(&sensor->lock);
	fi->interval = sensor->frame_interval;
	mutex_unlock(&sensor->lock);

	return 0;
}

static int ov8865_s_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct ov8865_dev *sensor = to_ov8865_dev(sd);
	const struct ov8865_mode_info *mode;
	int frame_rate, ret = 0;

	if (fi->pad !=0)
		return -EINVAL;

	mutex_lock(&sensor->lock);

	if (sensor->streaming) {
		ret = -EBUSY;
		goto out;
	}

	mode = sensor->current_mode;

	frame_rate = ov8865_try_frame_interval(sensor, &fi->interval,
					       mode->hact, mode->vact);
	 if (frame_rate < 0) {
		 fi->interval = sensor->frame_interval;
		 goto out;
	}

	mode = ov8865_find_mode(sensor, frame_rate, mode->hact,
				 mode->vact, true);
	if(!mode) {
		ret = -EINVAL;
		goto out;
	}

	if (mode != sensor->current_mode ||
	    frame_rate != sensor->current_fr) {
		sensor->current_fr = frame_rate;
		sensor->frame_interval = fi->interval;
		sensor->current_mode = mode;
		sensor->pending_mode_change = true;

/*		__v4l2_ctrl_s_ctrl_int64(sensor->ctrls.pixel_rate,
					 ov8865_calc_pixel_rate(sensor));
*/	}
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int ov8865_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad != 0)
		return -EINVAL;
	if (code->index >= ARRAY_SIZE(ov8865_formats))
		return -EINVAL;

	code->code = ov8865_formats[code->index].code;
	return 0;
}

static int ov8865_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov8865_dev *sensor = to_ov8865_dev(sd);
	int ret = 0;

	mutex_lock(&sensor->lock);

	if (sensor->streaming == !enable) {
		if (enable && sensor->pending_mode_change) {
			ret = ov8865_set_mode(sensor);
			if (ret)
				goto out;
		}

		/*if (enable && sensor->pending_fmt_change) {
			ret = ov8865_set_framefmt(sensor, &sensor->fmt);
			if (ret)
				goto out;
			sensor_>pending_fmt_change = false;
		}
*/
		ret = ov8865_write_reg(sensor, OV8865_REG_MIPI_CTRL, 
				       enable ? 0x72 : 0x62);
		if (ret)
			goto out;

		/*ret = ov8865_write_reg(sensor, OV8865_REG_FRAME_CTRL01,
				       on ?*/
		if(!ret)
			sensor->streaming = enable;
	}
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static const struct v4l2_subdev_core_ops ov8865_core_ops = {
	.s_power = ov8865_s_power,
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops ov8865_video_ops = {
	.g_frame_interval = ov8865_g_frame_interval,
	.s_frame_interval = ov8865_s_frame_interval,
	.s_stream = ov8865_s_stream,
};

static const struct v4l2_subdev_pad_ops ov8865_pad_ops = {
	.enum_mbus_code = ov8865_enum_mbus_code,
	.get_fmt = ov8865_get_fmt,
	.set_fmt = ov8865_set_fmt,
	.enum_frame_size = ov8865_enum_frame_size,
	.enum_frame_interval = ov8865_enum_frame_interval,
};

static const struct v4l2_subdev_ops ov8865_subdev_ops = {
	.core = &ov8865_core_ops,
	.video = &ov8865_video_ops,
	.pad = &ov8865_pad_ops,
};

static int ov8865_get_regulators(struct ov8865_dev *sensor)
{
	int i;

	for (i = 0; i < OV8865_NUM_SUPPLIES; i++)
		sensor->supplies[i].supply = ov8865_supply_name[i];

	return devm_regulator_bulk_get(&sensor->i2c_client->dev,
				       OV8865_NUM_SUPPLIES,
				       sensor->supplies);
}

static int ov8865_check_chip_id(struct ov8865_dev *sensor)
{
	struct i2c_client *client = sensor->i2c_client;
	int ret = 0;
	u8 chip_id_0, chip_id_1, chip_id_2;
	u32 chip_id = 0x000000;

	ret = ov8865_set_power_on(sensor);
	if (ret)
		return ret;

	ret = ov8865_read_reg(sensor, OV8865_REG_CHIP_ID, &chip_id_0);
	ret = ov8865_read_reg(sensor, OV8865_REG_CHIP_ID+1, &chip_id_1);
	ret = ov8865_read_reg(sensor, OV8865_REG_CHIP_ID+2, &chip_id_2);
	if (ret) {
		dev_err(&client->dev, "%s: failed to reach chip identifier\n",

			__func__);
		goto power_off;
	}
	chip_id = ((u32)chip_id_0 << 16) | ((u32)chip_id_1 << 8) | ((u32)chip_id_2);

	if (chip_id != 0x008865) {
		dev_err(&client->dev, "%s: wrong chip identifier, expected 0x8865, got 0x%x\n",__func__, chip_id);
		ret = -ENXIO;
	}

power_off:
	ov8865_set_power_off(sensor);
	return ret;
}

static int ov8865_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct fwnode_handle *endpoint;
	struct ov8865_dev *sensor;
	struct v4l2_mbus_framefmt *fmt;
	u32 rotation;
	int ret = 0;
	pr_info("ov8865: probe");

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->i2c_client = client;

	/*
	 * default init sequence initialize sensor to
	 * RAW SBGGR10 QUXGA@30fps
	 */
	fmt = &sensor->fmt;
	fmt->code = MEDIA_BUS_FMT_SRGGB10_1X10;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
	fmt->width = 3264;
	fmt->height = 2448;
	fmt->field = V4L2_FIELD_NONE;
	sensor->frame_interval.numerator = 1;
	sensor->frame_interval.denominator = ov8865_framerates[OV8865_30_FPS];
	sensor->current_fr = OV8865_30_FPS;
	sensor->current_mode = &ov8865_mode_data[OV8865_MODE_QUXGA_3264_2448];
	sensor->last_mode = sensor->current_mode;
	//sensor->ae_target = 52;

	/* optional indication of physical rotation of sensor */
	ret = fwnode_property_read_u32(dev_fwnode(&client->dev), "rotation",
				       &rotation);
	if (!ret) {
		switch (rotation) {
		case 180:
			sensor->upside_down = true;
			/* fall through */
		case 0:
			break;
		default:
			dev_warn(dev, "%u degrees rotation is not supported, ignoring..\n", 
				 rotation);
		}
	}

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(&client->dev),
						  NULL);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(endpoint, &sensor->ep);
	fwnode_handle_put(endpoint);
	if (ret) {
		dev_err(dev, "Could not parse endpoint\n");
		return ret;
	}

	/* get system clock (xclk) */
	sensor->xclk = devm_clk_get(dev, "xclk");
	if (IS_ERR(sensor->xclk)) {
		dev_err(dev, "failed to get xclk\n");
		return PTR_ERR(sensor->xclk);
	}

	sensor->xclk_freq = clk_get_rate(sensor->xclk);
	if (sensor->xclk_freq < OV8865_XCLK_MIN ||
	    sensor->xclk_freq > OV8865_XCLK_MAX) {
		dev_err(dev, "xclk frequency out of range: %d Hz\n",
			sensor->xclk_freq);
		return -EINVAL;
	}
	/* request optional power down pin */
	sensor->pwdn_gpio = devm_gpiod_get_optional(dev, "powerdown",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->pwdn_gpio))
		return PTR_ERR(sensor->pwdn_gpio);

	/* request optional reset pin */
	sensor->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->reset_gpio))
		return PTR_ERR(sensor->reset_gpio);

	v4l2_i2c_subdev_init(&sensor->sd, client, &ov8865_subdev_ops);
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			    V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		return ret;

	ret = ov8865_get_regulators(sensor);
	if (ret)
		return ret;

	mutex_init(&sensor->lock);

	ret = ov8865_check_chip_id(sensor);
	if (ret)
		goto entity_cleanup;
	return 0;

/*	ret = ov8865_init_controls(sensor);
	if (ret)
		goto entity_cleanup;
*/
	ret = v4l2_async_register_subdev_sensor_common(&sensor->sd);
	if (ret)
		goto free_ctrls;

	return 0;
free_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrls.handler);
entity_cleanup:
	mutex_destroy(&sensor->lock);
	media_entity_cleanup(&sensor->sd.entity);
	return ret;
}


static int ov8865_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov8865_dev *sensor = to_ov8865_dev(sd);

	v4l2_async_unregister_subdev(&sensor->sd);
	mutex_destroy(&sensor->lock);
	media_entity_cleanup(&sensor->sd.entity);
	v4l2_ctrl_handler_free(&sensor->ctrls.handler);

	return 0;
}

static const struct i2c_device_id ov8865_id[] = {
	{"ov8865", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, ov8865_id);

static const struct of_device_id ov8865_dt_ids[] = {
	{ .compatible = "ovti,ov8865" },
	{}
};
MODULE_DEVICE_TABLE(of, ov8865_dt_ids);

static struct i2c_driver ov8865_i2c_driver = {
         .driver       = {
		 .name = "ov8865",
		 .of_match_table = ov8865_dt_ids,
	 },
	 .id_table     = ov8865_id,
	 .probe_new    = ov8865_probe,
	 .remove       = ov8865_remove,
};

module_i2c_driver(ov8865_i2c_driver);

MODULE_DESCRIPTION("OV8865 MIPI Camera Subdev Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kévin L'hôpital <kevin.lhopital@bootlin.com>");
