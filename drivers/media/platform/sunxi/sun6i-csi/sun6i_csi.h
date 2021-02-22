/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2011-2018 Magewell Electronics Co., Ltd. (Nanjing)
 * All rights reserved.
 * Author: Yong Deng <yong.deng@magewell.com>
 */

#ifndef __SUN6I_CSI_H__
#define __SUN6I_CSI_H__

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#include "sun6i_video.h"
#include "sunxi_isp.h"

struct sun6i_csi;

/**
 * struct sun6i_csi_config - configs for sun6i csi
 * @pixelformat: v4l2 pixel format (V4L2_PIX_FMT_*)
 * @code:	media bus format code (MEDIA_BUS_FMT_*)
 * @field:	used interlacing type (enum v4l2_field)
 * @width:	frame width
 * @height:	frame height
 */
struct sun6i_csi_config {
	u32		pixelformat;
	u32		code;
	u32		field;
	u32		width;
	u32		height;
};

/*
struct sunxi_isp_memory {
	void *lut_table;
	dma_addr_t lut_table_dma;
	unsigned int lut_table_size;

	void *drc_table;
	dma_addr_t drc_table_dma;
	unsigned int drc_table_size;

	void *stat;
	dma_addr_t stat_dma;
	unsigned int stat_size;

	void *reg_load;
	dma_addr_t reg_load_dma;
	unsigned int reg_load_size;

	void *reg_save;
	dma_addr_t reg_save_dma;
	unsigned int reg_save_size;
};

struct sunxi_isp_device {
	void *io;
	struct device			*dev;
	struct sunxi_isp_memory memory;
};
*/
struct sun6i_csi {
	struct device			*dev;
	struct v4l2_ctrl_handler	ctrl_handler;
	struct v4l2_device		v4l2_dev;
	struct media_device		media_dev;

	struct v4l2_async_subdev	subdev;
	struct v4l2_async_notifier	notifier;

	struct sun6i_csi_config		config;

	struct sun6i_video		video;

	struct sunxi_isp_device isp;
};

/**
 * sun6i_csi_is_format_supported() - check if the format supported by csi
 * @csi:	pointer to the csi
 * @endpoint:	pointer to the endpoint to check
 * @pixformat:	v4l2 pixel format (V4L2_PIX_FMT_*)
 * @mbus_code:	media bus format code (MEDIA_BUS_FMT_*)
 */
bool sun6i_csi_is_format_supported(struct sun6i_csi *csi,
				   struct v4l2_fwnode_endpoint *endpoint,
				   u32 pixformat, u32 mbus_code);

/**
 * sun6i_csi_set_power() - power on/off the csi
 * @csi:	pointer to the csi
 * @enable:	on/off
 */
int sun6i_csi_set_power(struct sun6i_csi *csi, bool enable);

/**
 * sun6i_csi_update_config() - update the csi register settings
 * @csi:	pointer to the csi
 * @config:	see struct sun6i_csi_config
 */
int sun6i_csi_update_config(struct sun6i_csi *csi,
			    struct sun6i_csi_config *config);

/**
 * sun6i_csi_update_buf_addr() - update the csi frame buffer address
 * @csi:	pointer to the csi
 * @addr:	frame buffer's physical address
 */
void sun6i_csi_update_buf_addr(struct sun6i_csi *csi, dma_addr_t addr);

/**
 * sun6i_csi_set_stream() - start/stop csi streaming
 * @csi:	pointer to the csi
 * @enable:	start/stop
 */
void sun6i_csi_set_stream(struct sun6i_csi *csi, bool enable);

/* get memory storage bpp from v4l2 pixformat */
static inline int sun6i_csi_get_bpp(unsigned int pixformat)
{
	const struct v4l2_format_info *info;
	unsigned int i;
	int bpp = 0;

	/* Handle special cases unknown to V4L2 format info first. */
	switch (pixformat) {
	case V4L2_PIX_FMT_JPEG:
		return 8;
	case V4L2_PIX_FMT_HM12:
		return 12;
	case V4L2_PIX_FMT_RGB565X:
		return 16;
	}

	info = v4l2_format_info(pixformat);
	if (!info) {
		WARN(1, "Unsupported pixformat: 0x%x\n", pixformat);
		return 0;
	}

	for (i = 0; i < info->comp_planes; i++) {
		unsigned int hdiv = (i == 0) ? 1 : info->hdiv;
		unsigned int vdiv = (i == 0) ? 1 : info->vdiv;

		/* We return bits per pixel while V4L2 format info is bytes. */
		bpp += 8 * info->bpp[i] / hdiv / vdiv;
	}

	return bpp;
}

#endif /* __SUN6I_CSI_H__ */
