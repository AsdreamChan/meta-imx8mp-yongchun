// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test pattern generator, step 1: a platform driver that binds to
 * yongchun,tpg device tree nodes and reads each node's frame interval.
 * The V4L2 capture device is added in the next step.
 *
 * The interval is a fraction of a second, numerator first, the same
 * shape as V4L2's timeperframe: <1 30> is 30 frames per second,
 * <1 9> is 9. A fraction also expresses a period that is not a whole
 * number of microseconds.
 */

#include <linux/math64.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/time64.h>

struct yc_tpg {
	struct device *dev;
	u32 interval[2];	/* seconds per frame: numerator, denominator */
	u64 period_ns;
};

static int yc_tpg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct yc_tpg *tpg;
	int ret;

	dev_dbg(dev, "probing %pfw\n", dev_fwnode(dev));

	tpg = devm_kzalloc(dev, sizeof(*tpg), GFP_KERNEL);
	if (!tpg)
		return -ENOMEM;
	tpg->dev = dev;

	ret = device_property_read_u32_array(dev, "yongchun,frame-interval",
					     tpg->interval,
					     ARRAY_SIZE(tpg->interval));
	if (ret)
		return dev_err_probe(dev, ret,
				     "cannot read yongchun,frame-interval\n");

	if (!tpg->interval[0] || !tpg->interval[1])
		return dev_err_probe(dev, -EINVAL,
				     "frame interval %u/%u is not valid\n",
				     tpg->interval[0], tpg->interval[1]);

	tpg->period_ns = div_u64((u64)tpg->interval[0] * NSEC_PER_SEC,
				 tpg->interval[1]);

	platform_set_drvdata(pdev, tpg);

	dev_info(dev, "frame interval %u/%u s, period %llu ns\n",
		 tpg->interval[0], tpg->interval[1], tpg->period_ns);
	return 0;
}

static void yc_tpg_remove(struct platform_device *pdev)
{
	struct yc_tpg *tpg = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "removed, period was %llu ns\n", tpg->period_ns);
}

static const struct of_device_id yc_tpg_of_match[] = {
	{ .compatible = "yongchun,tpg" },
	{ }
};
MODULE_DEVICE_TABLE(of, yc_tpg_of_match);

static struct platform_driver yc_tpg_driver = {
	.probe = yc_tpg_probe,
	.remove = yc_tpg_remove,
	.driver = {
		.name = "yongchun-tpg",
		.of_match_table = yc_tpg_of_match,
	},
};
module_platform_driver(yc_tpg_driver);

MODULE_AUTHOR("YP Chan <53651187+AsdreamChan@users.noreply.github.com>");
MODULE_DESCRIPTION("Test pattern generator for the sprint 7 V4L2 work");
MODULE_LICENSE("GPL");
