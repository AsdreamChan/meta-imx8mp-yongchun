// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal IIO driver for the ROHM BH1750 ambient light sensor.
 *
 * One reading per sysfs read: the driver sends a one-time high-resolution
 * measurement command, waits for the conversion, and reads the 16-bit
 * result. No buffer, no trigger, no runtime PM. It exists to compare how
 * little an IIO driver has to provide with what a V4L2 capture driver does.
 *
 * Protocol (BH1750FVI datasheet):
 *   - every command is a single byte written to the device;
 *   - in one-time mode the device powers itself down after the conversion,
 *     so no separate power-down command is needed;
 *   - the result is two bytes, most significant first;
 *   - with the default measurement time register (69), lux = count / 1.2.
 *
 * Copyright (C) 2026 YP Chan
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/unaligned.h>

#define BH1750_POWER_ON		0x01
#define BH1750_ONE_TIME_H_RES	0x20

/*
 * The datasheet gives 120 ms typical and 180 ms maximum for a
 * high-resolution conversion. Wait for the maximum: reading early returns
 * the previous result, not an error, so a short wait would go unnoticed.
 */
#define BH1750_H_RES_WAIT_MS	180

/* 1 / 1.2 lux per count, as IIO_VAL_INT_PLUS_MICRO: 0.833333 */
#define BH1750_SCALE_MICRO	833333

struct yc_bh1750 {
	struct i2c_client *client;
	/* One conversion at a time: a second command would restart the first. */
	struct mutex lock;
};

static int yc_bh1750_cmd(struct yc_bh1750 *data, u8 cmd)
{
	int ret;

	ret = i2c_master_send(data->client, &cmd, 1);
	if (ret < 0)
		return ret;
	return ret == 1 ? 0 : -EIO;
}

static int yc_bh1750_measure(struct yc_bh1750 *data, int *val)
{
	u8 buf[2];
	int ret;

	guard(mutex)(&data->lock);

	ret = yc_bh1750_cmd(data, BH1750_ONE_TIME_H_RES);
	if (ret)
		return ret;

	msleep(BH1750_H_RES_WAIT_MS);

	ret = i2c_master_recv(data->client, buf, sizeof(buf));
	if (ret < 0)
		return ret;
	if (ret != sizeof(buf))
		return -EIO;

	*val = get_unaligned_be16(buf);
	return 0;
}

static int yc_bh1750_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct yc_bh1750 *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = yc_bh1750_measure(data, val);
		if (ret)
			return ret;
		dev_dbg(&data->client->dev, "raw %d\n", *val);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = BH1750_SCALE_MICRO;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static const struct iio_info yc_bh1750_info = {
	.read_raw = yc_bh1750_read_raw,
};

/* IIO_LIGHT becomes in_illuminance_raw and in_illuminance_scale in sysfs. */
static const struct iio_chan_spec yc_bh1750_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
	},
};

static int yc_bh1750_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct iio_dev *indio_dev;
	struct yc_bh1750 *data;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return dev_err_probe(dev, -EOPNOTSUPP,
				     "adapter cannot do plain I2C transfers\n");

	/*
	 * Unlike the V4L2 driver, devm is safe here: the IIO core keeps the
	 * iio_dev alive while a sysfs file is open, and unregistering stops
	 * new reads from reaching read_raw.
	 */
	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	ret = devm_mutex_init(dev, &data->lock);
	if (ret)
		return ret;

	/*
	 * Talk to the device once before registering, so a wrong address or
	 * missing wiring fails the probe instead of every later read.
	 */
	ret = yc_bh1750_cmd(data, BH1750_POWER_ON);
	if (ret)
		return dev_err_probe(dev, ret, "no response at 0x%02x\n",
				     client->addr);

	indio_dev->name = "yongchun-bh1750";
	indio_dev->info = &yc_bh1750_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = yc_bh1750_channels;
	indio_dev->num_channels = ARRAY_SIZE(yc_bh1750_channels);

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id yc_bh1750_of_match[] = {
	{ .compatible = "yongchun,bh1750" },
	{ }
};
MODULE_DEVICE_TABLE(of, yc_bh1750_of_match);

static const struct i2c_device_id yc_bh1750_id[] = {
	{ "yongchun-bh1750" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, yc_bh1750_id);

static struct i2c_driver yc_bh1750_driver = {
	.driver = {
		.name = "yongchun-bh1750",
		.of_match_table = yc_bh1750_of_match,
	},
	.probe = yc_bh1750_probe,
	.id_table = yc_bh1750_id,
};
module_i2c_driver(yc_bh1750_driver);

MODULE_AUTHOR("YP Chan");
MODULE_DESCRIPTION("Minimal IIO driver for the ROHM BH1750 light sensor");
MODULE_LICENSE("GPL");
