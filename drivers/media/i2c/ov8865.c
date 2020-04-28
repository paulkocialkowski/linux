// SPDX-License-Identifier: GPL-2.0
/*
* Nunchuk driver
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

#define OV8865_XCLK_MIN		6000000
#define OV8865_XCLK_MAX		27000000

#define OV8865_DEFAULT_SLAVE_ID 0x36

#define OV8865_REG_CHIP_ID	0x300a
#define OV8865_REG_SLAVE_ID     0x3004

/* regulator supplies */
static const char * const ov8865_supply_name[] = {
	"DOVDD", /* Digital I/O (1,8V/2.8V) supply */
	"AVDD",  /* Analog (2.8V) supply */
	"DVDD",  /* Digital Core (1.2V) supply */
};

#define OV8865_NUM_SUPPLIES ARRAY_SIZE(ov8865_supply_name)

struct ov8865_dev {
	struct i2c_client *i2c_client;
//	struct v4l2_subdev sd;
//	struct media_pad pad;
	struct clk *xclk;
	u32 xclk_freq;
	struct regulator_bulk_data supplies[OV8865_NUM_SUPPLIES];
	struct gpio_desc *reset_gpio;

	struct gpio_desc *pwdn_gpio;
//	struct mutex lock;

//	int power_count;

//	struct ov8865_ctrls ctrls;
};
/*
static inline struct ov8865_dev *to_ov8865_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov8865_dev, sd);
}*/
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

static int ov8865_read_reg(struct ov8865_dev *sensor, u16 reg, u8 *val)
{
	struct i2c_client *client = sensor->i2c_client;
	struct i2c_msg msg[2];
	u8 buf[4];
	int ret = 0;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	msg[0].addr = client->addr;
	msg[0].flags = 0;//client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = client->addr;
	msg[1].flags =  I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 2;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s: error: reg=%x, slave = 0x%2x, value= 0x%2x\n",__func__, reg,client->addr, *val);
		return ret;
	}

	*val = buf[0];
	return 0;
}

static int ov8865_read_reg16(struct ov8865_dev *sensor, u16 reg, u16 *val)
{
	u8 hi, lo;
	int ret = 0;

	ret = ov8865_read_reg(sensor, reg, &hi);
	if (ret)
		return ret;
	ret = ov8865_read_reg(sensor, reg +1, &lo);
	if (ret)
		return ret;

	*val = ((u16)hi << 8) | (u16)lo;
	return 0;
}

static void ov8865_power(struct ov8865_dev *sensor, bool enable)
{
	gpiod_set_value_cansleep(sensor->pwdn_gpio, enable ? 1 : 0);
}

static void ov8865_reset(struct ov8865_dev *sensor)
{
	if(!sensor->reset_gpio)
		return;

	gpiod_set_value_cansleep(sensor->reset_gpio, 0);

	ov8865_power(sensor, false);
	usleep_range(5000, 10000);
	ov8865_power(sensor, true);
	usleep_range(5000, 10000);

	gpiod_set_value_cansleep(sensor->reset_gpio, 1);
	usleep_range(2000, 3000);

}

static int ov8865_set_power_on(struct ov8865_dev *sensor)
{
	struct i2c_client *client = sensor->i2c_client;
	int ret = 0;

	ov8865_power(sensor, true);
	ret = regulator_bulk_enable(OV8865_NUM_SUPPLIES, sensor->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		goto xclk_off;
	}
	ov8865_reset(sensor);

	ret = clk_prepare_enable(sensor->xclk);
	if(ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		return ret;
	}
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
/*
static int ov8865_set_power(struct ov8865_dev *sensor, bool on)
{
	int ret = 0;

	if (on){
		ret = ov8865_set_power_on(sensor);
		if (ret)
			return ret;
	}else {
		ov8865_set_power_off(sensor);
	}
	return 0;
}*/
/* --------------- Subdev Operations --------------- */
/*
static int ov8865_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov8865_dev *sensor = to_ov8865_dev(sd);
	int ret = 0;

	mutex_lock(&sensor->lock);
	if (sensor->power_count == !on) {
		ret = ov8865_set_power(sensor, !!on);
		if (ret)
			goto out;
	}*/
	/* Update the power count. */
/*	sensor->power_count += on ? 1 : -1;
	WARN_ON(sensor->power_count < 0);
out:
	mutex_unlock(&sensor->lock);
*/
/*	if (on && !ret && sensor->power_count == 1) {
		ret = v4l2_ctrl_handler_setup(&sensor->ctrls.handler);
	}
*/
/*	return ret;
}

static const struct v4l2_subdev_core_ops ov8865_core_ops = {
	.s_power = ov8865_s_power,
};

static const struct v4l2_subdev_ops ov8865_subdev_ops = {
	.core = &ov8865_core_ops,
};
*/
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
	int i = 0;
	u16 chip_id = 0xee;

	ret = ov8865_set_power_on(sensor);
	if (ret)
		return ret;

	ret = ov8865_read_reg16(sensor, OV8865_REG_CHIP_ID, &chip_id);
	if (ret) {
		dev_err(&client->dev, "%s: failed to reach chip identifier\n",
			__func__);
		goto power_off;
	}

	if (chip_id != 0x8865) {
		dev_err(&client->dev, "%s: wrong chip identifier, expected 0x8865, got 0x%x\n",__func__, chip_id);
		ret = -ENXIO;
	}else{
		printk("ov8865: bon ID");
	}

power_off:
	printk("ov8865: got  0x%x\n", chip_id);
	ov8865_set_power_off(sensor);
	return ret;
}

static int ov8865_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ov8865_dev *sensor;
	int ret = 0;
	pr_info("ov8865: probe");

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->i2c_client = client;
	/* get system clock (xclk) */
	/*sensor->xclk = devm_clk_get(dev, "xclk");
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
	}*/
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
/*
	v4l2_i2c_subdev_init(&sensor->sd, client, &ov8865_subdev_ops);
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			    V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		return ret;
*/
	ret = ov8865_get_regulators(sensor);
	if (ret)
		return ret;

//	mutex_init(&sensor->lock);

	ret = ov8865_check_chip_id(sensor);
	if (ret)
		goto entity_cleanup;
	return 0;


entity_cleanup:
//	mutex_destroy(&sensor->lock);
	return ret;
}


static int ov8865_remove(struct i2c_client *client)
{
	pr_info("ov8865: remove");
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
