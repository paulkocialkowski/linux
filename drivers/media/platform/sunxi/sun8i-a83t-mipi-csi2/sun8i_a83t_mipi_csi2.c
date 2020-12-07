// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020 Kévin L'hôpital <kevin.lhopital@bootlin.com>
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
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#include "sun8i_a83t_dphy.h"
#include "sun8i_a83t_mipi_csi2.h"

#define MODULE_NAME	"sun8i-a83t-mipi-csi2"

static const u32 sun8i_a83t_mipi_csi2_mbus_codes[] = {
	MEDIA_BUS_FMT_SBGGR8_1X8,
	MEDIA_BUS_FMT_SGBRG8_1X8,
	MEDIA_BUS_FMT_SGRBG8_1X8,
	MEDIA_BUS_FMT_SRGGB8_1X8,
	MEDIA_BUS_FMT_SBGGR10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SRGGB10_1X10,
};

/* Core */

static void sun8i_a83t_mipi_csi2_init(struct sun8i_a83t_mipi_csi2_dev *cdev)
{
	struct regmap *regmap = cdev->regmap;

	/*
	 * The Allwinner BSP sets various magic values on a bunch of registers.
	 * This is apparently a necessary initialization process that will cause
	 * the capture to fail with unsolicited interrupts hitting if skipped.
	 *
	 * Most of the registers are set to proper values later, except for the
	 * two reserved registers. They are said to hold a "hardware lock"
	 * value, without more information available.
	 */

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_CTRL_REG, 0);
	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_CTRL_REG,
		     SUN8I_A83T_MIPI_CSI2_CTRL_INIT_VALUE);

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_RX_PKT_NUM_REG, 0);
	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_RX_PKT_NUM_REG,
		     SUN8I_A83T_MIPI_CSI2_RX_PKT_NUM_INIT_VALUE);

	regmap_write(regmap, SUN8I_A83T_DPHY_CTRL_REG, 0);
	regmap_write(regmap, SUN8I_A83T_DPHY_CTRL_REG,
		     SUN8I_A83T_DPHY_CTRL_INIT_VALUE);

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_RSVD1_REG, 0);
	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_RSVD1_REG,
		     SUN8I_A83T_MIPI_CSI2_RSVD1_HW_LOCK_VALUE);

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_RSVD2_REG, 0);
	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_RSVD2_REG,
		     SUN8I_A83T_MIPI_CSI2_RSVD2_HW_LOCK_VALUE);

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_CFG_REG, 0);
	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_CFG_REG,
		     SUN8I_A83T_MIPI_CSI2_CFG_INIT_VALUE);
}

/* Video */

