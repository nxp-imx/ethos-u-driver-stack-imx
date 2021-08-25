/*
 * Copyright (C) 2013-2015 Fujitsu Semiconductor Ltd.
 * Copyright (C) 2015 Linaro Ltd.
 * Copyright (C) 2020 Arm Ltd.
 * Author: Jassi Brar <jaswinder.singh@linaro.org>
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

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/amba/bus.h>
#include <linux/mailbox_controller.h>
#include <linux/version.h>

struct mhu_register_offsets {
	uint8_t intr_stat_ofs;
	uint8_t intr_set_ofs;
	uint8_t intr_clr_ofs;
};

#define MHU_MAX_CHANS	3
struct mhu_cfg {
	uint32_t id;
	uint8_t channels;
	struct mhu_register_offsets offsets;
	uint16_t tx_offset;
	uint16_t rx_offset;
	uint32_t channel_offsets[MHU_MAX_CHANS];
};

struct mhu_link {
	unsigned irq;
	void __iomem *tx_reg;
	void __iomem *rx_reg;
	struct mhu_register_offsets *offsets;
};

#define MHU_LP_OFFSET	0x0
#define MHU_HP_OFFSET	0x20
#define MHU_SEC_OFFSET	0x200
static struct mhu_cfg mhu_cfgs[] = {
	{
		/* MHUv1 */
		.id = 0x1bb098,
		.channels = 3,
		.offsets = {
			.intr_stat_ofs = 0x0,
			.intr_set_ofs  = 0x8,
			.intr_clr_ofs  = 0x10,
		},
		.tx_offset = 0x100,
		.rx_offset = 0x0,
		.channel_offsets = {MHU_LP_OFFSET, MHU_HP_OFFSET, MHU_SEC_OFFSET}
	},
	{
		/* MHU found on CoreLink SSE200 */
		.id = 0x0bb856,
		.channels = 1,
		.offsets = {
			.intr_stat_ofs = 0x0,
			.intr_set_ofs  = 0x4,
			.intr_clr_ofs  = 0x8,
		},
		.tx_offset = 0x0,
		.rx_offset = 0x10,
		.channel_offsets = {0}
	}
};

struct arm_mhu {
	void __iomem *base;
	struct mhu_link mlink[MHU_MAX_CHANS];
	struct mbox_chan chan[MHU_MAX_CHANS];
	struct mbox_controller mbox;
};

static irqreturn_t mhu_rx_interrupt(int irq, void *p)
{
	struct mbox_chan *chan = p;
	struct mhu_link *mlink = chan->con_priv;
	u32 val;

	val = readl_relaxed(mlink->rx_reg + mlink->offsets->intr_stat_ofs);
	if (!val)
		return IRQ_NONE;

	mbox_chan_received_data(chan, (void *)&val);

	writel_relaxed(val, mlink->rx_reg + mlink->offsets->intr_clr_ofs);

	return IRQ_HANDLED;
}

static bool mhu_last_tx_done(struct mbox_chan *chan)
{
	struct mhu_link *mlink = chan->con_priv;
	u32 val = readl_relaxed(mlink->tx_reg + mlink->offsets->intr_stat_ofs);

	return (val == 0);
}

static int mhu_send_data(struct mbox_chan *chan, void *data)
{
	struct mhu_link *mlink = chan->con_priv;
	u32 *arg = data;

	writel_relaxed(*arg, mlink->tx_reg + mlink->offsets->intr_set_ofs);

	return 0;
}

static int mhu_startup(struct mbox_chan *chan)
{
	struct mhu_link *mlink = chan->con_priv;
	u32 val;
	int ret;

	val = readl_relaxed(mlink->tx_reg + mlink->offsets->intr_stat_ofs);
	writel_relaxed(val, mlink->tx_reg + mlink->offsets->intr_clr_ofs);

	ret = request_irq(mlink->irq, mhu_rx_interrupt,
			  IRQF_SHARED, "mhu_link", chan);
	if (ret) {
		dev_err(chan->mbox->dev,
			"Unable to acquire IRQ %d\n", mlink->irq);
		return ret;
	}

	return 0;
}

