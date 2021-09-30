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

#define JUNO_FPGA_RESET_DRIVER_VERSION "0.0.1"

struct juno_fpga_reset {
	struct reset_controller_dev rst;
	struct device               *dev;
	void __iomem                *base;
};

#define JUNO_FPGA_RESET_ID(base)          (base)
#define JUNO_FPGA_RESET_SOFT_RESET(base) ((base) + 0x140)
#define JUNO_FPGA_RESET_CPU_WAIT(base)   ((base) + 0x144)

#define JUNO_FPGA_RESET_SET_RESET       (0x1)
#define JUNO_FPGA_RESET_UNSET_RESET     (0x0)
#define JUNO_FPGA_RESET_SET_CPUWAIT     (0x1)
#define JUNO_FPGA_RESET_UNSET_CPUWAIT   (0x0)

static void __iomem *verify_and_remap(struct device *dev,
				      struct resource *res)
{
	void __iomem *base = devm_ioremap_resource(dev, res);
	u32 id;

	if (IS_ERR(base))
		return base;

	id = readl(JUNO_FPGA_RESET_ID(base));

	if (id != 0x2010f &&
                id != 0x20110 &&
                id != 0x20111) {
		return IOMEM_ERR_PTR(-EINVAL);
        }

	return base;
}

int juno_fpga_reset_assert(struct reset_controller_dev *rcdev,
			 unsigned long id)
{
	struct juno_fpga_reset *reset = container_of(rcdev, struct juno_fpga_reset,
						   rst);

	/* pull reset */
	dev_dbg(reset->dev, "Asserting reset");

	/* set wait and reset */
	writel(JUNO_FPGA_RESET_SET_RESET,
	       JUNO_FPGA_RESET_SOFT_RESET(reset->base));
	writel(JUNO_FPGA_RESET_SET_CPUWAIT,
	       JUNO_FPGA_RESET_CPU_WAIT(reset->base));

	writel(JUNO_FPGA_RESET_UNSET_RESET,
	       JUNO_FPGA_RESET_SOFT_RESET(reset->base));
	return 0;
}

int juno_fpga_reset_deassert(struct reset_controller_dev *rcdev,
			   unsigned long id)
{
	struct juno_fpga_reset *reset = container_of(rcdev, struct juno_fpga_reset,
						   rst);

	/* release wait */
	dev_dbg(reset->dev, "Deasserting reset");

	writel(JUNO_FPGA_RESET_UNSET_CPUWAIT,
	       JUNO_FPGA_RESET_CPU_WAIT(reset->base));
	return 0;
}

static struct reset_control_ops juno_fpga_reset_ops = {
	.assert   = juno_fpga_reset_assert,
	.deassert = juno_fpga_reset_deassert,
};

static const struct of_device_id juno_fpga_reset_match[] = {
	{ .compatible = "arm,mali_fpga_sysctl", .data = 0 },
	{ /* sentinel */ },
};

static int juno_fpga_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct juno_fpga_reset *reset;
	struct resource *res;

	reset = devm_kzalloc(&pdev->dev, sizeof(*reset), GFP_KERNEL);
	if (!reset)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	reset->base = verify_and_remap(dev, res);
	reset->dev = dev;

	if (IS_ERR(reset->base))
		return PTR_ERR(reset->base);

	platform_set_drvdata(pdev, reset);

	reset->rst.owner = THIS_MODULE;
	reset->rst.nr_resets = 1;
	reset->rst.ops = &juno_fpga_reset_ops;
	reset->rst.of_node = pdev->dev.of_node;

	dev_dbg(dev, "registering to reset controller core");

	return devm_reset_controller_register(dev, &reset->rst);
}

static int juno_fpga_reset_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver juno_fpga_reset_driver = {
	.probe                  = juno_fpga_reset_probe,
	.remove                 = juno_fpga_reset_remove,
	.driver                 = {
		.name           = "juno-fpga-reset",
		.of_match_table = of_match_ptr(juno_fpga_reset_match),
	},
};

module_platform_driver(juno_fpga_reset_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Arm Ltd");
MODULE_DESCRIPTION("Arm Juno FPGA Reset Driver");
MODULE_VERSION(JUNO_FPGA_RESET_DRIVER_VERSION);