static int sun8i_a83t_mipi_csi2_s_stream(struct v4l2_subdev *subdev, int on)
{
	struct sun8i_a83t_mipi_csi2_video *video =
		sun8i_a83t_mipi_csi2_subdev_video(subdev);
	struct sun8i_a83t_mipi_csi2_dev *cdev =
		sun8i_a83t_mipi_csi2_video_dev(video);
	struct v4l2_subdev *remote_subdev = video->remote_subdev;
	struct v4l2_fwnode_bus_mipi_csi2 *bus_mipi_csi2 =
		&video->endpoint.bus.mipi_csi2;
	union phy_configure_opts dphy_opts = { 0 };
	struct phy_configure_opts_mipi_dphy *dphy_cfg = &dphy_opts.mipi_dphy;
	struct regmap *regmap = cdev->regmap;
	struct v4l2_ctrl *ctrl;
	unsigned int lanes_count;
	unsigned int bpp;
	unsigned long pixel_rate;
	u8 data_type = 0;
	u32 version = 0;
	/* Initialize to 0 to use both in disable label (ret != 0) and off. */
	int ret = 0;

	if (!remote_subdev)
		return -ENODEV;

	if (!on) {
		v4l2_subdev_call(remote_subdev, video, s_stream, 0);
		goto disable;
	}

	switch (video->mbus_format.code) {
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		data_type = MIPI_CSI2_DATA_TYPE_RAW8;
		bpp = 8;
		break;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		data_type = MIPI_CSI2_DATA_TYPE_RAW10;
		bpp = 10;
		break;
	default:
		return -EINVAL;
	}

	/* Sensor pixel rate */

	ctrl = v4l2_ctrl_find(remote_subdev->ctrl_handler, V4L2_CID_PIXEL_RATE);
	if (!ctrl) {
		dev_err(cdev->dev,
			"%s: no MIPI CSI-2 pixel rate from the sensor\n",
			__func__);
		return -ENODEV;
	}

	pixel_rate = (unsigned long)v4l2_ctrl_g_ctrl_int64(ctrl);
	if (!pixel_rate) {
		dev_err(cdev->dev,
			"%s: zero MIPI CSI-2 pixel rate from the sensor\n",
			__func__);
		return -ENODEV;
	}

	/* Power management */

	ret = pm_runtime_get_sync(cdev->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(cdev->dev);
		return ret;
	}

	/* D-PHY configuration */

	lanes_count = bus_mipi_csi2->num_data_lanes;
	phy_mipi_dphy_get_default_config(pixel_rate, bpp, lanes_count,
					 dphy_cfg);

	/*
	 * Note that our hardware is using DDR, which is not taken in account by
	 * phy_mipi_dphy_get_default_config when calculating hs_clk_rate from
	 * the pixel rate, lanes count and bpp.
	 *
	 * The resulting clock rate is basically the symbol rate over the whole
	 * link. The actual clock rate is calculated with division by two since
	 * DDR samples both on rising and falling edges.
	 */

	dev_dbg(cdev->dev, "A83T MIPI CSI-2 config:\n");
	dev_dbg(cdev->dev,
		"%ld pixels/s, %u bits/pixel, %u lanes, %lu Hz clock\n",
		pixel_rate, bpp, lanes_count, dphy_cfg->hs_clk_rate / 2);

	ret = phy_reset(cdev->dphy);
	if (ret) {
		dev_err(cdev->dev, "failed to reset MIPI D-PHY\n");
		goto error_pm;
	}

	ret = phy_set_mode_ext(cdev->dphy, PHY_MODE_MIPI_DPHY,
			       PHY_MIPI_DPHY_SUBMODE_RX);
	if (ret) {
		dev_err(cdev->dev, "failed to set MIPI D-PHY mode\n");
		goto error_pm;
	}

	ret = phy_configure(cdev->dphy, &dphy_opts);
	if (ret) {
		dev_err(cdev->dev, "failed to configure MIPI D-PHY\n");
		goto error_pm;
	}

	ret = phy_power_on(cdev->dphy);
	if (ret) {
		dev_err(cdev->dev, "failed to power on MIPI D-PHY\n");
		goto error_pm;
	}

	/* MIPI CSI-2 controller setup */

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_CTRL_REG,
		     SUN8I_A83T_MIPI_CSI2_CTRL_RESET_N);

	regmap_read(regmap, SUN8I_A83T_MIPI_CSI2_VERSION_REG, &version);

	dev_dbg(cdev->dev, "A83T MIPI CSI-2 version: %04x\n", version);

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_CFG_REG,
		     SUN8I_A83T_MIPI_CSI2_CFG_UNPKT_EN |
		     SUN8I_A83T_MIPI_CSI2_CFG_SYNC_DLY_CYCLE(8) |
		     SUN8I_A83T_MIPI_CSI2_CFG_N_CHANNEL(1) |
		     SUN8I_A83T_MIPI_CSI2_CFG_N_LANE(lanes_count));

	/*
	 * Our MIPI CSI-2 controller has internal channels that can be
	 * configured to match a specific MIPI CSI-2 virtual channel and/or
	 * a specific data type. Each internal channel can be piped to an
	 * internal channel of the CSI controller.
	 *
	 * We set virtual channel numbers to all channels to make sure that
	 * virtual channel 0 goes to CSI channel 0 only.
	 */
	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_VCDT0_REG,
		     SUN8I_A83T_MIPI_CSI2_VCDT0_CH_VC(3, 3) |
		     SUN8I_A83T_MIPI_CSI2_VCDT0_CH_VC(2, 2) |
		     SUN8I_A83T_MIPI_CSI2_VCDT0_CH_VC(1, 1) |
		     SUN8I_A83T_MIPI_CSI2_VCDT0_CH_VC(0, 0) |
		     SUN8I_A83T_MIPI_CSI2_VCDT0_CH_DT(0, data_type));

	/* Start streaming. */
	regmap_update_bits(regmap, SUN8I_A83T_MIPI_CSI2_CFG_REG,
			   SUN8I_A83T_MIPI_CSI2_CFG_SYNC_EN,
			   SUN8I_A83T_MIPI_CSI2_CFG_SYNC_EN);

	ret = v4l2_subdev_call(remote_subdev, video, s_stream, 1);
	if (ret)
		goto disable;

	return 0;

