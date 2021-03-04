/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2020 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef __SUNXI_ISP_H__
#define __SUNXI_ISP_H__

#define SUNXI_ISP_SRC_MODE_DRAM			0
#define SUNXI_ISP_SRC_MODE_CSI(n)		(1 + (n))

#define SUNXI_ISP_FE_CFG_REG			0x0000
#define SUNXI_ISP_FE_CFG_EN			BIT(0)
#define SUNXI_ISP_FE_CFG_SRC0_MODE(v)		(((v) << 8) & GENMASK(9, 8))
#define SUNXI_ISP_FE_CFG_SRC1_MODE(v)		(((v) << 16) & GENMASK(17, 16))

#define SUNXI_ISP_FE_CTRL_REG			0x0004
#define SUNXI_ISP_FE_CTRL_SCAP_EN		BIT(0)
#define SUNXI_ISP_FE_CTRL_VCAP_EN		BIT(1)
#define SUNXI_ISP_FE_CTRL_PARA_READY		BIT(2)
#define SUNXI_ISP_FE_CTRL_LUT_UPDATE		BIT(3)
#define SUNXI_ISP_FE_CTRL_LENS_UPDATE		BIT(4)
#define SUNXI_ISP_FE_CTRL_GAMMA_UPDATE		BIT(5)
#define SUNXI_ISP_FE_CTRL_DRC_UPDATE		BIT(6)
#define SUNXI_ISP_FE_CTRL_DISC_UPDATE		BIT(7)
#define SUNXI_ISP_FE_CTRL_OUTPUT_SPEED_CTRL(v)	(((v) << 16) & GENMASK(17, 16))
#define SUNXI_ISP_FE_CTRL_VCAP_READ_START	BIT(31)

#define SUNXI_ISP_FE_INT_EN_REG			0x0008
#define SUNXI_ISP_FE_INT_EN_FINISH		BIT(0)
#define SUNXI_ISP_FE_INT_EN_START		BIT(1)
#define SUNXI_ISP_FE_INT_EN_PARA_SAVE		BIT(2)
#define SUNXI_ISP_FE_INT_EN_PARA_LOAD		BIT(3)
#define SUNXI_ISP_FE_INT_EN_SRC0_FIFO		BIT(4)
#define SUNXI_ISP_FE_INT_EN_SRC1_FIFO		BIT(5)
#define SUNXI_ISP_FE_INT_EN_ROT_FINISH		BIT(6)
#define SUNXI_ISP_FE_INT_EN_LINE_NUM_START	BIT(7)

#define SUNXI_ISP_FE_INT_STA_REG		0x000c
#define SUNXI_ISP_FE_INT_STA_FINISH		BIT(0)
#define SUNXI_ISP_FE_INT_STA_START		BIT(1)
#define SUNXI_ISP_FE_INT_STA_PARA_SAVE		BIT(2)
#define SUNXI_ISP_FE_INT_STA_PARA_LOAD		BIT(3)
#define SUNXI_ISP_FE_INT_STA_SRC0_FIFO		BIT(4)
#define SUNXI_ISP_FE_INT_STA_SRC1_FIFO		BIT(5)
#define SUNXI_ISP_FE_INT_STA_ROT_FINISH		BIT(6)
#define SUNXI_ISP_FE_INT_STA_LINE_NUM_START	BIT(7)

#define SUNXI_ISP_FE_INT_LINE_NUM_REG		0x0018
#define SUNXI_ISP_FE_ROT_OF_CFG_REG		0x001c

#define SUNXI_ISP_REG_LOAD_ADDR_REG		0x0020
#define SUNXI_ISP_REG_SAVE_ADDR_REG		0x0024

#define SUNXI_ISP_LUT_TABLE_ADDR_REG		0x0028
#define SUNXI_ISP_DRC_TABLE_ADDR_REG		0x002c
#define SUNXI_ISP_STATS_ADDR_REG		0x0030