static void mhu_shutdown(struct mbox_chan *chan)
{
	struct mhu_link *mlink = chan->con_priv;

	free_irq(mlink->irq, chan);
}

static const struct mbox_chan_ops mhu_ops = {
	.send_data = mhu_send_data,
	.startup = mhu_startup,
	.shutdown = mhu_shutdown,
	.last_tx_done = mhu_last_tx_done,
};

static int mhu_probe(struct amba_device *adev, const struct amba_id *id)
{
	int i, err;
	struct arm_mhu *mhu;
	struct device *dev = &adev->dev;
	struct mhu_cfg *cfg = NULL;

	/* Allocate memory for device */
	mhu = devm_kzalloc(dev, sizeof(*mhu), GFP_KERNEL);
	if (!mhu)
		return -ENOMEM;

	mhu->base = devm_ioremap_resource(dev, &adev->res);
	if (IS_ERR(mhu->base)) {
		dev_err(dev, "ioremap failed\n");
		return PTR_ERR(mhu->base);
	}

	for (i = 0; i < ARRAY_SIZE(mhu_cfgs); i++) {
		if ((mhu_cfgs[i].id & id->mask) == id->id) {
			cfg = &mhu_cfgs[i];
			break;
		}
	}

	if (!cfg) {
		dev_err(dev, "Failed to match id %x to configuration\n", id->id);
		return -EINVAL;
	}

	for (i = 0; i < cfg->channels; i++) {
		mhu->chan[i].con_priv = &mhu->mlink[i];
		mhu->mlink[i].irq = adev->irq[i];
		mhu->mlink[i].rx_reg = mhu->base + cfg->channel_offsets[i]
			+ cfg->rx_offset;
		mhu->mlink[i].tx_reg = mhu->base + cfg->channel_offsets[i]
			+ cfg->tx_offset;
		mhu->mlink[i].offsets = &cfg->offsets;
	}

	mhu->mbox.dev = dev;
	mhu->mbox.chans = &mhu->chan[0];
	mhu->mbox.num_chans = cfg->channels;
	mhu->mbox.ops = &mhu_ops;
	mhu->mbox.txdone_irq = false;
	mhu->mbox.txdone_poll = true;
	mhu->mbox.txpoll_period = 1;

	amba_set_drvdata(adev, mhu);

	err = mbox_controller_register(&mhu->mbox);
	if (err) {
		dev_err(dev, "Failed to register mailboxes %d\n", err);
		return err;
	}

	dev_info(dev, "ARM MHU Mailbox registered\n");
	return 0;
}

#if KERNEL_VERSION(5, 12, 0) <= LINUX_VERSION_CODE
static void mhu_remove(struct amba_device *adev)
{
	struct arm_mhu *mhu = amba_get_drvdata(adev);

	mbox_controller_unregister(&mhu->mbox);
}
#else
static int mhu_remove(struct amba_device *adev)
{
	struct arm_mhu *mhu = amba_get_drvdata(adev);

	mbox_controller_unregister(&mhu->mbox);

	return 0;
}
#endif


static struct amba_id mhu_ids[] = {
	{
		.id	= 0x1bb098,
		.mask	= 0xffffff,
	},
	{
		.id	= 0x0bb856,
		.mask	= 0xffffff,
	},
	{ 0, 0 },
};
MODULE_DEVICE_TABLE(amba, mhu_ids);

static struct amba_driver arm_mhu_driver = {
	.drv = {
		/* Change name from "mhu" to "mhu_v1" to avoid conflict with
		 * upstream version of kernel module.
		 */
		.name	= "mhu_v1",
	},
	.id_table	= mhu_ids,
	.probe		= mhu_probe,
	.remove		= mhu_remove,
};
module_amba_driver(arm_mhu_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ARM MHU Driver");
MODULE_AUTHOR("Jassi Brar <jassisinghbrar@gmail.com>");