disable:
	regmap_update_bits(regmap, SUN8I_A83T_MIPI_CSI2_CFG_REG,
			   SUN8I_A83T_MIPI_CSI2_CFG_SYNC_EN, 0);

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_CTRL_REG, 0);

	phy_power_off(cdev->dphy);

error_pm:
	pm_runtime_put(cdev->dev);

	return ret;
}

static const
struct v4l2_subdev_video_ops sun8i_a83t_mipi_csi2_subdev_video_ops = {
	.s_stream	= sun8i_a83t_mipi_csi2_s_stream,
};

/* Pad */

static int
sun8i_a83t_mipi_csi2_enum_mbus_code(struct v4l2_subdev *subdev,
				    struct v4l2_subdev_pad_config *config,
				    struct v4l2_subdev_mbus_code_enum *code_enum)
{
	if (code_enum->index >= ARRAY_SIZE(sun8i_a83t_mipi_csi2_mbus_codes))
		return -EINVAL;

	code_enum->code = sun8i_a83t_mipi_csi2_mbus_codes[code_enum->index];

	return 0;
}

static int sun8i_a83t_mipi_csi2_get_fmt(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_pad_config *config,
				   struct v4l2_subdev_format *format)
{
	struct sun8i_a83t_mipi_csi2_video *video =
		sun8i_a83t_mipi_csi2_subdev_video(subdev);
	struct v4l2_mbus_framefmt *mbus_format = &format->format;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		*mbus_format = *v4l2_subdev_get_try_format(subdev, config,
							   format->pad);
	else
		*mbus_format = video->mbus_format;

	return 0;
}

static int sun8i_a83t_mipi_csi2_set_fmt(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_pad_config *config,
				   struct v4l2_subdev_format *format)
{
	struct sun8i_a83t_mipi_csi2_video *video =
		sun8i_a83t_mipi_csi2_subdev_video(subdev);
	struct v4l2_mbus_framefmt *mbus_format = &format->format;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		*v4l2_subdev_get_try_format(subdev, config, format->pad) =
			*mbus_format;
	else
		video->mbus_format = *mbus_format;

	return 0;
}

static const struct v4l2_subdev_pad_ops sun8i_a83t_mipi_csi2_subdev_pad_ops = {
	.enum_mbus_code	= sun8i_a83t_mipi_csi2_enum_mbus_code,
	.get_fmt	= sun8i_a83t_mipi_csi2_get_fmt,
	.set_fmt	= sun8i_a83t_mipi_csi2_set_fmt,
};

/* Subdev */

static const struct v4l2_subdev_ops sun8i_a83t_mipi_csi2_subdev_ops = {
	.video		= &sun8i_a83t_mipi_csi2_subdev_video_ops,
	.pad		= &sun8i_a83t_mipi_csi2_subdev_pad_ops,
};

/* Notifier */

static int
sun8i_a83t_mipi_csi2_notifier_bound(struct v4l2_async_notifier *notifier,
				    struct v4l2_subdev *remote_subdev,
				    struct v4l2_async_subdev *remote_subdev_async)
{
	struct v4l2_subdev *subdev = notifier->sd;
	struct sun8i_a83t_mipi_csi2_video *video =
		sun8i_a83t_mipi_csi2_subdev_video(subdev);
	struct sun8i_a83t_mipi_csi2_dev *cdev =
		sun8i_a83t_mipi_csi2_video_dev(video);
	int source_pad;
	int ret;

	source_pad = media_entity_get_fwnode_pad(&remote_subdev->entity,
						 remote_subdev->fwnode,
						 MEDIA_PAD_FL_SOURCE);
	if (source_pad < 0)
		return source_pad;

	ret = media_create_pad_link(&remote_subdev->entity, source_pad,
				    &subdev->entity, 0,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret) {
		dev_err(cdev->dev, "failed to create %s:%u -> %s:%u link\n",
			remote_subdev->entity.name, source_pad,
			subdev->entity.name, 0);
		return ret;
	}

	video->remote_subdev = remote_subdev;

	return 0;
}

static const
struct v4l2_async_notifier_operations sun8i_a83t_mipi_csi2_notifier_ops = {
	.bound		= sun8i_a83t_mipi_csi2_notifier_bound,
};

/* Media Entity */

static const struct media_entity_operations sun8i_a83t_mipi_csi2_entity_ops = {
	.link_validate	= v4l2_subdev_link_validate,
};

/* Base Driver */

