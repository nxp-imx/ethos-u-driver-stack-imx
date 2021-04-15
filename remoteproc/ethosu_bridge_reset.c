/*
 * Copyright (c) 2021 Arm Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>

#define ETHOSU_BRIDGE_RESET_DRIVER_VERSION "0.0.1"

struct ethosu_reset {
	struct reset_controller_dev rst;
	struct device               *dev;
	void __iomem                *base;
};

#define ETHOSU_BRIDGE_ID(base)   (base)
#define ETHOSU_BRIDGE_CTRL(base) ((base) + 0x100)

#define ETHOSU_BRIDGE_WAIT_ENABLE (0x2)
#define ETHOSU_BRIDGE_RESET       (0x1)

static void __iomem *bridge_verify_and_remap(struct device *dev,
					     struct resource *res)
{
	void __iomem *base = devm_ioremap_resource(dev, res);
	u32 id;
	u16 magic;
	u8 minor;
	u8 major;

	if (IS_ERR(base))
		return base;

	id = readl(ETHOSU_BRIDGE_ID(base));
	magic = id & 0x0000ffff;
	minor = (id & 0x00ff0000) >> 16;
	major = (id & 0xff000000) >> 24;

	dev_dbg(dev, "verifying bridge %d.%d", major, minor);

	if (magic != 0xBD9E)
		return IOMEM_ERR_PTR(-EINVAL);

	return base;
}

int ethosu_bridge_assert(struct reset_controller_dev *rcdev,
			 unsigned long id)
{
	struct ethosu_reset *ethosu = container_of(rcdev, struct ethosu_reset,
						   rst);

	/* pull reset */
	dev_dbg(ethosu->dev, "Asserting reset");

	/* set wait and reset */
	writel(ETHOSU_BRIDGE_WAIT_ENABLE | ETHOSU_BRIDGE_RESET,
	       ETHOSU_BRIDGE_CTRL(ethosu->base));

	return 0;
}

int ethosu_bridge_deassert(struct reset_controller_dev *rcdev,
			   unsigned long id)
{
	struct ethosu_reset *ethosu = container_of(rcdev, struct ethosu_reset,
						   rst);

	/* release reset */
	dev_dbg(ethosu->dev, "Deasserting reset");
	writel(~ETHOSU_BRIDGE_WAIT_ENABLE & ETHOSU_BRIDGE_RESET,
	       ETHOSU_BRIDGE_CTRL(ethosu->base));

	return 0;
}

static struct reset_control_ops ethosu_reset_bridge_ops = {
	.assert   = ethosu_bridge_assert,
	.deassert = ethosu_bridge_deassert,
};

static const struct of_device_id ethosu_reset_match[] = {
	{ .compatible = "arm,ethosu-bridge-reset", .data = 0 },
	{ /* sentinel */ },
};

static int ethosu_bridge_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ethosu_reset *ethosu;
	struct resource *res;

	ethosu = devm_kzalloc(&pdev->dev, sizeof(*ethosu), GFP_KERNEL);
	if (!ethosu)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	ethosu->base = bridge_verify_and_remap(dev, res);
	ethosu->dev = dev;

	if (IS_ERR(ethosu->base))
		return PTR_ERR(ethosu->base);

	platform_set_drvdata(pdev, ethosu);

	ethosu->rst.owner = THIS_MODULE;
	ethosu->rst.nr_resets = 1;
	ethosu->rst.ops = &ethosu_reset_bridge_ops;
	ethosu->rst.of_node = pdev->dev.of_node;

	dev_dbg(dev, "registering to reset controller core");

	return devm_reset_controller_register(dev, &ethosu->rst);
}

static int ethosu_bridge_reset_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver ethosu_reset_driver = {
	.probe                  = ethosu_bridge_reset_probe,
	.remove                 = ethosu_bridge_reset_remove,
	.driver                 = {
		.name           = "ethosu-bridge-reset",
		.of_match_table = of_match_ptr(ethosu_reset_match),
	},
};

module_platform_driver(ethosu_reset_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Arm Ltd");
MODULE_DESCRIPTION("Arm Ethos-U NPU Bridge Reset Driver");
MODULE_VERSION(ETHOSU_BRIDGE_RESET_DRIVER_VERSION);
