// SPDX-License-Identifier: GPL-2.0
/*
 * Message Handling Unit version 2 controller driver
 * Copyright (C) 2019 ARM Ltd.
 *
 * Based on drivers/mailbox/arm_mhu.c
 *
 */

#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/amba/bus.h>
#include <linux/mailbox_controller.h>
#include <linux/of_device.h>
#include <linux/of_address.h>

#define MHU_V2_REG_STAT_OFS		0x0
#define MHU_V2_REG_CLR_OFS		0x8
#define MHU_V2_REG_SET_OFS		0xC
#define MHU_V2_REG_MSG_NO_CAP_OFS	0xF80
#define MHU_V2_REG_ACC_REQ_OFS		0xF88
#define MHU_V2_REG_ACC_RDY_OFS		0xF8C
#define MHU_V2_INT_EN_OFS		0xF98
#define MHU_V2_AIDR_OFS			0xFCC

#define MHU_V2_CHCOMB			BIT(2)
#define MHU_V2_AIDR_MINOR(_reg)		((_reg) & 0xF)

#define MHU_V2_EACH_CHANNEL_SIZE	0x20

#define mbox_to_arm_mhuv2(c) container_of(c, struct arm_mhuv2, mbox)

struct mhuv2_link {
	unsigned int irq;
	void __iomem *tx_reg;
	void __iomem *rx_reg;
};

struct arm_mhuv2 {
	void __iomem *base;
	struct mhuv2_link *mlink;
	struct mbox_chan *chan;
	struct mbox_controller mbox;
};

static irqreturn_t mhuv2_rx_interrupt(int irq, void *p)
{
	struct mbox_chan *chan = p;
	struct mhuv2_link *mlink = chan->con_priv;
	u32 val;

	val = readl_relaxed(mlink->rx_reg + MHU_V2_REG_STAT_OFS);
	if (!val)
		return IRQ_NONE;

	mbox_chan_received_data(chan, (void *)&val);

	writel_relaxed(val, mlink->rx_reg + MHU_V2_REG_CLR_OFS);

	return IRQ_HANDLED;
}

static bool mhuv2_last_tx_done(struct mbox_chan *chan)
{
	struct mhuv2_link *mlink = chan->con_priv;
	u32 val = readl_relaxed(mlink->tx_reg + MHU_V2_REG_STAT_OFS);

	return (val == 0);
}

static int mhuv2_send_data(struct mbox_chan *chan, void *data)
{
	struct mhuv2_link *mlink = chan->con_priv;
	u32 *arg = data;

	writel_relaxed(*arg, mlink->tx_reg + MHU_V2_REG_SET_OFS);

	return 0;
}

static int mhuv2_startup(struct mbox_chan *chan)
{
	struct mhuv2_link *mlink = chan->con_priv;
	u32 val;
	int ret;
	struct arm_mhuv2 *mhuv2 = mbox_to_arm_mhuv2(chan->mbox);

	writel_relaxed(0x1, mhuv2->base + MHU_V2_REG_ACC_REQ_OFS);

	val = readl_relaxed(mlink->tx_reg + MHU_V2_REG_STAT_OFS);
	writel_relaxed(val, mlink->tx_reg + MHU_V2_REG_CLR_OFS);

	ret = request_irq(mlink->irq, mhuv2_rx_interrupt,
			  IRQF_SHARED, "mhuv2_link", chan);
	if (ret) {
		dev_err(chan->mbox->dev,
			"unable to acquire IRQ %d\n", mlink->irq);
		return ret;
	}

	return 0;
}

static void mhuv2_shutdown(struct mbox_chan *chan)
{
	struct mhuv2_link *mlink = chan->con_priv;
	struct arm_mhuv2 *mhuv2 = mbox_to_arm_mhuv2(chan->mbox);

	writel_relaxed(0x0, mhuv2->base + MHU_V2_REG_ACC_REQ_OFS);

	free_irq(mlink->irq, chan);
}

static const struct mbox_chan_ops mhuv2_ops = {
	.send_data = mhuv2_send_data,
	.startup = mhuv2_startup,
	.shutdown = mhuv2_shutdown,
	.last_tx_done = mhuv2_last_tx_done,
};

void mhuv2_check_enable_cmbint(struct mhuv2_link *link)
{
	const u32 aidr = readl_relaxed(link->rx_reg + MHU_V2_AIDR_OFS);

	if (MHU_V2_AIDR_MINOR(aidr) == 1) {
		// Enable combined receiver interrupt for MHUv2.1
		writel_relaxed(MHU_V2_CHCOMB, link->rx_reg + MHU_V2_INT_EN_OFS);
	}
}

