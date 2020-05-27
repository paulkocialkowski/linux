// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner A83t MIPI Camera Sensor Interface driver 
 * Copyright Kévin L'hôpital (C) 2020
 */

//#include <linux/clk.h>
//#include <linux/interrupt.h>//
//#include <linux/io.h>//
//#include <linux/module.h>//
//#include <linux/platform_device.h>//
//#include <linux/regmap.h>
//#include <linux/reset.h>//

//#include "sun6i_csi.h"
#include "sun6i_mipi_csi.h"
#include "sun6i_dphy.h"

#define MIPI_OFFSET     0x1000
#define IS_FLAG(x, y) (((x) & (y)) == y) 

/*struct sun6i_mipi_csi_dev {
	struct sun6i_mipi_csi		mipi_csi;
	struct device			*dev;
//	struct regmap			*regmap;
	struct clk			*clk_mod;
//	struct clk			*clk_ram;
//	struct reset_control		*rstc_bus;
	int				planar_offset[3];
};
*/
enum pkt_fmt {
	MIPI_FS           = 0X00, //short packet      
	MIPI_FE           = 0X01,
  	MIPI_LS           = 0X02,
  	MIPI_LE           = 0X03,
  	MIPI_SDAT0          = 0X08,
  	MIPI_SDAT1          = 0X09,
  	MIPI_SDAT2          = 0X0A,
  	MIPI_SDAT3          = 0X0B,
  	MIPI_SDAT4          = 0X0C,
  	MIPI_SDAT5          = 0X0D,
  	MIPI_SDAT6          = 0X0E,
  	MIPI_SDAT7          = 0X0F,             
	/*  NULL          = 0X10, //long packet */     
  	MIPI_BLK            = 0X11,
  	MIPI_EMBD         = 0X12,
  	MIPI_YUV420       = 0X18,
  	MIPI_YUV420_10    = 0X19,
  	MIPI_YUV420_CSP   = 0X1C,
  	MIPI_YUV420_CSP_10 =  0X1D,
  	MIPI_YUV422       = 0X1E,
  	MIPI_YUV422_10    = 0X1F,
  	MIPI_RGB565       = 0X22,
  	MIPI_RGB888       = 0X24,
  	MIPI_RAW8         = 0X2A,
  	MIPI_RAW10          = 0X2B,
  	MIPI_RAW12          = 0X2C,
  	MIPI_USR_DAT0     = 0X30,
  	MIPI_USR_DAT1     = 0X31,
  	MIPI_USR_DAT2     = 0X32,
  	MIPI_USR_DAT3     = 0X33,
  	MIPI_USR_DAT4     = 0X34,
  	MIPI_USR_DAT5     = 0X35,
  	MIPI_USR_DAT6     = 0X36,
  	MIPI_USR_DAT7     = 0X37,
};

static inline struct sun6i_csi_dev *sun6i_csi_to_dev(struct sun6i_csi *csi)
{
	return container_of(csi, struct sun6i_csi_dev, csi);
}

static enum pkt_fmt get_pkt_fmt(u16 bus_pix_code)
{
	switch (bus_pix_code) {
	case MEDIA_BUS_FMT_RGB565_1X16:
		return MIPI_RGB565;
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_UYVY8_1X16:
		return MIPI_YUV422;
	case MEDIA_BUS_FMT_UYVY10_2X10:
		return MIPI_YUV422_10;
	case MEDIA_BUS_FMT_RGB888_1X24:
		return MIPI_RGB888;
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		return MIPI_RAW8;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		return MIPI_RAW10;
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		return MIPI_RAW12;
	default:
		return MIPI_RAW8;
	}
}

/**/
void sun6i_mipi_csi_set_stream(struct sun6i_csi *csi, bool enable)
{
	struct sun6i_csi_dev *sdev = sun6i_csi_to_dev(csi);
	u32 val;
	if (enable) {
		regmap_read(sdev->regmap, MIPI_OFFSET + 0x100, &val);
		regmap_write(sdev->regmap, MIPI_OFFSET + 0x100, val|0x80000000);
		usleep_range(10000, 12000);
		//sun6i_dphy_enable(sdev);
	} else {
		//sun6i_dphy_disable(sdev);
		regmap_read(sdev->regmap, MIPI_OFFSET + 0x100, &val);
		regmap_write(sdev->regmap, MIPI_OFFSET + 0x100, val&0x7fffffff);
	}
}
/**/