#define SUNXI_ISP_SRAM_RW_OFFSET_REG		0x0038
#define SUNXI_ISP_SRAM_RW_DATA_REG		0x003c

#define SUNXI_ISP_MODULE_EN_REG			0x0040
#define SUNXI_ISP_MODULE_EN_AE			BIT(0)
#define SUNXI_ISP_MODULE_EN_OBC			BIT(1)
#define SUNXI_ISP_MODULE_EN_DPC_LUT		BIT(2)
#define SUNXI_ISP_MODULE_EN_DPC_OTF		BIT(3)
#define SUNXI_ISP_MODULE_EN_BDNF		BIT(4)
#define SUNXI_ISP_MODULE_EN_AWB			BIT(6)
#define SUNXI_ISP_MODULE_EN_WB			BIT(7)
#define SUNXI_ISP_MODULE_EN_LSC			BIT(8)
#define SUNXI_ISP_MODULE_EN_BGC			BIT(9)
#define SUNXI_ISP_MODULE_EN_SAP			BIT(10)
#define SUNXI_ISP_MODULE_EN_AF			BIT(11)
#define SUNXI_ISP_MODULE_EN_RGB2RGB		BIT(12)
#define SUNXI_ISP_MODULE_EN_RGB_DRC		BIT(13)
#define SUNXI_ISP_MODULE_EN_TDNF		BIT(15)
#define SUNXI_ISP_MODULE_EN_AFS			BIT(16)
#define SUNXI_ISP_MODULE_EN_HIST		BIT(17)
#define SUNXI_ISP_MODULE_EN_YUV_GAIN_OFFSET	BIT(18)
#define SUNXI_ISP_MODULE_EN_YUV_DRC		BIT(19)
#define SUNXI_ISP_MODULE_EN_TG			BIT(20)
#define SUNXI_ISP_MODULE_EN_ROT			BIT(21)
#define SUNXI_ISP_MODULE_EN_CONTRAST		BIT(22)
#define SUNXI_ISP_MODULE_EN_SATU		BIT(24)
#define SUNXI_ISP_MODULE_EN_SRC1		BIT(30)
#define SUNXI_ISP_MODULE_EN_SRC0		BIT(31)

#define SUNXI_ISP_INPUT_FMT_YUV420		0
#define SUNXI_ISP_INPUT_FMT_YUV422		1
#define SUNXI_ISP_INPUT_FMT_RAW_BGGR		4
#define SUNXI_ISP_INPUT_FMT_RAW_RGGB		5
#define SUNXI_ISP_INPUT_FMT_RAW_GBRG		6
#define SUNXI_ISP_INPUT_FMT_RAW_GRBG		7

#define SUNXI_ISP_INPUT_YUV_SEQ_YUYV		0
#define SUNXI_ISP_INPUT_YUV_SEQ_YVYU		1
#define SUNXI_ISP_INPUT_YUV_SEQ_UYVY		2
#define SUNXI_ISP_INPUT_YUV_SEQ_VYUY		3

#define SUNXI_ISP_MODE_REG			0x0044
#define SUNXI_ISP_MODE_INPUT_FMT(v)		((v) & GENMASK(2, 0))
#define SUNXI_ISP_MODE_INPUT_YUV_SEQ(v)		(((v) << 3) & GENMASK(4, 3))
#define SUNXI_ISP_MODE_OTF_DPC(v)		(((v) << 16) & BIT(16))
#define SUNXI_ISP_MODE_SHARP(v)			(((v) << 17) & BIT(17))
#define SUNXI_ISP_MODE_HIST(v)			(((v) << 20) & GENMASK(21, 20))

#define SUNXI_ISP_IN_CFG_REG			0x0048
#define SUNXI_ISP_IN_CFG_STRIDE_DIV16(v)	((v) & GENMASK(10, 0))

