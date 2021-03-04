// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/soc/sunxi/sunxi_sram.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-v4l2.h>

#include "sunxi_isp.h"

#define SUNXI_ISP_NAME	"sunxi-isp"

#define sunxi_isp_file_context(f) \
	container_of((f)->private_data, struct sunxi_isp_context, v4l2_fh)

/*
 * TODO:
 * - media device
 */

static void test(struct sunxi_isp_device *isp_dev)
{
	struct regmap *regmap = isp_dev->regmap;
	u32 version = 0;
	u32 reg = 0;
	unsigned int max = 0x240;
	unsigned int i = 0;
//	u8 bits = GENMASK(7,1) | GENMASK(15,10) | GENMASK(31,18);

	printk(KERN_ERR "-- ISP reg dump --\n");

	for (i = 0; i < max; i += 4) {
		reg = 0;
		regmap_read(regmap, i, &reg);
		printk(KERN_ERR "ISP [%04x] %#x\n", i, reg);
	}
}

static void dump(void *save, char *prefix)
{
	u32 reg = 0;
	unsigned int max = 0x240;
	unsigned int i = 0;
	u32 *pointer = save;

	printk(KERN_ERR "-- ISP data dump --\n");

	for (i = 0; i < max; i += 4) {
		printk(KERN_ERR "%s [%04x] %#x\n", prefix, i, *pointer);
		pointer++;
	}
}

static const struct sunxi_isp_format sunxi_isp_formats_src[] = {
	{
		.pixelformat	= V4L2_PIX_FMT_NV12M,
		.fmt		= SUNXI_ISP_INPUT_FMT_YUV420,
		.seq		= 0,
	},
};

static const struct sunxi_isp_format sunxi_isp_formats_dst[] = {
	{
		.pixelformat	= V4L2_PIX_FMT_NV12M,
		.fmt		= SUNXI_ISP_OUTPUT_FMT_YUV420SP,
		.seq		= SUNXI_ISP_OUTPUT_SEQ_UV,
	},
};

static void sunxi_isp_write(struct sunxi_isp_device *isp_dev, u32 offset,
			    u32 value)
{
	struct sunxi_isp_memory *memory = &isp_dev->memory;
	u32 *reg = (u32 *)(memory->reg_load + offset);

	*reg = value;
}

static int sunxi_isp_memory_setup(struct sunxi_isp_device *isp_dev)
{
	struct sunxi_isp_memory *memory = &isp_dev->memory;

	memory->lut_table_size = 0xe00;
	memory->lut_table =
		dma_alloc_coherent(isp_dev->dev, memory->lut_table_size,
				   &memory->lut_table_dma, GFP_KERNEL);
	memory->lut_table_dma -= 0x40000000;

	memory->drc_table_size = 0x600;
	memory->drc_table =
		dma_alloc_coherent(isp_dev->dev, memory->drc_table_size,
				   &memory->drc_table_dma, GFP_KERNEL);
	memory->drc_table_dma -= 0x40000000;

	memory->stat_size = 0x2100;
	memory->stat = dma_alloc_coherent(isp_dev->dev, memory->stat_size,
					  &memory->stat_dma, GFP_KERNEL);
	memory->stat_dma -= 0x40000000;

	memory->reg_load_size = 0x1000;
	memory->reg_load =
		dma_alloc_coherent(isp_dev->dev, memory->reg_load_size,
				   &memory->reg_load_dma, GFP_KERNEL);
	memory->reg_load_dma -= 0x40000000;

	memory->reg_save_size = 0x1000;
	memory->reg_save =
		dma_alloc_coherent(isp_dev->dev, memory->reg_save_size,
				   &memory->reg_save_dma, GFP_KERNEL);
	memory->reg_save_dma -= 0x40000000;

	printk(KERN_ERR "LUT table %#x, DRC table %#x, stats %#x\n", memory->lut_table_dma, memory->drc_table_dma, memory->stat_dma);

	return 0;
}

void __iomem *isp_io = NULL;

static irqreturn_t sunxi_isp_interrupt(int irq, void *private)
{
	struct sunxi_isp_device *isp_dev = private;
	struct regmap *regmap = isp_dev->regmap;
	u32 status;
	u32 value;

	printk(KERN_ERR "%s()\n", __func__);

	regmap_read(regmap, SUNXI_ISP_FE_INT_STA_REG, &status);

	if (!status)
		return IRQ_NONE;

	printk(KERN_ERR "%s: status is %#x\n", __func__, status);

	regmap_write(regmap, SUNXI_ISP_FE_INT_STA_REG, status);

/*
	test(isp_dev);
	dump(isp_dev->memory.reg_save, "SAVE");
	dump(isp_dev->memory.reg_load, "LOAD");
*/

	return IRQ_HANDLED;
}