void sun6i_mipi_csi_setup_bus(struct sun6i_csi *csi)
{
	struct v4l2_fwnode_endpoint *endpoint = &csi->v4l2_ep;
	struct sun6i_csi_dev *sdev = sun6i_csi_to_dev(csi);
//	struct sun6i_dphy_param dphy_param = {0};
	int lane_num = endpoint->bus.mipi_csi2.num_data_lanes;
	int flags = endpoint->bus.mipi_csi2.flags;
	int total_rx_ch;
	u32 val;
	int ch;

	total_rx_ch = 0;
	if (IS_FLAG(flags, V4L2_MBUS_CSI2_CHANNEL_0))
		total_rx_ch++;

	if (IS_FLAG(flags, V4L2_MBUS_CSI2_CHANNEL_1))
		total_rx_ch++;

	if (IS_FLAG(flags, V4L2_MBUS_CSI2_CHANNEL_2))
		total_rx_ch++;

	if (IS_FLAG(flags, V4L2_MBUS_CSI2_CHANNEL_3))
		total_rx_ch++;

	if (!total_rx_ch) {
		dev_dbg(sdev->dev,
			 "No receive channel assigned, using channel 0.\n");
		total_rx_ch++;
	}
	/*set lane*/
	regmap_read(sdev->regmap, MIPI_OFFSET + 0x100, &val);
	regmap_write(sdev->regmap, MIPI_OFFSET + 0x100, val|((lane_num -1) <<
							      4));
	/*set total channels*/
	regmap_read(sdev->regmap, MIPI_OFFSET + 0x100, &val);
	regmap_write(sdev->regmap, MIPI_OFFSET + 0x100, val|((total_rx_ch - 1)
							      << 16));

	for (ch = 0; ch < total_rx_ch; ch++) {
		switch(ch)
		{
			case 0:
				regmap_read(sdev->regmap, MIPI_OFFSET + 0x104,
					    &val);
				regmap_write(sdev->regmap, MIPI_OFFSET + 0x104,
					     val|(ch << 6));
				regmap_read(sdev->regmap, MIPI_OFFSET + 0x104,
					    &val);
				regmap_write(sdev->regmap, MIPI_OFFSET + 0x104,
					     val|get_pkt_fmt(csi->config.code));
				break;
			case 1:
				regmap_read(sdev->regmap, MIPI_OFFSET + 0x104,
					    &val);
				regmap_write(sdev->regmap, MIPI_OFFSET + 0x104,
					     val|(ch << 14));
				regmap_read(sdev->regmap, MIPI_OFFSET + 0x104,
					    &val);
				regmap_write(sdev->regmap, MIPI_OFFSET + 0x104,
					     val|get_pkt_fmt(csi->config.code
							      << 8));
				break;
			case 2:
				regmap_read(sdev->regmap, MIPI_OFFSET + 0x104,
					    &val);
				regmap_write(sdev->regmap, MIPI_OFFSET + 0x104,
					     val|(ch << 22));
				regmap_read(sdev->regmap, MIPI_OFFSET + 0x104,
					    &val);
				regmap_write(sdev->regmap, MIPI_OFFSET + 0x104,
					     val|get_pkt_fmt(csi->config.code
							     << 16));
				break;
			case 3:
				regmap_read(sdev->regmap, MIPI_OFFSET + 0x104,
					    &val);
				regmap_write(sdev->regmap, MIPI_OFFSET + 0x104,
					     val|(ch << 30));
				regmap_read(sdev->regmap, MIPI_OFFSET + 0x104,
					    &val);
				regmap_write(sdev->regmap, MIPI_OFFSET + 0x104,
					     val|get_pkt_fmt(csi->config.code
							      <<24));
				break;
			default:
				regmap_read(sdev->regmap, MIPI_OFFSET + 0x104,
					    &val);
				regmap_write(sdev->regmap, MIPI_OFFSET + 0x104,
					     val|0xc0804000);
				break;
			}
	}

	/*dphy_param.lane_num = lane_num;
	dphy_param.bps = 400 * 1000 * 1000;
	dphy_param.auto_bps = 1;
	sun6i_dphy_set_param(sdev, &dphy_param);
*/
	sun6i_mipi_csi_dphy_init(sdev);
}



