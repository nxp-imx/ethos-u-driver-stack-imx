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

#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>

#define ETHOSU_RPROC_DRIVER_VERSION "0.0.1"

#define DEFAULT_FW_FILE "arm-ethos-u65.fw"
#define DEFAULT_AUTO_BOOT (false)

/* firmware naming module parameter */
static char fw_filename_param[256] = DEFAULT_FW_FILE;
/* As the remoteproc is setup at probe, just allow the filename readonly */
module_param_string(filename, fw_filename_param, sizeof(fw_filename_param),
		    0444);
MODULE_PARM_DESC(filename,
		 "Filename for firmware image for Ethos-U remoteproc");

static bool auto_boot = DEFAULT_AUTO_BOOT;
module_param(auto_boot, bool, DEFAULT_AUTO_BOOT);
MODULE_PARM_DESC(auto_boot, "Set to one to auto boot at load.");

struct ethosu_rproc {
	struct device            *dev;
	struct reset_control     *rstc;
	struct rproc_mem_mapping *map;
	size_t                   map_size;
};

struct rproc_mem_mapping {
	const char   *name;
	phys_addr_t  rproc_addr;
	void __iomem *vaddr;
	size_t       size;
};

struct ethosu_rproc_config {
	struct fw_config *fw;
};

/*****************************************************************************/

static int ethosu_rproc_start(struct rproc *rproc)
{
	struct ethosu_rproc *ethosu = (struct ethosu_rproc *)rproc->priv;
	struct device *dev = ethosu->dev;

	dev_info(dev, "Starting up Ethos-U subsystem CPU!");

	return reset_control_deassert(ethosu->rstc);
}

static int ethosu_rproc_stop(struct rproc *rproc)
{
	struct ethosu_rproc *ethosu = (struct ethosu_rproc *)rproc->priv;
	struct device *dev = ethosu->dev;

	dev_info(dev, "Stopping Ethos-U subsystem CPU!");

	return reset_control_assert(ethosu->rstc);
}

static void ethosu_rproc_kick(struct rproc *rproc,
			      int vqid)
{
	return;
}

static void *ethosu_da_to_va(struct rproc *rproc,
			     u64 da,
			     int len)
{
	struct ethosu_rproc *ethosu = (struct ethosu_rproc *)rproc->priv;
	int offset;
	int i;

	for (i = 0; i < ethosu->map_size; i++)
		if (da >= ethosu->map[i].rproc_addr &&
		    da < (ethosu->map[i].rproc_addr + ethosu->map[i].size)) {
			offset = da - ethosu->map[i].rproc_addr;
			dev_info(ethosu->dev,
				 "mapping %llx to %p (offset: 0x%x)", da,
				 (void *)(ethosu->map[i].vaddr + offset),
				 offset);

			return (void *)(ethosu->map[i].vaddr + offset);
		}

	return NULL;
}

static const struct rproc_ops ethosu_rproc_ops = {
	.start    = &ethosu_rproc_start,
	.stop     = &ethosu_rproc_stop,
	.kick     = &ethosu_rproc_kick,
	.da_to_va = &ethosu_da_to_va,
};

/**
 * Since the remote side doesn't yet support rpmsg just return an
 * empty resource table when asked about it.
 */
struct resource_table *ethosu_rproc_find_rsc_table(struct rproc *rproc,
						   const struct firmware *fw,
						   int *tablesz)
{
	static struct resource_table table = { .ver = 1, };
	struct ethosu_rproc *ethosu = (struct ethosu_rproc *)rproc->priv;

	dev_info(ethosu->dev, "Sizeof struct resource_table : %zu",
		 sizeof(table));
	*tablesz = sizeof(table);

	return &table;
}

/*****************************************************************************/

static int ethosu_rproc_of_memory_translations(struct platform_device *pdev,
					       struct ethosu_rproc *ethosu_rproc)
{
	const char *const of_rproc_address_cells =
		"#ethosu,rproc-address-cells";
	const char *const of_rproc_ranges = "ethosu,rproc-ranges";
	const char *const of_rproc_ranges_names = "ethosu,rproc-names";

	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct rproc_mem_mapping *mem_map;
	const __be32 *rproc_ranges;

	int addr_cells, rproc_addr_cells, size_cells, cells_for_array_element;
	int i, len, cnt, name_cnt, ret = 0;

	if (of_property_read_u32(np, of_rproc_address_cells,
				 &rproc_addr_cells)) {
		dev_info(dev, "%s not defined in dtb", of_rproc_address_cells);

		return -ENODEV;
	}

	addr_cells = of_n_addr_cells(np);
	size_cells = of_n_size_cells(np);