#define SUNXI_ISP_IN_LUMA_RGB_ADDR0_REG		0x004c
#define SUNXI_ISP_IN_CHROMA_ADDR0_REG		0x0050
#define SUNXI_ISP_IN_LUMA_RGB_ADDR1_REG		0x0054
#define SUNXI_ISP_IN_CHROMA_ADDR1_REG		0x0058

#define SUNXI_ISP_AE_CFG_REG			0x0060
#define SUNXI_ISP_AE_CFG_LOW_BRI_TH(v)		((v) & GENMASK(11, 0))
#define SUNXI_ISP_AE_CFG_HORZ_NUM(v)		(((v) << 12) & GENMASK(15, 12))
#define SUNXI_ISP_AE_CFG_HIGH_BRI_TH(v)		(((v) << 16) & GENMASK(27, 16))
#define SUNXI_ISP_AE_CFG_VERT_NUM(v)		(((v) << 28) & GENMASK(31, 28))

#define SUNXI_ISP_AE_SIZE_REG			0x0064
#define SUNXI_ISP_AE_SIZE_WIDTH(v)		((v) & GENMASK(10, 0))
#define SUNXI_ISP_AE_SIZE_HEIGHT(v)		(((v) << 16) & GENMASK(26, 16))

#define SUNXI_ISP_AE_POS_REG			0x0068
#define SUNXI_ISP_AE_POS_HORZ_START(v)		((v) & GENMASK(10, 0))
#define SUNXI_ISP_AE_POS_VERT_START(v)		(((v) << 16) & GENMASK(26, 16))

#define SUNXI_ISP_OB_SIZE_REG			0x0078
#define SUNXI_ISP_OB_SIZE_WIDTH(v)		((v) & GENMASK(13, 0))
#define SUNXI_ISP_OB_SIZE_HEIGHT(v)		(((v) << 16) & GENMASK(29, 16))

#define SUNXI_ISP_OB_VALID_REG			0x007c
#define SUNXI_ISP_OB_VALID_WIDTH(v)		((v) & GENMASK(12, 0))
#define SUNXI_ISP_OB_VALID_HEIGHT(v)		(((v) << 16) & GENMASK(28, 16))

#define SUNXI_ISP_OB_SRC0_VALID_START_REG	0x0080
#define SUNXI_ISP_OB_SRC0_VALID_START_HORZ(v)	((v) & GENMASK(11, 0))
#define SUNXI_ISP_OB_SRC0_VALID_START_VERT(v)	(((v) << 16) & GENMASK(27, 16))

#define SUNXI_ISP_OB_SRC1_VALID_START_REG	0x0084
#define SUNXI_ISP_OB_SRC1_VALID_START_HORZ(v)	((v) & GENMASK(11, 0))
#define SUNXI_ISP_OB_SRC1_VALID_START_VERT(v)	(((v) << 16) & GENMASK(27, 16))

#define SUNXI_ISP_OB_SPRITE_REG			0x0088
#define SUNXI_ISP_OB_SPRITE_WIDTH(v)		((v) & GENMASK(12, 0))
#define SUNXI_ISP_OB_SPRITE_HEIGHT(v)		(((v) << 16) & GENMASK(28, 16))

#define SUNXI_ISP_OB_SPRITE_START_REG		0x008c
#define SUNXI_ISP_OB_SPRITE_START_HORZ(v)	((v) & GENMASK(11, 0))
#define SUNXI_ISP_OB_SPRITE_START_VERT(v)	(((v) << 16) & GENMASK(27, 16))

#define SUNXI_ISP_OB_CFG_REG			0x0090
#define SUNXI_ISP_OB_HORZ_POS_REG		0x0094
#define SUNXI_ISP_OB_VERT_PARA_REG		0x0098
#define SUNXI_ISP_OB_OFFSET_FIXED_REG		0x009c

/* XXX: below might be different between platforms */

#define SUNXI_ISP_MCH_SIZE_CFG_REG		0x01e0
#define SUNXI_ISP_MCH_SIZE_CFG_WIDTH(v)		((v) & GENMASK(12, 0))
#define SUNXI_ISP_MCH_SIZE_CFG_HEIGHT(v)	(((v) << 16) & GENMASK(28, 16))

