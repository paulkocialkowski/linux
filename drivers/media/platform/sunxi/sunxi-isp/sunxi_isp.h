/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2020 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef __SUNXI_ISP_H__
#define __SUNXI_ISP_H__

/* XXX: is it all-in-one SP or P? */
#define SUNXI_ISP_INPUT_FMT_YUV420	0
#define SUNXI_ISP_INPUT_FMT_YUV422	1
#define SUNXI_ISP_INPUT_FMT_RAW		2

#define SUNXI_ISP_INPUT_SEQ_YUYV	0
#define SUNXI_ISP_INPUT_SEQ_YVYU	1
#define SUNXI_ISP_INPUT_SEQ_UYVY	2
#define SUNXI_ISP_INPUT_SEQ_VYUY	3
#define SUNXI_ISP_INPUT_SEQ_BGGR	4
#define SUNXI_ISP_INPUT_SEQ_RGGB	5
#define SUNXI_ISP_INPUT_SEQ_GBRG	6
#define SUNXI_ISP_INPUT_SEQ_GRBG	7

#define SUNXI_ISP_OUTPUT_FMT_YUV420SP	0
#define SUNXI_ISP_OUTPUT_FMT_YUV422SP	1
#define SUNXI_ISP_OUTPUT_FMT_YUV420P	2
#define SUNXI_ISP_OUTPUT_FMT_YUV422P	3

#define SUNXI_ISP_OUTPUT_SEQ_UV		0
#define SUNXI_ISP_OUTOUT_SEQ_VU		1

struct sunxi_isp_format {
	u32 pixelformat;
	u8 fmt;
	u8 seq;
};

struct sunxi_isp_setup {
	u32 width;
	u32 height;
	u32 colorspace;
	u32 ycbcr_enc;
	u32 quantization;
	u32 xfer_func;
};

struct sunxi_isp_video {
	struct video_device video_dev;
	struct v4l2_device v4l2_dev;
	struct v4l2_m2m_dev *m2m_dev;
};

struct sunxi_isp_device {
	struct device *dev;

	struct regmap *regmap;
	struct clk *clk_bus;
	struct clk *clk_mod;
	struct reset_control *reset;

	struct mutex file_mutex;

	struct sunxi_isp_video video;
};

struct sunxi_isp_context {
	struct sunxi_isp_device *dev;

	struct v4l2_fh v4l2_fh;

	const struct sunxi_isp_format *format_src;
	const struct sunxi_isp_format *format_dst;

	struct sunxi_isp_setup setup_src;
	struct sunxi_isp_setup setup_dst;
};

#endif
