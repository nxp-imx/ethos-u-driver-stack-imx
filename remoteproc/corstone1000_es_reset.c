/*
 * Copyright (c) 2022 Arm Limited. All rights reserved.
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

/* External system reset control bits */
#define EXTSYS_CPU_WAIT (0x0)
#define EXTSYS_RST_REQ (0x1)

/* External system reset status bits */
#define EXTSYS_STATUS_NO_RST_REQ (0x0)
#define EXTSYS_STATUS_RST_REQ_NOT_COMPLETED (0x1)
#define EXTSYS_STATUS_RST_REQ_COMPLETED (0x2)
#define EXTSYS_STATUS_MASK(a) (0x3 & ((a) >> 1))

struct cs1k_es_reset_data {
	struct reset_controller_dev rcdev;
	struct device               *dev;
	void __iomem                *ctrl;
	void __iomem                *status;
};

int cs1k_es_assert(struct reset_controller_dev *rcdev,
		   unsigned long id)
{
	u32 status;
	struct cs1k_es_reset_data *reset =
		container_of(rcdev, struct cs1k_es_reset_data, rcdev);

	if (id)
		return -ENODEV;

	dev_dbg(reset->dev, "Asserting reset");

	/* set cpu wait and reset request of external system */
	writel((1 << EXTSYS_CPU_WAIT) | (1 << EXTSYS_RST_REQ), reset->ctrl);

	status = EXTSYS_STATUS_MASK(readl(reset->status));
	dev_dbg(reset->dev, "status deasserting reset: %u", status);

	return status == EXTSYS_STATUS_RST_REQ_COMPLETED ? 0 : 1;
}

int cs1k_es_deassert(struct reset_controller_dev *rcdev,
		     unsigned long id)
{
	u32 status;
	struct cs1k_es_reset_data *reset =
		container_of(rcdev, struct cs1k_es_reset_data, rcdev);

	if (id)
		return -ENODEV;

	/* release cpu wait */
	dev_dbg(reset->dev, "Deasserting reset");

	writel(0, reset->ctrl);

	status = EXTSYS_STATUS_MASK(readl(reset->status));
	dev_dbg(reset->dev, "status deasserting reset: %u", status);

	return status == EXTSYS_STATUS_NO_RST_REQ ? 0 : 1;
}

static struct reset_control_ops cs1k_es_reset_ops = {
	.assert   = cs1k_es_assert,
	.deassert = cs1k_es_deassert,
};

static int of_reset_noop(struct reset_controller_dev *rcdev,
			 const struct of_phandle_args *reset_spec)
{
	return 0;
}

static int cs1k_es_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cs1k_es_reset_data *data;
	struct resource *res;

	if (!dev->of_node)
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rstreg");
	data->ctrl = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->ctrl))
		return PTR_ERR(data->ctrl);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "streg");
	data->status = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->status))
		return PTR_ERR(data->status);

	data->dev = dev;
	platform_set_drvdata(pdev, data);

	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = 1;
	data->rcdev.ops = &cs1k_es_reset_ops;
	data->rcdev.of_node = pdev->dev.of_node;
	/* only one reset line for this reset controller */
	data->rcdev.of_xlate = of_reset_noop;

	dev_info(dev, "registering reset to core");

	return devm_reset_controller_register(dev, &data->rcdev);
}

static int cs1k_es_reset_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id cs1k_es_reset_match[] = {
	{ .compatible = "arm,cs1k_es_rst", .data = 0 },
	{ /* sentinel */ },
};

static struct platform_driver cs1k_es_reset_driver = {
	.probe                  = cs1k_es_reset_probe,
	.remove                 = cs1k_es_reset_remove,
	.driver                 = {
		.name           = "cs1k_es-reset",
		.of_match_table = of_match_ptr(cs1k_es_reset_match),
	},
};
module_platform_driver(cs1k_es_reset_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Arm Corstone1000 External System Reset Driver");
MODULE_AUTHOR("Arm Ltd");
