/*
 * (C) COPYRIGHT 2020 ARM Limited. All rights reserved.
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

#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include "ethosu_device.h"

/****************************************************************************
 * Defines
 ****************************************************************************/

#define ETHOSU_DRIVER_VERSION "1.0"
#define ETHOSU_DRIVER_NAME    "ethosu"

/****************************************************************************
 * Variables
 ****************************************************************************/

struct class *ethosu_class;

/****************************************************************************
 * Arm Ethos-U
 ****************************************************************************/

static int ethosu_pdev_probe(struct platform_device *pdev)
{
	struct ethosu_device *edev;
	struct resource *in_queue_res;
	struct resource *out_queue_res;
	int ret;

	dev_info(&pdev->dev, "Probe\n");

	/* Get path to TCM memory */
	in_queue_res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						    "in_queue");
	if (IS_ERR(in_queue_res)) {
		dev_err(&pdev->dev, "Failed to get in_queue resource.\n");

		return PTR_ERR(in_queue_res);
	}

	out_queue_res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						     "out_queue");
	if (IS_ERR(out_queue_res)) {
		dev_err(&pdev->dev, "Failed to get out_queue resource.\n");

		return PTR_ERR(out_queue_res);
	}

	/* Allocate memory for Arm Ethos-U device */
	edev = devm_kzalloc(&pdev->dev, sizeof(*edev), GFP_KERNEL);
	if (!edev)
		return -ENOMEM;

	platform_set_drvdata(pdev, edev);

	/* Initialize device */
	ret = ethosu_dev_init(edev, &pdev->dev, ethosu_class, in_queue_res,
			      out_queue_res);
	if (ret)
		goto free_dev;

	return 0;

free_dev:
	devm_kfree(&pdev->dev, edev);

	return ret;
}

static int ethosu_pdev_remove(struct platform_device *pdev)
{
	struct ethosu_device *edev = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "Remove\n");

	ethosu_dev_deinit(edev);

	return 0;
}

static const struct of_device_id ethosu_pdev_match[] = {
	{ .compatible = "arm,ethosu" },
	{ /* Sentinel */ },
};

MODULE_DEVICE_TABLE(of, ethosu_pdev_match);

static struct platform_driver ethosu_pdev_driver = {
	.probe                  = &ethosu_pdev_probe,
	.remove                 = &ethosu_pdev_remove,
	.driver                 = {
		.name           = ETHOSU_DRIVER_NAME,
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(ethosu_pdev_match),
	},
};

/****************************************************************************
 * Module init and exit
 ****************************************************************************/

static int __init ethosu_init(void)
{
	int ret;

	ethosu_class = class_create(THIS_MODULE, ETHOSU_DRIVER_NAME);
	if (IS_ERR(ethosu_class)) {
		printk("Failed to create class '%s'.\n", ETHOSU_DRIVER_NAME);

		return PTR_ERR(ethosu_class);
	}

	ret = platform_driver_register(&ethosu_pdev_driver);
	if (ret) {
		printk("Failed to register Arm Ethos-U platform driver.\n");
		goto destroy_class;
	}

	return 0;

destroy_class:
	class_destroy(ethosu_class);

	return ret;
}

static void __exit ethosu_exit(void)
{
	platform_driver_unregister(&ethosu_pdev_driver);
	class_destroy(ethosu_class);
}

module_init(ethosu_init)
module_exit(ethosu_exit)
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Arm Ltd");
MODULE_DESCRIPTION("Arm Ethos-U NPU Driver");
MODULE_VERSION(ETHOSU_DRIVER_VERSION);