static int sun8i_a83t_mipi_csi2_suspend(struct device *dev)
{
	struct sun8i_a83t_mipi_csi2_dev *cdev = dev_get_drvdata(dev);

	clk_disable_unprepare(cdev->clk_misc);
	clk_disable_unprepare(cdev->clk_mipi);
	clk_disable_unprepare(cdev->clk_mod);
	clk_disable_unprepare(cdev->clk_bus);
	reset_control_assert(cdev->reset);

	return 0;
}

static int sun8i_a83t_mipi_csi2_resume(struct device *dev)
{
	struct sun8i_a83t_mipi_csi2_dev *cdev = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_deassert(cdev->reset);
	if (ret) {
		dev_err(cdev->dev, "failed to deassert reset\n");
		return ret;
	}

	ret = clk_prepare_enable(cdev->clk_bus);
	if (ret) {
		dev_err(cdev->dev, "failed to enable bus clock\n");
		goto error_reset;
	}

	ret = clk_prepare_enable(cdev->clk_mod);
	if (ret) {
		dev_err(cdev->dev, "failed to enable module clock\n");
		goto error_clk_bus;
	}

	ret = clk_prepare_enable(cdev->clk_mipi);
	if (ret) {
		dev_err(cdev->dev, "failed to enable MIPI clock\n");
		goto error_clk_mod;
	}

	ret = clk_prepare_enable(cdev->clk_misc);
	if (ret) {
		dev_err(cdev->dev, "failed to enable CSI misc clock\n");
		goto error_clk_mipi;
	}

	sun8i_a83t_mipi_csi2_init(cdev);

	return 0;

error_clk_mipi:
	clk_disable_unprepare(cdev->clk_mipi);

error_clk_mod:
	clk_disable_unprepare(cdev->clk_mod);

error_clk_bus:
	clk_disable_unprepare(cdev->clk_bus);

error_reset:
	reset_control_assert(cdev->reset);

	return ret;
}

static int
sun8i_a83t_mipi_csi2_v4l2_setup(struct sun8i_a83t_mipi_csi2_dev *cdev)
{
	struct sun8i_a83t_mipi_csi2_video *video = &cdev->video;
	struct v4l2_subdev *subdev = &video->subdev;
	struct v4l2_async_notifier *notifier = &video->notifier;
	struct fwnode_handle *handle;
	struct v4l2_fwnode_endpoint *endpoint;
	struct v4l2_async_subdev *subdev_async;
	int ret;

	/* Subdev */

	v4l2_subdev_init(subdev, &sun8i_a83t_mipi_csi2_subdev_ops);
	subdev->dev = cdev->dev;
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	strscpy(subdev->name, MODULE_NAME, sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, cdev);

	/* Entity */

	subdev->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	subdev->entity.ops = &sun8i_a83t_mipi_csi2_entity_ops;

	/* Pads */

	video->pads[0].flags = MEDIA_PAD_FL_SINK;
	video->pads[1].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&subdev->entity, 2, video->pads);
	if (ret)
		return ret;

	/* Endpoint */

	handle = fwnode_graph_get_endpoint_by_id(dev_fwnode(cdev->dev), 0, 0,
						 FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!handle) {
		ret = -ENODEV;
		goto error_media_entity;
	}

	endpoint = &video->endpoint;
	endpoint->bus_type = V4L2_MBUS_CSI2_DPHY;

	ret = v4l2_fwnode_endpoint_parse(handle, endpoint);
	fwnode_handle_put(handle);
	if (ret)
		goto error_media_entity;

	/* Notifier */

	v4l2_async_notifier_init(notifier);

	subdev_async = &video->subdev_async;
	ret = v4l2_async_notifier_add_fwnode_remote_subdev(notifier, handle,
							   subdev_async);
	if (ret)
		goto error_media_entity;

	video->notifier.ops = &sun8i_a83t_mipi_csi2_notifier_ops;

	ret = v4l2_async_subdev_notifier_register(subdev, notifier);
	if (ret < 0)
		goto error_notifier;

	/* Subdev */

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0)
		goto error_notifier_registered;

	/* Runtime PM */

	pm_runtime_enable(cdev->dev);
	pm_runtime_set_suspended(cdev->dev);

	return 0;

error_notifier_registered:
	v4l2_async_notifier_unregister(notifier);
error_notifier:
	v4l2_async_notifier_cleanup(notifier);
error_media_entity:
	media_entity_cleanup(&subdev->entity);

	return ret;
}