	dev_dbg(dev, "Using %d remote proc address cells for parsing mapping",
		rproc_addr_cells);
	dev_dbg(dev,
		"Using %d of size %d parent address cells for parsing mapping",
		addr_cells, size_cells);

	cells_for_array_element = addr_cells + rproc_addr_cells + size_cells;

	cnt = of_property_count_elems_of_size(np, of_rproc_ranges,
					      cells_for_array_element);
	cnt /= sizeof(u32);

	if (cnt <= 0) {
		dev_info(dev, "No remoteproc memory mapping ranges found.");

		return 0;
	}

	name_cnt = of_property_count_strings(np, of_rproc_ranges_names);
	if (name_cnt > 0 && name_cnt != cnt) {
		dev_err(dev, "Mismatch length for %s and %s", of_rproc_ranges,
			of_rproc_ranges_names);

		return -EINVAL;
	}

	mem_map = devm_kcalloc(dev, cnt, sizeof(*mem_map), GFP_KERNEL);
	if (!mem_map)
		return -ENOMEM;

	rproc_ranges = of_get_property(np, of_rproc_ranges, &len);

	for (i = 0; i < cnt; i++) {
		struct resource *r;
		const char *name = NULL;
		int n;

		of_property_read_string_index(np, of_rproc_ranges_names, i,
					      &name);
		mem_map[i].name = name;
		n = i * cells_for_array_element;
		mem_map[i].rproc_addr =
			of_read_number(&rproc_ranges[n + addr_cells],
				       rproc_addr_cells);
		mem_map[i].size =
			of_read_number(&rproc_ranges[n + addr_cells +
						     rproc_addr_cells],
				       size_cells);

		r = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
		if (!r) {
			dev_err(&pdev->dev, "Failed to get '%s' resource.\n",
				name);

			return -EINVAL;
		}

		mem_map[i].vaddr = devm_ioremap_wc(dev, r->start,
						   mem_map[i].size);
		if (IS_ERR(mem_map[i].vaddr)) {
			dev_err(dev, "Failed to remap '%s'", name);

			return PTR_ERR(mem_map[i].vaddr);
		}

		dev_dbg(dev,
			"rproc memory mapping[%i]=%s: da %llx, va, %pa, size %zx:\n",
			i, name, mem_map[i].rproc_addr, &mem_map[i].vaddr,
			mem_map[i].size);
	}

	ethosu_rproc->map = mem_map;
	ethosu_rproc->map_size = cnt;
	dev_dbg(dev, "rproc memory mapped %zx regions", ethosu_rproc->map_size);

	return ret;
}

static const struct of_device_id ethosu_rproc_match[] = {
	{ .compatible = "arm,ethosu-rproc" },
};

static int ethosu_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct ethosu_rproc *ethosu_rproc;
	struct rproc *rproc;
	int ret = -ENODEV;

	rproc = rproc_alloc(dev, np->name, &ethosu_rproc_ops,
			    fw_filename_param,
			    sizeof(*ethosu_rproc));
	if (!rproc) {
		ret = -ENOMEM;
		goto out;
	}

	/* Configure rproc */
	rproc->has_iommu = false;
	rproc->auto_boot = auto_boot;

	platform_set_drvdata(pdev, rproc);

	ethosu_rproc = rproc->priv;
	ethosu_rproc->dev = dev;

	/* Get the reset handler for the subsystem */
	ethosu_rproc->rstc = devm_reset_control_get_exclusive_by_index(dev, 0);
	if (IS_ERR(ethosu_rproc->rstc)) {
		dev_err(&pdev->dev, "Failed to get reset controller.\n");
		ret = PTR_ERR(ethosu_rproc->rstc);
		goto free_rproc;
	}

	/* Get the translation from device memory to kernel space */
	ret = ethosu_rproc_of_memory_translations(pdev, ethosu_rproc);
	if (ret)
		goto free_rproc;

	ret = rproc_add(rproc);

free_rproc:
	if (ret)
		rproc_free(rproc);

out:

	return ret;
}

static int ethosu_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);

	rproc_del(rproc);
	rproc_free(rproc);

	return 0;
}

static struct platform_driver ethosu_rproc_driver = {
	.probe                  = ethosu_rproc_probe,
	.remove                 = ethosu_rproc_remove,
	.driver                 = {
		.name           = "ethosu-rproc",
		.of_match_table = of_match_ptr(ethosu_rproc_match),
	},
};

module_platform_driver(ethosu_rproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Arm Ltd");
MODULE_DESCRIPTION("Arm Ethos-U NPU RemoteProc Driver");
MODULE_VERSION(ETHOSU_RPROC_DRIVER_VERSION);