#define ISP_LINEAR_LUT_LENS_GAMMA_MEM_SIZE 0xE00
#define ISP_STAT_TOTAL_SIZE         0x2100
#define ISP_DRC_DISC_MEM_SIZE            0x0600

#define CSI_EN_REG			0x0
#define CSI_EN_VER_EN				BIT(30)
#define CSI_EN_CSI_EN				BIT(0)

void sun6i_csi_enable(struct sunxi_isp_device *isp_dev)
{
	struct regmap *regmap = isp_dev->regmap_csi;

	printk(KERN_ERR "%s: CSI enable\n", __func__);

	regmap_write(regmap, CSI_EN_REG, CSI_EN_CSI_EN | 0x2);
}

void sunxi_isp_device_run(void *private)
{
	struct sunxi_isp_context *isp_ctx = private;
	struct sunxi_isp_device *isp_dev = isp_ctx->dev;
	struct sunxi_isp_memory *memory = &isp_dev->memory;
	struct regmap *regmap = isp_dev->regmap;
	struct v4l2_m2m_ctx *m2m_ctx = isp_ctx->v4l2_fh.m2m_ctx;
	struct vb2_v4l2_buffer *buffer_src, *buffer_dst;
	/* XXX: luma_src_dma_addr */
	dma_addr_t dma_addr_luma_src, dma_addr_chroma_src;
	dma_addr_t dma_addr_luma_dst, dma_addr_chroma_dst;
	unsigned int width, height;
	u32 value;

	width = 1280;
	height = 480;

	buffer_src = v4l2_m2m_next_src_buf(m2m_ctx);
	buffer_dst = v4l2_m2m_next_dst_buf(m2m_ctx);

	memcpy(memory->reg_load, isp_io, 0x240);

	/* CSI */

	sun6i_csi_enable(isp_dev);

	/* Frontend Config */

	value = SUNXI_ISP_FE_CFG_EN |
		SUNXI_ISP_FE_CFG_SRC0_MODE(SUNXI_ISP_SRC_MODE_DRAM);
	regmap_write(regmap, SUNXI_ISP_FE_CFG_REG, value);

	/* Tables */

	regmap_write(regmap, SUNXI_ISP_REG_LOAD_ADDR_REG,
		     memory->reg_load_dma >> 2);
	regmap_write(regmap, SUNXI_ISP_REG_SAVE_ADDR_REG,
		     memory->reg_save_dma >> 2);

	regmap_write(regmap, SUNXI_ISP_LUT_TABLE_ADDR_REG,
		     memory->lut_table_dma >> 2);
	regmap_write(regmap, SUNXI_ISP_DRC_TABLE_ADDR_REG,
		     memory->drc_table_dma >> 2);
	regmap_write(regmap, SUNXI_ISP_STATS_ADDR_REG,
		     memory->stat_dma >> 2);

	/* Module */

	value = SUNXI_ISP_MODULE_EN_SRC0;
	sunxi_isp_write(isp_dev, SUNXI_ISP_MODULE_EN_REG, value);

	dma_addr_luma_src =
		vb2_dma_contig_plane_dma_addr(&buffer_src->vb2_buf, 0);
	dma_addr_chroma_src =
		vb2_dma_contig_plane_dma_addr(&buffer_src->vb2_buf, 1);

	dma_addr_luma_dst =
		vb2_dma_contig_plane_dma_addr(&buffer_dst->vb2_buf, 0);
	dma_addr_chroma_dst =
		vb2_dma_contig_plane_dma_addr(&buffer_dst->vb2_buf, 1);

	dma_addr_luma_src -= 0x40000000;
	dma_addr_chroma_src -= 0x40000000;
	dma_addr_luma_dst -= 0x40000000;
	dma_addr_chroma_dst -= 0x40000000;

	printk(KERN_ERR "%s: src %#x/%#x -> dst %#x/%#x\n", __func__,
	       dma_addr_luma_src, dma_addr_chroma_src, dma_addr_luma_dst,
	       dma_addr_chroma_dst);

	/* AE */

	value = SUNXI_ISP_AE_SIZE_WIDTH((width >> 1) - 1) |
		SUNXI_ISP_AE_SIZE_HEIGHT((height >> 1) - 1);
	sunxi_isp_write(isp_dev, SUNXI_ISP_AE_SIZE_REG, value);

	value = SUNXI_ISP_AE_POS_HORZ_START(0) |
		SUNXI_ISP_AE_POS_VERT_START(0);
	sunxi_isp_write(isp_dev, SUNXI_ISP_AE_POS_REG, value);

	/* OB */

	value = SUNXI_ISP_OB_SIZE_WIDTH(width) |
		SUNXI_ISP_OB_SIZE_HEIGHT(height);
	sunxi_isp_write(isp_dev, SUNXI_ISP_OB_SIZE_REG, value);

	value = SUNXI_ISP_OB_VALID_WIDTH(width) |
		SUNXI_ISP_OB_VALID_HEIGHT(height);
	sunxi_isp_write(isp_dev, SUNXI_ISP_OB_VALID_REG, value);

	sunxi_isp_write(isp_dev, SUNXI_ISP_OB_SRC0_VALID_START_REG, 0);

	value = SUNXI_ISP_OB_SPRITE_WIDTH(width) |
		SUNXI_ISP_OB_SPRITE_HEIGHT(height);
	sunxi_isp_write(isp_dev,SUNXI_ISP_OB_SPRITE_REG, value);

	/* Bayer offset/gain */

	sunxi_isp_write(isp_dev, 0xe0, 0x200020);
	sunxi_isp_write(isp_dev, 0xe4, 0x200020);
	sunxi_isp_write(isp_dev, 0xe8, 0x1000100);
	sunxi_isp_write(isp_dev, 0xec, 0x100);

	/* Mode */

	value = SUNXI_ISP_MODE_INPUT_FMT(SUNXI_ISP_INPUT_FMT_RAW_BGGR) |
		SUNXI_ISP_MODE_INPUT_YUV_SEQ(0) |
		SUNXI_ISP_MODE_SHARP(1) |
		SUNXI_ISP_MODE_HIST(2);
	sunxi_isp_write(isp_dev, SUNXI_ISP_MODE_REG, value);

	/* SRC0 Input */

	sunxi_isp_write(isp_dev, SUNXI_ISP_IN_CFG_REG,
			SUNXI_ISP_IN_CFG_STRIDE_DIV16(width / 16));

	sunxi_isp_write(isp_dev, SUNXI_ISP_IN_LUMA_RGB_ADDR0_REG,
		     dma_addr_luma_src >> 2);
	sunxi_isp_write(isp_dev, SUNXI_ISP_IN_CHROMA_ADDR0_REG,
		     dma_addr_chroma_src >> 2);

	/* MCH Output */

	value = SUNXI_ISP_MCH_SIZE_CFG_WIDTH(width) |
		SUNXI_ISP_MCH_SIZE_CFG_HEIGHT(height);
	sunxi_isp_write(isp_dev, SUNXI_ISP_MCH_SIZE_CFG_REG, value);

	value = SUNXI_ISP_MCH_SCALE_CFG_X_RATIO(1) |
		SUNXI_ISP_MCH_SCALE_CFG_Y_RATIO(1) |
		SUNXI_ISP_MCH_SCALE_CFG_WEIGHT_SHIFT(0);
	sunxi_isp_write(isp_dev, SUNXI_ISP_MCH_SCALE_CFG_REG, value);

	/* YUV420 SP mode */
	value = SUNXI_ISP_MCH_CFG_EN |
		SUNXI_ISP_MCH_CFG_MODE(0) |
		SUNXI_ISP_MCH_CFG_STRIDE_Y_DIV4(width/4) |
		SUNXI_ISP_MCH_CFG_STRIDE_UV_DIV4(width/4);
	sunxi_isp_write(isp_dev, SUNXI_ISP_MCH_CFG_REG, value);

	sunxi_isp_write(isp_dev, SUNXI_ISP_MCH_Y_ADDR0_REG, dma_addr_luma_dst >> 2);
	sunxi_isp_write(isp_dev, SUNXI_ISP_MCH_U_ADDR0_REG, dma_addr_chroma_dst >> 2);

	/* Frontend Config */

	value = SUNXI_ISP_FE_CFG_EN |
		SUNXI_ISP_FE_CFG_SRC0_MODE(SUNXI_ISP_SRC_MODE_DRAM);
	regmap_write(regmap, SUNXI_ISP_FE_CFG_REG, value);

	/* Para Ready */

	regmap_read(regmap, SUNXI_ISP_FE_CTRL_REG, &value);
	value |= SUNXI_ISP_FE_CTRL_PARA_READY;
	regmap_write(regmap, SUNXI_ISP_FE_CTRL_REG, value);

	/* SRAM Clear */

//	regmap_write(regmap, SUNXI_ISP_SRAM_RW_OFFSET_REG, BIT(31));

	/* Interrupt */

	regmap_write(regmap, SUNXI_ISP_FE_INT_LINE_NUM_REG, 4);

	regmap_write(regmap, SUNXI_ISP_FE_INT_STA_REG, 0xff);
	regmap_write(regmap, SUNXI_ISP_FE_INT_EN_REG, 0xff);
/*
		     SUNXI_ISP_FE_INT_EN_FINISH |
		     SUNXI_ISP_FE_INT_EN_START |
		     SUNXI_ISP_FE_INT_EN_SRC0_FIFO);
*/

//	regmap_write(regmap, SUNXI_ISP_FE_ROT_OF_CFG_REG, 0x10);

	/* Frontend Control */

	regmap_read(regmap, SUNXI_ISP_FE_CTRL_REG, &value);
	value |= SUNXI_ISP_FE_CTRL_VCAP_EN;
	regmap_write(regmap, SUNXI_ISP_FE_CTRL_REG, value);
}