static int
sun8i_a83t_mipi_csi2_v4l2_teardown(struct sun8i_a83t_mipi_csi2_dev *cdev)
{
	struct sun8i_a83t_mipi_csi2_video *video = &cdev->video;
	struct v4l2_subdev *subdev = &video->subdev;
	struct v4l2_async_notifier *notifier = &video->notifier;

	v4l2_async_unregister_subdev(subdev);
	v4l2_async_notifier_unregister(notifier);
	v4l2_async_notifier_cleanup(notifier);
	media_entity_cleanup(&subdev->entity);
	v4l2_device_unregister_subdev(subdev);

	return 0;
}

static const struct regmap_config sun8i_a83t_mipi_csi2_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
	.max_register	= 0x120,
};

static int sun8i_a83t_mipi_csi2_probe(struct platform_device *pdev)
{
	struct sun8i_a83t_mipi_csi2_dev *cdev;
	struct resource *res;
	void __iomem *io_base;
	int ret;

	cdev = devm_kzalloc(&pdev->dev, sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	cdev->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	cdev->regmap =
		devm_regmap_init_mmio(&pdev->dev, io_base,
				      &sun8i_a83t_mipi_csi2_regmap_config);
	if (IS_ERR(cdev->regmap)) {
		dev_err(&pdev->dev, "failed to init register map\n");
		return PTR_ERR(cdev->regmap);
	}

	cdev->clk_bus = devm_clk_get(&pdev->dev, "bus");
	if (IS_ERR(cdev->clk_bus)) {
		dev_err(&pdev->dev, "failed to acquire bus clock\n");
		return PTR_ERR(cdev->clk_bus);
	}

	cdev->clk_mod = devm_clk_get(&pdev->dev, "mod");
	if (IS_ERR(cdev->clk_mod)) {
		dev_err(&pdev->dev, "failed to acquire mod clock\n");
		return PTR_ERR(cdev->clk_mod);
	}

	cdev->clk_mipi = devm_clk_get(&pdev->dev, "mipi");
	if (IS_ERR(cdev->clk_mipi)) {
		dev_err(&pdev->dev, "failed to acquire mipi clock\n");
		return PTR_ERR(cdev->clk_mipi);
	}

	cdev->clk_misc = devm_clk_get(&pdev->dev, "misc");
	if (IS_ERR(cdev->clk_misc)) {
		dev_err(&pdev->dev, "failed to acquire misc clock\n");
		return PTR_ERR(cdev->clk_misc);
	}

	cdev->reset = devm_reset_control_get_shared(&pdev->dev, NULL);
	if (IS_ERR(cdev->reset)) {
		dev_err(&pdev->dev, "failed to get reset controller\n");
		return PTR_ERR(cdev->reset);
	}

	ret = sun8i_a83t_dphy_register(cdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to init MIPI D-PHY\n");
		return ret;
	}

	platform_set_drvdata(pdev, cdev);

	ret = sun8i_a83t_mipi_csi2_v4l2_setup(cdev);
	if (ret)
		return ret;

	return 0;
}

static int sun8i_a83t_mipi_csi2_remove(struct platform_device *pdev)
{
	struct sun8i_a83t_mipi_csi2_dev *cdev = platform_get_drvdata(pdev);

	phy_exit(cdev->dphy);

	return sun8i_a83t_mipi_csi2_v4l2_teardown(cdev);
}

static const struct dev_pm_ops sun8i_a83t_mipi_csi2_pm_ops = {
	SET_RUNTIME_PM_OPS(sun8i_a83t_mipi_csi2_suspend,
			   sun8i_a83t_mipi_csi2_resume, NULL)
};

static const struct of_device_id sun8i_a83t_mipi_csi2_of_match[] = {
	{ .compatible = "allwinner,sun8i-a83t-mipi-csi2" },
	{},
};
MODULE_DEVICE_TABLE(of, sun8i_a83t_mipi_csi2_of_match);

static struct platform_driver sun8i_a83t_mipi_csi2_platform_driver = {
	.probe = sun8i_a83t_mipi_csi2_probe,
	.remove = sun8i_a83t_mipi_csi2_remove,
	.driver = {
		.name = MODULE_NAME,
		.of_match_table = of_match_ptr(sun8i_a83t_mipi_csi2_of_match),
		.pm = &sun8i_a83t_mipi_csi2_pm_ops,
	},
};
module_platform_driver(sun8i_a83t_mipi_csi2_platform_driver);

MODULE_DESCRIPTION("Allwinner A83T MIPI CSI-2 and D-PHY Controller Driver");
MODULE_AUTHOR("Paul Kocialkowski <paul.kocialkowski@bootlin.com>");
MODULE_LICENSE("GPL");