#define SUNXI_ISP_MCH_SCALE_CFG_REG		0x01e4
#define SUNXI_ISP_MCH_SCALE_CFG_X_RATIO(v)	((v) & GENMASK(11, 0))
#define SUNXI_ISP_MCH_SCALE_CFG_Y_RATIO(v)	(((v) << 16) & GENMASK(27, 16))
#define SUNXI_ISP_MCH_SCALE_CFG_WEIGHT_SHIFT(v)	(((v) << 28) & GENMASK(31, 28))

#define SUNXI_ISP_SCH_SIZE_CFG_REG		0x01e8
#define SUNXI_ISP_SCH_SIZE_CFG_WIDTH(v)		((v) & GENMASK(12, 0))
#define SUNXI_ISP_SCH_SIZE_CFG_HEIGHT(v)	(((v) << 16) & GENMASK(28, 16))

#define SUNXI_ISP_SCH_SCALE_CFG_REG		0x01ec
#define SUNXI_ISP_SCH_SCALE_CFG_X_RATIO(v)	((v) & GENMASK(11, 0))
#define SUNXI_ISP_SCH_SCALE_CFG_Y_RATIO(v)	(((v) << 16) & GENMASK(27, 16))
#define SUNXI_ISP_SCH_SCALE_CFG_WEIGHT_SHIFT(v)	(((v) << 28) & GENMASK(31, 28))

#define SUNXI_ISP_MCH_CFG_REG			0x01f0
#define SUNXI_ISP_MCH_CFG_EN			BIT(0)
#define SUNXI_ISP_MCH_CFG_SCALE_EN		BIT(1)
#define SUNXI_ISP_MCH_CFG_MODE(v)		(((v) << 2) & GENMASK(4, 2))
#define SUNXI_ISP_MCH_CFG_MIRROR_EN		BIT(5)
#define SUNXI_ISP_MCH_CFG_FLIP_EN		BIT(6)
#define SUNXI_ISP_MCH_CFG_STRIDE_Y_DIV4(v)	(((v) << 8) & GENMASK(18, 8))
#define SUNXI_ISP_MCH_CFG_STRIDE_UV_DIV4(v)	(((v) << 20) & GENMASK(30, 20))

#define SUNXI_ISP_SCH_CFG_REG			0x01f4

#define SUNXI_ISP_MCH_Y_ADDR0_REG		0x01f8
#define SUNXI_ISP_MCH_U_ADDR0_REG		0x01fc
#define SUNXI_ISP_MCH_V_ADDR0_REG		0x0200
#define SUNXI_ISP_MCH_Y_ADDR1_REG		0x0204
#define SUNXI_ISP_MCH_U_ADDR1_REG		0x0208
#define SUNXI_ISP_MCH_V_ADDR1_REG		0x020c
#define SUNXI_ISP_SCH_Y_ADDR0_REG		0x0210
#define SUNXI_ISP_SCH_U_ADDR0_REG		0x0214
#define SUNXI_ISP_SCH_V_ADDR0_REG		0x0218
#define SUNXI_ISP_SCH_Y_ADDR1_REG		0x021c
#define SUNXI_ISP_SCH_U_ADDR1_REG		0x0220
#define SUNXI_ISP_SCH_V_ADDR1_REG		0x0224

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
	struct device *dev;

	struct regmap *regmap;
	struct regmap *regmap_csi;
	struct clk *clk_bus;
	struct clk *clk_mod;
	struct clk *clk_ram;

	struct clk *clk_isp;
	struct clk *clk_mipi;
	struct clk *clk_misc;

	struct reset_control *reset;

	/* XXX: move under video */
	struct mutex file_mutex;

	struct sunxi_isp_video video;
	struct sunxi_isp_memory memory;

	void *io;
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