static const struct v4l2_m2m_ops sunxi_isp_m2m_ops = {
	.device_run	= sunxi_isp_device_run,
};

static int sunxi_isp_querycap(struct file *file, void *private,
			      struct v4l2_capability *capability)
{
	printk(KERN_ERR "%s()\n", __func__);

	strscpy(capability->driver, SUNXI_ISP_NAME, sizeof(capability->driver));
	strscpy(capability->card, SUNXI_ISP_NAME, sizeof(capability->card));
	snprintf(capability->bus_info, sizeof(capability->bus_info),
		 "platform:%s", SUNXI_ISP_NAME);

	printk(KERN_ERR "%s() out\n", __func__);

	return 0;
}

static int sunxi_isp_context_format_setup(struct sunxi_isp_context *isp_ctx,
					  u32 format_type,
					  const struct sunxi_isp_format **format,
					  struct sunxi_isp_setup **setup)
{
	printk(KERN_ERR "%s()\n", __func__);

	switch (format_type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		if (format)
			*format = isp_ctx->format_src;

		if (setup)
			*setup = &isp_ctx->setup_src;
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		if (format)
			*format = isp_ctx->format_dst;

		if (setup)
			*setup = &isp_ctx->setup_dst;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sunxi_isp_enum_fmt(struct file *file, void *private,
			      struct v4l2_fmtdesc *fmtdesc)
{
	const struct sunxi_isp_format *formats;
	unsigned int formats_count;

	printk(KERN_ERR "%s()\n", __func__);

	/* XXX: function to get formats+count and lookup too */
	switch (fmtdesc->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		formats = sunxi_isp_formats_src;
		formats_count = ARRAY_SIZE(sunxi_isp_formats_src);
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		formats = sunxi_isp_formats_dst;
		formats_count = ARRAY_SIZE(sunxi_isp_formats_dst);
		break;
	default:
		return -EINVAL;
	}

	if (fmtdesc->index >= formats_count)
		return -EINVAL;

	fmtdesc->pixelformat = formats[fmtdesc->index].pixelformat;

	return 0;
}

static int sunxi_isp_g_fmt(struct file *file, void *private,
			   struct v4l2_format *v4l2_format)
{
	struct sunxi_isp_context *isp_ctx = sunxi_isp_file_context(file);
	const struct sunxi_isp_format *format;
	struct sunxi_isp_setup *setup;
	int ret;

	printk(KERN_ERR "%s()\n", __func__);

	ret = sunxi_isp_context_format_setup(isp_ctx, v4l2_format->type,
					     &format, &setup);
	if (ret)
		return ret;

	v4l2_format->fmt.pix_mp.pixelformat = format->pixelformat;
	v4l2_format->fmt.pix_mp.width = setup->width;
	v4l2_format->fmt.pix_mp.height = setup->height;
	v4l2_format->fmt.pix_mp.num_planes = 2;
	v4l2_format->fmt.pix_mp.plane_fmt[0].bytesperline = setup->width;
	v4l2_format->fmt.pix_mp.plane_fmt[0].sizeimage = setup->width * setup->height;
	v4l2_format->fmt.pix_mp.plane_fmt[1].bytesperline = setup->width;
	v4l2_format->fmt.pix_mp.plane_fmt[1].sizeimage = setup->width * setup->height / 2;

	/* TODO: the other stuff. */

	return 0;
}

static int sunxi_isp_try_fmt(struct file *file, void *private,
			     struct v4l2_format *format)
{
	const struct sunxi_isp_format *formats;
	unsigned int formats_count;
	unsigned int i;

	printk(KERN_ERR "%s()\n", __func__);

	switch (format->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		formats = sunxi_isp_formats_src;
		formats_count = ARRAY_SIZE(sunxi_isp_formats_src);
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		formats = sunxi_isp_formats_dst;
		formats_count = ARRAY_SIZE(sunxi_isp_formats_dst);
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < formats_count; i++)
		if (formats[i].pixelformat == format->fmt.pix_mp.pixelformat)
			break;

	if (i == formats_count)
		format->fmt.pix_mp.pixelformat = formats[0].pixelformat;

	return sunxi_isp_g_fmt(file, private, format);
}

static int sunxi_isp_s_fmt(struct file *file, void *private,
			   struct v4l2_format *v4l2_format)
{
	struct sunxi_isp_context *isp_ctx = sunxi_isp_file_context(file);
	const struct sunxi_isp_format *formats;
	unsigned int formats_count;
	struct sunxi_isp_setup *setup;
	unsigned int i;
	int ret;

	printk(KERN_ERR "%s()\n", __func__);

	switch (v4l2_format->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		formats = sunxi_isp_formats_src;
		formats_count = ARRAY_SIZE(sunxi_isp_formats_src);
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		formats = sunxi_isp_formats_dst;
		formats_count = ARRAY_SIZE(sunxi_isp_formats_dst);
		break;
	default:
		return -EINVAL;
	}

	ret = sunxi_isp_context_format_setup(isp_ctx, v4l2_format->type,
					     NULL, &setup);
	if (ret)
		return ret;

	ret = sunxi_isp_try_fmt(file, private, v4l2_format);
	if (ret)
		return ret;

/*
	for (i = 0; i < formats_count; i++)
		if (formats[i].pixelformat == format->fmt.pix_mp.pixelformat)
			break;
*/
	// XXX: need to replace format ptr here with lookup
//	format->pixelformat = v4l2_format->fmt.pix_mp.pixelformat;
	setup->width = v4l2_format->fmt.pix_mp.width;
	setup->height = v4l2_format->fmt.pix_mp.height;

	return 0;
}

static const struct v4l2_ioctl_ops sunxi_isp_ioctl_ops = {
	.vidioc_querycap		= sunxi_isp_querycap,

	.vidioc_enum_fmt_vid_cap	= sunxi_isp_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane	= sunxi_isp_g_fmt,
	.vidioc_try_fmt_vid_cap_mplane	= sunxi_isp_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane	= sunxi_isp_s_fmt,

	.vidioc_enum_fmt_vid_out	= sunxi_isp_enum_fmt,
	.vidioc_g_fmt_vid_out_mplane	= sunxi_isp_g_fmt,
	.vidioc_try_fmt_vid_out_mplane	= sunxi_isp_try_fmt,
	.vidioc_s_fmt_vid_out_mplane	= sunxi_isp_s_fmt,

	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,

	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static int sunxi_isp_queue_setup(struct vb2_queue *queue,
				 unsigned int *buffers_count,
				 unsigned int *planes_count,
				 unsigned int sizes[],
				 struct device *alloc_devs[])
{
	struct sunxi_isp_context *isp_ctx = vb2_get_drv_priv(queue);
	const struct sunxi_isp_format *format;
	struct sunxi_isp_setup *setup;
	int ret;

	printk(KERN_ERR "%s()\n", __func__);

	ret = sunxi_isp_context_format_setup(isp_ctx, queue->type, &format,
					     &setup);
	if (ret)
		return ret;

	sizes[0] = setup->width * setup->height;
	sizes[1] = setup->width * setup->height / 2;

	*planes_count = 2;

	return 0;
}

static int sunxi_isp_buf_prepare(struct vb2_buffer *buffer)
{
	printk(KERN_ERR "%s()\n", __func__);

	return 0;
}

static void sunxi_isp_buf_queue(struct vb2_buffer *buffer)
{
	struct vb2_v4l2_buffer *v4l2_buffer = to_vb2_v4l2_buffer(buffer);
	struct sunxi_isp_context *isp_ctx = vb2_get_drv_priv(buffer->vb2_queue);

	printk(KERN_ERR "%s()\n", __func__);

	v4l2_m2m_buf_queue(isp_ctx->v4l2_fh.m2m_ctx, v4l2_buffer);
}

static int sunxi_isp_start_streaming(struct vb2_queue *queue,
				     unsigned int count)
{
	printk(KERN_ERR "%s()\n", __func__);

	return 0;
}

static void sunxi_isp_stop_streaming(struct vb2_queue *queue)
{
	printk(KERN_ERR "%s()\n", __func__);
}

static const struct vb2_ops sunxi_isp_vb2_ops = {
	.queue_setup		= sunxi_isp_queue_setup,
	.buf_prepare		= sunxi_isp_buf_prepare,
	.buf_queue		= sunxi_isp_buf_queue,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
	.start_streaming	= sunxi_isp_start_streaming,
	.stop_streaming		= sunxi_isp_stop_streaming,
};

static int sunxi_isp_m2m_queue_init(void *private, struct vb2_queue *queue_src,
				    struct vb2_queue *queue_dst)
{
	struct sunxi_isp_context *isp_ctx = private;
	struct sunxi_isp_device *isp_dev = isp_ctx->dev;
	struct device *dev = isp_dev->dev;
	struct mutex *file_mutex = &isp_dev->file_mutex;
	struct vb2_queue *queue;
	int ret;

	printk(KERN_ERR "%s: private %x isp_dev %x dev %x src %x dst %x\n", __func__, private, isp_dev, dev, queue_src, queue_dst);

	queue_src->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	queue_src->io_modes = VB2_MMAP | VB2_DMABUF;
	queue_src->drv_priv = isp_ctx;
	queue_src->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	queue_src->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	queue_src->min_buffers_needed = 1;
	queue_src->ops = &sunxi_isp_vb2_ops;
	queue_src->mem_ops = &vb2_dma_contig_memops;
	queue_src->lock = file_mutex;
	queue_src->dev = dev;

	ret = vb2_queue_init(queue_src);
	if (ret)
		return ret;

	queue_dst->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	queue_dst->io_modes = VB2_MMAP | VB2_DMABUF;
	queue_dst->drv_priv = isp_ctx;
	queue_dst->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	queue_dst->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	queue_dst->min_buffers_needed = 1;
	queue_dst->ops = &sunxi_isp_vb2_ops;
	queue_dst->mem_ops = &vb2_dma_contig_memops;
	queue_dst->lock = file_mutex;
	queue_dst->dev = dev;

	return vb2_queue_init(queue_dst);
}

static void sunxi_isp_context_defaults(struct sunxi_isp_context *isp_ctx)
{
	isp_ctx->format_src = &sunxi_isp_formats_src[0];
	isp_ctx->format_dst = &sunxi_isp_formats_dst[0];

	isp_ctx->setup_src.width = 640;
	isp_ctx->setup_src.height = 480;

	isp_ctx->setup_dst.width = 640;
	isp_ctx->setup_dst.height = 480;
/*
	isp_ctx->setup_src = (struct sunxi_isp_setup) {
		.width = 640,
		.height = 480,
		.colorspace = V4L2_COLORSPACE_DEFAULT,
		.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
		.quantization = V4L2_QUANTIZATION_DEFAULT,
		.xfer_func = V4L2_XFER_FUNC_DEFAULT,
	};

	isp_ctx->setup_dst = (struct sunxi_isp_setup) {
		.width = 640,
		.height = 480,
		.colorspace = V4L2_COLORSPACE_DEFAULT,
		.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
		.quantization = V4L2_QUANTIZATION_DEFAULT,
		.xfer_func = V4L2_XFER_FUNC_DEFAULT,
	};
*/
}

static int sunxi_isp_open(struct file *file)
{
	struct sunxi_isp_device *isp_dev = video_drvdata(file);
	struct video_device *video_dev = video_devdata(file);
	struct v4l2_m2m_dev *m2m_dev = isp_dev->video.m2m_dev;
	struct mutex *file_mutex = &isp_dev->file_mutex;
	struct sunxi_isp_context *isp_ctx;
	struct v4l2_fh *v4l2_fh;
	int ret;

	if (mutex_lock_interruptible(file_mutex))
		return -ERESTARTSYS;

	isp_ctx = kzalloc(sizeof(*isp_ctx), GFP_KERNEL);
	if (!isp_ctx) {
		ret = -ENOMEM;
		goto error;
	}

	isp_ctx->dev = isp_dev;

	v4l2_fh = &isp_ctx->v4l2_fh;
	printk(KERN_ERR "%s: isp_dev %x isp_ctx %x v4l2 fh %x\n", __func__, isp_dev, isp_ctx, v4l2_fh);
	v4l2_fh_init(v4l2_fh, video_dev);
	file->private_data = v4l2_fh;
	v4l2_fh_add(v4l2_fh);

	printk(KERN_ERR "%s: isp_dev %x m2m_dev\n", __func__, isp_dev, m2m_dev);

	v4l2_fh->m2m_ctx = v4l2_m2m_ctx_init(m2m_dev, isp_ctx,
					     sunxi_isp_m2m_queue_init);
	if (IS_ERR(v4l2_fh->m2m_ctx)) {
		ret = PTR_ERR(v4l2_fh->m2m_ctx);
		goto error;
	}

	printk(KERN_ERR "%s: isp_dev %x isp_ctx %x v4l2 fh %x\n", __func__, isp_dev, isp_ctx, v4l2_fh);

	sunxi_isp_context_defaults(isp_ctx);

	ret = 0;
	goto complete;

error:
	kfree(isp_ctx);

complete:
	mutex_unlock(file_mutex);

	return ret;
}

static int sunxi_isp_release(struct file *file)
{
	struct sunxi_isp_context *isp_ctx = sunxi_isp_file_context(file);
	struct sunxi_isp_device *isp_dev = isp_ctx->dev;
	struct mutex *file_mutex = &isp_dev->file_mutex;
	struct v4l2_fh *v4l2_fh = &isp_ctx->v4l2_fh;

	printk(KERN_ERR "%s: isp_dev %x isp_ctx %x v4l2 fh %x\n", __func__, isp_dev, isp_ctx, v4l2_fh);

	mutex_lock(file_mutex);

	v4l2_fh_del(v4l2_fh);
	v4l2_m2m_ctx_release(v4l2_fh->m2m_ctx);
	v4l2_fh_exit(v4l2_fh);

	kfree(isp_ctx);

	mutex_unlock(file_mutex);

	return 0;
}

static const struct v4l2_file_operations sunxi_isp_file_ops = {
	.owner		= THIS_MODULE,
	.open		= sunxi_isp_open,
	.release	= sunxi_isp_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

/* XXX: rename video_setup */
static int sunxi_isp_v4l2_setup(struct sunxi_isp_device *isp_dev)
{
	struct video_device *video_dev = &isp_dev->video.video_dev;
	struct v4l2_device *v4l2_dev = &isp_dev->video.v4l2_dev;
	struct mutex *file_mutex = &isp_dev->file_mutex;
	struct device *dev = isp_dev->dev;
	struct v4l2_m2m_dev *m2m_dev;
	int ret;

	ret = v4l2_device_register(dev, v4l2_dev);
	if (ret) {
		dev_err(dev, "failed to register V4L2 device\n");
		return ret;
	}

	m2m_dev = v4l2_m2m_init(&sunxi_isp_m2m_ops);
	if (IS_ERR(m2m_dev)) {
		v4l2_err(v4l2_dev, "failed to initialize V4L2 M2M device\n");
		ret = PTR_ERR(m2m_dev);
		goto error_v4l2;
	}

	mutex_init(file_mutex);

	strncpy(video_dev->name, SUNXI_ISP_NAME, sizeof(video_dev->name));
	video_dev->vfl_dir = VFL_DIR_M2M;
	video_dev->fops = &sunxi_isp_file_ops;
	video_dev->ioctl_ops = &sunxi_isp_ioctl_ops;
	video_dev->minor = -1;
	video_dev->release = video_device_release_empty;
	video_dev->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	video_dev->v4l2_dev = v4l2_dev;
	video_dev->lock = file_mutex;

	video_set_drvdata(video_dev, isp_dev);

/*
	*video_dev = (struct video_device) {
		.name = SUNXI_ISP_NAME,
		.vfl_dir = VFL_DIR_M2M,
		.fops = &sunxi_isp_file_ops,
		.ioctl_ops = &sunxi_isp_ioctl_ops,
		.minor = -1,
		.release = video_device_release_empty,
		.device_caps = V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING,
		.v4l2_dev = v4l2_dev,
		.lock = file_mutex,
	};
*/
	ret = video_register_device(video_dev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(v4l2_dev, "failed to register video device\n");
		goto error_m2m;
	}

	v4l2_info(v4l2_dev, "registered %s as video%d\n", video_dev->name,
		  video_dev->num);

	isp_dev->video.m2m_dev = m2m_dev;

	printk(KERN_ERR "%s: isp_dev %x m2m_dev\n", __func__, isp_dev, m2m_dev);

	return 0;

error_m2m:
	v4l2_m2m_release(m2m_dev);
error_v4l2:
	v4l2_device_unregister(v4l2_dev);

	return ret;
}

static int sunxi_isp_v4l2_teardown(struct sunxi_isp_device *isp_dev)
{
	struct video_device *video_dev = &isp_dev->video.video_dev;
	struct v4l2_device *v4l2_dev = &isp_dev->video.v4l2_dev;
	struct v4l2_m2m_dev *m2m_dev = isp_dev->video.m2m_dev;

	v4l2_m2m_release(m2m_dev);
	video_unregister_device(video_dev);
	v4l2_device_unregister(v4l2_dev);

	return 0;
}

static const struct regmap_config sunxi_isp_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
	.max_register	= 0x400,
};

static const struct regmap_config sunxi_isp_csi_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
	.max_register	= 0x100,
};

static int sunxi_isp_probe(struct platform_device *pdev)
{
	struct sunxi_isp_device *isp_dev;
	struct device *dev = &pdev->dev;
	struct resource *res;
	void __iomem *io_base;
	int irq;
	int ret;

	isp_dev = devm_kzalloc(dev, sizeof(*isp_dev), GFP_KERNEL);
	if (!isp_dev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	isp_io = io_base;

	isp_dev->regmap = devm_regmap_init_mmio(dev, io_base,
					     &sunxi_isp_regmap_config);
	if (IS_ERR(isp_dev->regmap)) {
		dev_err(dev, "failed to init register map\n");
		return PTR_ERR(isp_dev->regmap);
	}

printk(KERN_ERR "%s: isp regmap ok\n", __func__);
	/* XXX CSI regmap */

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
printk(KERN_ERR "%s: platform get res %x\n", __func__, res);
	io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

printk(KERN_ERR "%s: go csi regmap\n", __func__);

	isp_dev->regmap_csi = devm_regmap_init_mmio(dev, io_base,
					     &sunxi_isp_csi_regmap_config);
	if (IS_ERR(isp_dev->regmap_csi)) {
		dev_err(dev, "failed to init csi register map\n");
		return PTR_ERR(isp_dev->regmap_csi);
	}

	/* XXX CSI regmap */

	isp_dev->clk_bus = devm_clk_get(dev, "bus");
	if (IS_ERR(isp_dev->clk_bus)) {
		dev_err(dev, "failed to acquire bus clock\n");
		return PTR_ERR(isp_dev->clk_bus);
	}

	isp_dev->clk_mod = devm_clk_get(dev, "mod");
	if (IS_ERR(isp_dev->clk_mod)) {
		dev_err(dev, "failed to acquire mod clock\n");
		return PTR_ERR(isp_dev->clk_mod);
	}

	isp_dev->clk_ram = devm_clk_get(dev, "ram");
	if (IS_ERR(isp_dev->clk_ram)) {
		dev_err(dev, "failed to acquire ram clock\n");
		return PTR_ERR(isp_dev->clk_ram);
	}

	isp_dev->clk_isp = devm_clk_get(dev, "isp");
	if (IS_ERR(isp_dev->clk_isp)) {
		dev_err(dev, "failed to acquire isp clock\n");
		return PTR_ERR(isp_dev->clk_isp);
	}

	isp_dev->clk_mipi = devm_clk_get(dev, "mipi");
	if (IS_ERR(isp_dev->clk_mipi)) {
		dev_err(dev, "failed to acquire mipi clock\n");
		return PTR_ERR(isp_dev->clk_mipi);
	}

	isp_dev->clk_misc = devm_clk_get(dev, "misc");
	if (IS_ERR(isp_dev->clk_misc)) {
		dev_err(dev, "failed to acquire mipi clock\n");
		return PTR_ERR(isp_dev->clk_misc);
	}

	isp_dev->reset = devm_reset_control_get_shared(dev, NULL);
	if (IS_ERR(isp_dev->reset)) {
		dev_err(dev, "failed to get reset controller\n");
		return PTR_ERR(isp_dev->reset);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENXIO;

	ret = devm_request_irq(dev, irq, sunxi_isp_interrupt, IRQF_SHARED,
			       SUNXI_ISP_NAME, isp_dev);
	if (ret) {
		dev_err(dev, "failed to request interrupt\n");
		return ret;
	}

	reset_control_deassert(isp_dev->reset);
	clk_prepare_enable(isp_dev->clk_bus);
	clk_prepare_enable(isp_dev->clk_mod);
	clk_prepare_enable(isp_dev->clk_ram);
	clk_prepare_enable(isp_dev->clk_isp);
	clk_prepare_enable(isp_dev->clk_mipi);
	clk_prepare_enable(isp_dev->clk_misc);

	isp_dev->dev = dev;

	ret = sunxi_sram_claim(dev);
	if (ret) {
		dev_err(dev, "Failed to claim SRAM\n");
		return ret;
	}

	ret = sunxi_isp_v4l2_setup(isp_dev);
	if (ret) {
		dev_err(dev, "failed to setup V4L2\n");
		return ret;
	}

	ret = sunxi_isp_memory_setup(isp_dev);
	if (ret) {
		dev_err(dev, "failed to setup memory\n");
		return ret;
	}

	platform_set_drvdata(pdev, isp_dev);

//	test(isp_dev);

	return 0;
}

static int sunxi_isp_remove(struct platform_device *pdev)
{
	struct sunxi_isp_device *isp_dev = platform_get_drvdata(pdev);

	sunxi_isp_v4l2_teardown(isp_dev);

	return 0;
}

static const struct of_device_id sunxi_isp_of_match[] = {
	{ .compatible = "allwinner,sun6i-a31-isp" },
	{},
};
MODULE_DEVICE_TABLE(of, sunxi_isp_of_match);

static struct platform_driver sunxi_isp_platform_driver = {
	.probe = sunxi_isp_probe,
	.remove = sunxi_isp_remove,
	.driver = {
		.name = SUNXI_ISP_NAME,
		.of_match_table = of_match_ptr(sunxi_isp_of_match),
	},
};
module_platform_driver(sunxi_isp_platform_driver);

MODULE_DESCRIPTION("Allwinner Image Signal Processor (ISP) Driver");
MODULE_AUTHOR("Paul Kocialkowski <paul.kocialkowski@bootlin.com>");
MODULE_LICENSE("GPL");
