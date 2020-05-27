// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright Kévin L'hôpital (C) 2020
 */

#ifndef __SUN6I_MIPI_CSI_H_
#define __SUN6I_MIPI_CSI_H_
#include <linux/regmap.h>
#include "sun6i_csi.h"

void sun6i_mipi_csi_set_stream(struct sun6i_csi *csi, bool enable);
void sun6i_mipi_csi_setup_bus(struct sun6i_csi *csi);

/*
#include "sun6i_video.h"

struct sun6i_mipi_csi;
*/
/**
 * struct sun6i_mipi_csi_config - configs for sun6i mipi_csi
 * @pixelformat: v4l2 pixel format (V4L2_PIX_FMT_*)
 * @code:       media bus format code (MEDIA_BUS_FMT_*)
 * @field:      used interlacing type (enum v4l2_field)
 * @width:      frame width
 * @height:     frame height
 */
/*
struct sun6i_mipi_csi_config {
	u32	pixelformat;
	u32	code;
	u32	field;
	u32	width;
	u32	height;
};

struct sun6i_mipi_csi {
	struct device			*dev;
	struct v4l2_ctrl_handler	ctrl_handler;
	struct v4l2_device		v4l2_dev;
	struct media_device		media_dev;

	struct v4l2_async_notifier	notifier;

*/	/* video port settings */
/*	struct v4l2_fwnode_endpoint	v4l2_ep;

	struct sun6i_csi_config		config;
	struct sun6i_video		video;
};
*/
/**
 * sun6i_mipi_csi_is_format_supported() - check if the format supported by
 * mipi_csi
 * @mipi_csi:   pointer to the mipi_csi
 * @pixformat:  v4l2 pixel format (V4L2_PIX_FMT_*)
 * @mbus_code:  media bus format code (MEDIA_BUS_FMT_*)
 */
/*bool sun6i_mipi_csi_is_format_supported(struct sun6i_mipi_csi *mipi_csi, u32
					pixformat, u32 mbus_code);
*/
/**
 * sun6i_mipi_csi_set_power() - power on/off the mipi_csi
 * @mipi_csi:	pointer to the mipi_csi
 * @enable:	on/off
 */
//int sun6i_mipi_csi_set_power(struct sun6i_mipi_csi *mipi_csi, bool enable);

/**
 * sun6i_mipi_csi_update_config() - update the mipi_csi register setting
 * @mipi_csi:	pointer to the mipi_csi
 * @config:	see struct sun6i_mipi_csi_config
 */
/*int sun6i_mipi_csi_update_config(struct sun6i_mipi_csi *csi,
				 struct sun6i_csi_config *config);
*/
/**
 * sun6i_mipi_csi_update_buf _addr() - updatethe csi frame buffer address
 * @mipi_csi:	pointer to the mipi_csi
 * @addr:	frame buffer's physical address
 */
/*void sun6i_mipi_csi_update_buf_addr(struct sun6i_mipi_csi *mipi_csi, dma_addr_t
				    addr);
*/
/**
 * sun6i_mipi_csi_set_stream() - start/stop mipi_csi streaming
 * @mipi_csi:	pointer to the mipi_csi
 * @enable:	start/stop
 */
//void sun6i_mipi_csi_set_stream(struct sun6i_mipi_csi *mipi_csi, bool enable);

#endif /* __SUN6I_MIPI_CSI_H__ */
