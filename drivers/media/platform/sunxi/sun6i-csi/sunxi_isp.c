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

#include "blob/bsp_isp.h"

static void test(struct sunxi_isp_device *isp_dev)
{
//	struct regmap *regmap = isp_dev->regmap;
	u32 version = 0;
	u32 reg = 0;
	unsigned int max = 0x240;
	unsigned int i = 0;
//	u8 bits = GENMASK(7,1) | GENMASK(15,10) | GENMASK(31,18);

	printk(KERN_ERR "-- ISP reg dump --\n");

	for (i = 0; i < max; i += 4) {
//		reg = 0;
		reg = readl(isp_dev->io + i);
//		regmap_read(regmap, i, &reg);
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

static void sunxi_isp_write(struct sunxi_isp_device *isp_dev, u32 offset,
			    u32 value)
{
	struct sunxi_isp_memory *memory = &isp_dev->memory;
	u32 *reg = (u32 *)(memory->reg_load + offset);

	*reg = value;
}

irqreturn_t sunxi_isp_isr(struct sunxi_isp_device *isp_dev)
{
	struct sunxi_isp_memory *memory = &isp_dev->memory;

	u32 status = readl(isp_dev->io + SUNXI_ISP_FE_INT_STA_REG);
	printk(KERN_ERR "%s: status = %#x\n", __func__, status);

	if (!status)
		return IRQ_NONE;

	writel(status, isp_dev->io + SUNXI_ISP_FE_INT_STA_REG);
/*
	test(isp_dev);
	dump(memory->reg_load, "LOAD");
	dump(memory->reg_save, "SAVE");
*/
	return IRQ_HANDLED;
}

void sunxi_isp_run(struct sunxi_isp_device *isp_dev, dma_addr_t addr)
{
	struct sunxi_isp_memory *memory = &isp_dev->memory;
	struct regmap *regmap = isp_dev->regmap;
	dma_addr_t dma_addr_luma_dst, dma_addr_chroma_dst;
	unsigned int width, height;
	u32 value;

	width = 640;
	height = 480;

	printk(KERN_ERR "%s: output dma addr %#x\n", __func__, addr);

	/* XXX: looks like that did it! */
	/* find out exactly what */
//	memcpy(memory->reg_load, isp_dev->io, 0x240);
	memcpy(memory->reg_load, isp_dev->io, 0x160);

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
/*
	value = SUNXI_ISP_MODULE_EN_SRC0 |
		SUNXI_ISP_MODULE_EN_AE |
		SUNXI_ISP_MODULE_EN_HIST |
		SUNXI_ISP_MODULE_EN_AFS |
		SUNXI_ISP_MODULE_EN_RGB2RGB |
		SUNXI_ISP_MODULE_EN_BDNF |
		SUNXI_ISP_MODULE_EN_LSC |
		SUNXI_ISP_MODULE_EN_SATU |
		SUNXI_ISP_MODULE_EN_SAP |
		SUNXI_ISP_MODULE_EN_RGB_DRC;
*/
	value = SUNXI_ISP_MODULE_EN_SRC0;
	sunxi_isp_write(isp_dev, SUNXI_ISP_MODULE_EN_REG, value);

	/* FIXME: remove PHYS_BASE or what? */

	dma_addr_luma_dst = addr;
	dma_addr_chroma_dst = addr + width * height;
/*
	dma_addr_luma_src -= 0x40000000;
	dma_addr_chroma_src -= 0x40000000;
	dma_addr_luma_dst -= 0x40000000;
	dma_addr_chroma_dst -= 0x40000000;

	printk(KERN_ERR "%s: src %#x/%#x -> dst %#x/%#x\n", __func__,
	       dma_addr_luma_src, dma_addr_chroma_src, dma_addr_luma_dst,
	       dma_addr_chroma_dst);
*/

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
	sunxi_isp_write(isp_dev, SUNXI_ISP_OB_SPRITE_REG, value);

	/* Bayer offset/gain */

	sunxi_isp_write(isp_dev, 0xe0, 0x200020);
	sunxi_isp_write(isp_dev, 0xe4, 0x200020);
	sunxi_isp_write(isp_dev, 0xe8, 0x1000100);
	sunxi_isp_write(isp_dev, 0xec, 0x100);

	/* Mode */

	/* BGGR */
	value = SUNXI_ISP_MODE_INPUT_FMT(SUNXI_ISP_INPUT_SEQ_RGGB) |
		SUNXI_ISP_MODE_INPUT_YUV_SEQ(0) |
		SUNXI_ISP_MODE_SHARP(1) |
		SUNXI_ISP_MODE_HIST(2);
	sunxi_isp_write(isp_dev, SUNXI_ISP_MODE_REG, value);

	/* SRC0 Input */

/*
	sunxi_isp_write(isp_dev, SUNXI_ISP_IN_CFG_REG,
			SUNXI_ISP_IN_CFG_STRIDE_DIV16(width / 16));
	sunxi_isp_write(isp_dev, SUNXI_ISP_IN_LUMA_RGB_ADDR0_REG,
		     dma_addr_luma_src >> 2);
*/

	/* MCH Output */

	value = SUNXI_ISP_MCH_SIZE_CFG_WIDTH(width) |
		SUNXI_ISP_MCH_SIZE_CFG_HEIGHT(height);
	sunxi_isp_write(isp_dev, SUNXI_ISP_MCH_SIZE_CFG_REG, value);

	// bsp: 0x11000100
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

	sunxi_isp_write(isp_dev, SUNXI_ISP_MCH_Y_ADDR0_REG,
			dma_addr_luma_dst >> 2);
	sunxi_isp_write(isp_dev, SUNXI_ISP_MCH_U_ADDR0_REG,
			dma_addr_chroma_dst >> 2);

	/* Frontend Config */

	value = SUNXI_ISP_FE_CFG_EN |
		SUNXI_ISP_FE_CFG_SRC0_MODE(SUNXI_ISP_SRC_MODE_CSI(0));
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

//	test(isp_dev);

//	memcpy(reg_load, isp_io, 0x240);
//	memcpy(reg_save, isp_io, 0x240);

	test(isp_dev);
	dump(memory->reg_load, "LOAD");

	/* Frontend Control */

	// XXX: try to remove para ready
	regmap_read(regmap, SUNXI_ISP_FE_CTRL_REG, &value);
	value |= SUNXI_ISP_FE_CTRL_VCAP_EN;
	regmap_write(regmap, SUNXI_ISP_FE_CTRL_REG, value);

/*
	value = SUNXI_ISP_FE_CTRL_VCAP_EN |
		SUNXI_ISP_FE_CTRL_OUTPUT_SPEED_CTRL(1);
//	*((u32 *) reg_load + SUNXI_ISP_FE_CTRL_REG) = value;
	regmap_write(regmap, SUNXI_ISP_FE_CTRL_REG, value);
*/

/*
	while (steps--) {
		u32 status = 0;

		mdelay(100);

		regmap_read(regmap, SUNXI_ISP_FE_INT_STA_REG, &status);
		printk(KERN_ERR "%s: IRQ status %#x\n", __func__, status);
	}
*/
}

void sunxi_isp_run_blob(struct sunxi_isp_device *isp_dev, dma_addr_t addr)
{
	struct sunxi_isp_memory *memory = &isp_dev->memory;
	dma_addr_t dma_addr_luma_src, dma_addr_chroma_src;
	dma_addr_t dma_addr_luma_dst, dma_addr_chroma_dst;
	enum pixel_fmt isp_fmt[ISP_MAX_CH_NUM] = { 0 };
	struct isp_size isp_size[ISP_MAX_CH_NUM] = { 0 };
	struct isp_size_settings size_settings = { 0 };
	struct isp_size ob_black_size = { 0 };
	struct isp_size ob_valid_size = { 0 };
	struct coor ob_start = { 0 };
	struct isp_init_para isp_init_para = { 0 };
	unsigned int size;
	u32 value;

	dma_addr_luma_dst = addr;
	dma_addr_chroma_dst = addr + 640 * 480;
/*
	dma_addr_luma_src -= 0x40000000;
	dma_addr_chroma_src -= 0x40000000;
	dma_addr_luma_dst -= 0x40000000;
	dma_addr_chroma_dst -= 0x40000000;
*/

	bsp_isp_init_platform(ISP_PLATFORM_SUN8IW8P1);
	bsp_isp_set_base_addr((unsigned int)isp_dev->io);
	bsp_isp_set_map_load_addr((unsigned int)memory->reg_load);
	bsp_isp_set_map_saved_addr((unsigned int)memory->reg_save);

	bsp_isp_set_dma_load_addr((unsigned int)memory->reg_load_dma);
	bsp_isp_set_dma_saved_addr((unsigned int)memory->reg_save_dma);

	isp_fmt[MAIN_CH] = PIX_FMT_YUV420SP_8;
	isp_fmt[SUB_CH] = PIX_FMT_NONE;
	isp_fmt[ROT_CH] = PIX_FMT_NONE;

	bsp_isp_set_fmt(BUS_FMT_SRGGB, isp_fmt);

	bsp_isp_set_rot(MAIN_CH,ANGLE_0);

	isp_size[MAIN_CH].width = 640;
	isp_size[MAIN_CH].height = 480;

	isp_size[SUB_CH].width = 0;
	isp_size[SUB_CH].height = 0;

	isp_size[ROT_CH].width = 0;
	isp_size[ROT_CH].height = 0;

	ob_black_size.width = 640;
	ob_black_size.height = 480;
	ob_valid_size.width = 640;
	ob_valid_size.height = 480;
	ob_start.hor = 0;
	ob_start.ver = 0;

	size_settings.full_size = isp_size[MAIN_CH];
	size_settings.scale_size = isp_size[SUB_CH];
	size_settings.ob_black_size = ob_black_size;
	size_settings.ob_start = ob_start;
	size_settings.ob_valid_size = ob_valid_size;
	size_settings.ob_rot_size = isp_size[ROT_CH];

	size = bsp_isp_set_size(isp_fmt, &size_settings);
	printk(KERN_ERR "bsp_isp_set_size gives %u\n", size);

	bsp_isp_enable();

	isp_init_para.isp_src_ch_mode = ISP_SINGLE_CH;
	isp_init_para.isp_src_ch_en[0] = 1;

	bsp_isp_init(&isp_init_para);

	bsp_isp_set_output_addr(dma_addr_luma_dst);

	bsp_isp_set_statistics_addr(memory->stat_dma);
	bsp_isp_set_para_ready();
	bsp_isp_clr_irq_status(ISP_IRQ_EN_ALL);
	bsp_isp_irq_enable(START_INT_EN | FINISH_INT_EN | SRC0_FIFO_INT_EN);

/*
	writel(SUNXI_ISP_IN_CFG_STRIDE_DIV16(640 / 16), isp_dev->io + SUNXI_ISP_IN_CFG_REG);
	sunxi_isp_write(isp_dev, SUNXI_ISP_IN_CFG_REG,
			SUNXI_ISP_IN_CFG_STRIDE_DIV16(640 / 16));

	writel(SUNXI_ISP_FE_CFG_EN, isp_dev->io + SUNXI_ISP_FE_CFG_REG);

	writel((addr - 0x258000) >> 2, isp_dev->io + SUNXI_ISP_IN_LUMA_RGB_ADDR0_REG);
	sunxi_isp_write(isp_dev, SUNXI_ISP_IN_LUMA_RGB_ADDR0_REG, (addr - 0x258000) >> 2);
	writel((addr - 0x258000 + 640 * 480) >> 2, isp_dev->io + SUNXI_ISP_IN_CHROMA_ADDR0_REG);
	sunxi_isp_write(isp_dev, SUNXI_ISP_IN_CHROMA_ADDR0_REG, (addr - 0x258000 + 640 * 480) >> 2);
*/

	test(isp_dev);
	dump(memory->reg_load, "LOAD");

//	bsp_isp_image_capture_start();
	bsp_isp_video_capture_start();
}

int sunxi_isp_memory_setup(struct sunxi_isp_device *isp_dev)
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