/* ---------------------------------------------------------------------------
 * Media Controller and V4L2
 * ---------------------------------------------------------------------------
 *//*
static int sun6i_mipi_csi_link_entity(struct sun6i_mipi_csi *mipi_csi,
				      struct media_entity *entity,
				      struct fwnode_handle *fwnode)
{
	struct media_entity *sink;
	struct media_pad *sink_pad;
	int src_pad_index;
	int ret;

	ret = media_entity_get_fwnode_pad(entity, fwnode, MEDIA_PAD_FL_SOURCE);
	if (ret < 0) {
		dev_err(mipi_csi->dev, "%s: no source pad in external entity %s\n",
			__func__, entity->name);
		return -EINVAL;
	}

	src_pad_index = ret;

	sink = &mipi_csi->video.vdev.entity;
	sink_pad = &mipi_csi->video.pad;

	dev_dbg(mipi_csi->dev, "creating %s:%u -> %s:%u link\n", entity->name,
		src_pad_index, sink->name, sink_pad->index);
	ret = media_create_pad_link(entity, src_pad_index, sink,
				    sink_pad->index, MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret < 0) {
		dev_err(mipi_csi->dev, "failed to create %s:%u -> %s:%u link\n",
			entity->name, src_pad_index, sink->name,
			sink_pad->index);
		return ret;
	}

	return 0;
}

static int sun6i_subdev_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct sun6i_mipi_csi *mipi_csi = container_of(notifier, struct
						       sun6i_mipi_csi,
						       notifier);
	struct v4l2_device *v4l2_dev = &mipi_csi->v4l2_dev;
	struct v4l2_subdev *sd;
	int ret;

	dev_dbg(mipi_csi->dev, "notify complete, all subdevs registered\n");

	sd = list_first_entry(&v4l2_dev->subdevs, struct v4l2_subdev, list);
	if (!sd)
		return -EINVAL;

	ret = sun6i_mipi_csi_link_entity(mipi_csi, &sd->entity, sd->fwnode);
	if (ret < 0)
		return ret;

	ret = v4l2_device_register_subdev_nodes(&mipi_csi->v4l2_dev);
	if (ret < 0)
		return ret;

	return media_device_register(&mipi_csi->media_dev);
}

static const struct v4l2_async_notifier_operations sun6i_mipi_csi_async_ops = {
	.complete = sun6i_subdev_notify_complete,
};

static int sun6i_mipi_csi_fwnode_parse(struct device *dev, 
				       struct v4l2_fwnode_endpoint *vep,
				       struct v4l2_async_subdev *asd)
{
	struct sun6i_mipi_csi *mipi_csi = dev_get_drvdata(dev);

	if (vep->base.port || vep->base.id) {
		dev_warn(dev, "Only support a single port with one endpoint\n");
		return -ENOTCONN;
	}
	pr_debug("coucou");
	printk("bus: %d", vep->bus_type);
	switch (vep->bus_type) {
	case V4L2_MBUS_CSI2_DPHY:
		mipi_csi->v4l2_ep = *vep;
		return 0;
	default:
		dev_err(dev, "Unsupported media bus type\n");
		return -ENOTCONN;
	}
}

static void sun6i_mipi_csi_v4l2_cleanup(struct sun6i_mipi_csi *mipi_csi)
{
	media_device_unregister(&mipi_csi->media_dev);
	v4l2_async_notifier_unregister(&mipi_csi->notifier);
	v4l2_async_notifier_cleanup(&mipi_csi->notifier);
	//sun6i_video_cleanup(&mipi_csi->video);
	v4l2_device_unregister(&mipi_csi->v4l2_dev);
	v4l2_ctrl_handler_free(&mipi_csi->ctrl_handler);
	media_device_cleanup(&mipi_csi->media_dev);
}

static int sun6i_mipi_csi_v4l2_init(struct sun6i_mipi_csi *mipi_csi)
{
	int ret;

	mipi_csi->media_dev.dev = mipi_csi->dev;
	strscpy(mipi_csi->media_dev.model, "Allwinner Video Capture Device",
		sizeof(mipi_csi->media_dev.model));
	mipi_csi->media_dev.hw_revision = 0;

	media_device_init(&mipi_csi->media_dev);
	v4l2_async_notifier_init(&mipi_csi->notifier);

	ret = v4l2_ctrl_handler_init(&mipi_csi->ctrl_handler, 0);
	if (ret) {
		dev_err(mipi_csi->dev, "V4L2 controls handler init failed (%d)\n",
			ret);
		goto clean_media;
	}

	mipi_csi->v4l2_dev.mdev = &mipi_csi->media_dev;
	mipi_csi->v4l2_dev.ctrl_handler = &mipi_csi->ctrl_handler;
	ret = v4l2_device_register(mipi_csi->dev, &mipi_csi->v4l2_dev);
	if (ret) {
		dev_err(mipi_csi->dev, "V4L2 device registration failed (%d)\n",
			ret);
		goto free_ctrl;
	}
*/
	/*ret = sun6i_video_init(&mipi_csi->video, mipi_csi, MODULE_NAME);
	if (ret)
		goto unreg_v4l2;*/