static int mhuv2_probe(struct amba_device *adev, const struct amba_id *id)
{
	int i, err;
	struct arm_mhuv2 *mhuv2;
	struct device *dev = &adev->dev;
	void __iomem *rx_base, *tx_base;
	const struct device_node *np = dev->of_node;
	unsigned int pchans;
	struct mhuv2_link *mlink;
	struct mbox_chan *chan;


	/* Allocate memory for device */
	mhuv2 = devm_kzalloc(dev, sizeof(*mhuv2), GFP_KERNEL);
	if (!mhuv2)
		return -ENOMEM;

	tx_base = of_iomap((struct device_node *)np, 0);
	if (!tx_base) {
		dev_err(dev, "failed to map tx registers\n");
		iounmap(rx_base);
		return -ENOMEM;
	}

	rx_base = of_iomap((struct device_node *)np, 1);
	if (!rx_base) {
		dev_err(dev, "failed to map rx registers\n");
		return -ENOMEM;
	}

	pchans = readl_relaxed(tx_base + MHU_V2_REG_MSG_NO_CAP_OFS);
	if (pchans == 0 || pchans % 2) {
		dev_err(dev, "invalid number of channels %d\n", pchans);
		iounmap(rx_base);
		iounmap(tx_base);
		return -EINVAL;
	}

	mhuv2->mlink = devm_kcalloc(dev, pchans, sizeof(*mlink), GFP_KERNEL);
	if (!mhuv2->mlink) {
		iounmap(rx_base);
		iounmap(tx_base);
		return -ENOMEM;
	}

	mhuv2->chan = devm_kcalloc(dev, pchans, sizeof(*chan), GFP_KERNEL);
	if (!mhuv2->chan) {
		iounmap(rx_base);
		iounmap(tx_base);
		kfree(mhuv2->mlink);
		return -ENOMEM;
	}

	for (i = 0; i < pchans; i++) {
		mlink = mhuv2->mlink + i;
		chan = mhuv2->chan + i;
		chan->con_priv = mlink;
		mlink->rx_reg = rx_base + (i * MHU_V2_EACH_CHANNEL_SIZE);
		mlink->tx_reg = tx_base + (i * MHU_V2_EACH_CHANNEL_SIZE);
	}

	mhuv2->mlink->irq = adev->irq[0];
	mhuv2_check_enable_cmbint(mhuv2->mlink);

	mhuv2->base = tx_base;
	mhuv2->mbox.dev = dev;
	mhuv2->mbox.chans = mhuv2->chan;
	mhuv2->mbox.num_chans = pchans;
	mhuv2->mbox.ops = &mhuv2_ops;
	mhuv2->mbox.txdone_irq = false;
	mhuv2->mbox.txdone_poll = true;
	mhuv2->mbox.txpoll_period = 1;

	amba_set_drvdata(adev, mhuv2);

	err = mbox_controller_register(&mhuv2->mbox);
	if (err) {
		dev_err(dev, "failed to register mailboxes %d\n", err);
		iounmap(rx_base);
		iounmap(tx_base);
		kfree(mhuv2->mlink);
		kfree(mhuv2->chan);
		return err;
	}

	dev_info(dev, "ARM MHUv2 Mailbox driver registered\n");
	return 0;
}

static int mhuv2_remove(struct amba_device *adev)
{
	struct arm_mhuv2 *mhuv2 = amba_get_drvdata(adev);

	mbox_controller_unregister(&mhuv2->mbox);

	return 0;
}

static struct amba_id mhuv2_ids[] = {
	{
		.id     = 0x4b0d1,
		.mask   = 0xfffff,
	},
	{
		.id     = 0xbb0d1,
		.mask   = 0xfffff,
	},
	{
		.id     = 0xbb076,
		.mask   = 0xfffff,
	},
	{ 0, 0 },
};
MODULE_DEVICE_TABLE(amba, mhuv2_ids);

static struct amba_driver arm_mhuv2_driver = {
	.drv = {
		.name	= "mhuv2",
	},
	.id_table	= mhuv2_ids,
	.probe		= mhuv2_probe,
	.remove		= mhuv2_remove,
};
module_amba_driver(arm_mhuv2_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ARM MHUv2 Driver");
MODULE_AUTHOR("Samarth Parikh <samarthp@ymail.com>");