/*
	ret = v4l2_async_notifier_parse_fwnode_endpoints(mipi_csi->dev,
							 &mipi_csi->notifier,
							 sizeof(struct
								v4l2_async_subdev),
							 sun6i_mipi_csi_fwnode_parse);
	if (ret)
		goto clean_video;

	mipi_csi->notifier.ops = &sun6i_mipi_csi_async_ops;

	ret = v4l2_async_notifier_register(&mipi_csi->v4l2_dev,
					   &mipi_csi->notifier);
	if (ret) {
		dev_err(mipi_csi->dev, "notifier registration failed\n");
		goto clean_video;
	}

	return 0;

clean_video:
//	sun6i_video_cleanup(&mipi_csi->video);
unreg_v4l2:
	v4l2_device_unregister(&mipi_csi->v4l2_dev);
free_ctrl:
	v4l2_ctrl_handler_free(&mipi_csi->ctrl_handler);
clean_media:
	v4l2_async_notifier_cleanup(&mipi_csi->notifier);
	media_device_cleanup(&mipi_csi->media_dev);

	return ret;
}
*/
/* ---------------------------------------------------------------------------
 * Resources and IRQ
 * ---------------------------------------------------------------------------
 */
/*
static irqreturn_t sun6i_mipi_csi_isr(int irq, void *dev_id)
{
	struct sun6i_mipi_csi_dev *sdev = (struct sun6i_mipi_csi_dev *)dev_id;
	struct regmap *regmap = sdev->regmap;
	u32 status;

	regmap_read(regmap, CSI
*/
/*not sure*//*
static const struct regmap_config sun6i_mipi_csi_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x9c,
};

static int sun6i_mipi_csi_resource_request(struct sun6i_mipi_csi_dev *sdev,
					   struct platform_device *pdev)
{
//	struct resource *res;
//	void __iomem *io_base;
//	int ret;
//	int irq;

//	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
*//*	io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	sdev->regmap = devm_regmap_init_mmio_clk(&pdev->dev, "bus", io_base,
						 &sun6i_mipi_csi_regmap_config);
	if (IS_ERR(sdev->regmap)) {
		dev_err(&pdev->dev, "Failed to init register map\n");
		return PTR_ERR(sdev->regmap);
	}
*//*
	sdev->clk_mod = devm_clk_get(&pdev->dev, "mod");
	if (IS_ERR(sdev->clk_mod)) {
		dev_err(&pdev->dev, "Unable to acquire mipi csi clock\n");
		return PTR_ERR(sdev->clk_mod);
	}

*//*	sdev->clk_ram = devm_clk_get(&pdev->dev, "ram");
	if (IS_ERR(sdev->clk_ram)) {
		dev_err(&pdev->dev, "Unable to acquire dram-mipi-csi clock\n");
		return PTR_ERR(sdev->clk_ram);
	}*/

/*	sdev->rstc_bus = devm_reset_control_get_shared(&pdev->dev, NULL);
	if (IS_ERR(sdev->rstc_bus)) {
		dev_err(&pdev->dev, "Cannot get reset controller\n");
		return PTR_ERR(sdev->rstc_bus);
	}*/

/*	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENXIO;

	ret = devm_request_irq(&pdev->dev, irq, sun6i_mipi_csi_isr, 0,
			       MODULE_NAME, sdev);
	if (ret) {
		dev_err(&pdev->dev, "Cannot request mipi csi IRQ\n");
		return ret;
	}
*//*
	return 0;
}

static int sun6i_mipi_csi_probe(struct platform_device *pdev)
{
	struct sun6i_mipi_csi_dev *sdev;
	int ret;

	sdev = devm_kzalloc(&pdev->dev, sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return -ENOMEM;

	sdev->dev = &pdev->dev;

	ret = sun6i_mipi_csi_resource_request(sdev, pdev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, sdev);

	sdev->mipi_csi.dev = &pdev->dev;
	return sun6i_mipi_csi_v4l2_init(&sdev->mipi_csi);
}

static int sun6i_mipi_csi_remove(struct platform_device *pdev)
{
	struct sun6i_mipi_csi_dev *sdev = platform_get_drvdata(pdev);

	sun6i_mipi_csi_v4l2_cleanup(&sdev->mipi_csi);

	return 0;
}

static const struct of_device_id sun6i_mipi_csi_of_match[] = {
	{ .compatible = "allwinner,sun8i-a83t-mipi-csi", },
	{},
};
MODULE_DEVICE_TABLE(of, sun6i_mipi_csi_of_match);

static struct platform_driver sun6i_mipi_csi_platform_driver = {
	.probe = sun6i_mipi_csi_probe,
	.remove = sun6i_mipi_csi_remove,
	.driver = {
		.name = MODULE_NAME,
		.of_match_table = of_match_ptr(sun6i_mipi_csi_of_match),
	},
};
module_platform_driver(sun6i_mipi_csi_platform_driver);

MODULE_DESCRIPTION("Allwinner V3s MIPI Camera Sensor Interface driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kévin L'hôpital <kevin.lhopital@bootlin.com>");*/
