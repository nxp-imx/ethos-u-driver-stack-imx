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

/****************************************************************************
 * Includes
 ****************************************************************************/

#include "ethosu_device.h"

#include "ethosu_buffer.h"
#include "ethosu_core_interface.h"
#include "ethosu_inference.h"
#include "ethosu_network.h"
#include "uapi/ethosu.h"

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_reserved_mem.h>
#include <linux/uaccess.h>

/****************************************************************************
 * Defines
 ****************************************************************************/

#define MINOR_VERSION  0 /* Minor version starts at 0 */
#define MINOR_COUNT    1 /* Allocate 1 minor version */
#define DMA_ADDR_BITS 32 /* Number of address bits */

/****************************************************************************
 * Types
 ****************************************************************************/

/****************************************************************************
 * Functions
 ****************************************************************************/

static int ethosu_handle_msg(struct ethosu_device *edev)
{
	struct ethosu_core_msg header;

	union {
		struct ethosu_core_inference_rsp inf;
	} data;
	int ret;

	/* Read message */
	ret = ethosu_mailbox_read(&edev->mailbox, &header, &data, sizeof(data));
	if (ret)
		return ret;

	switch (header.type) {
	case ETHOSU_CORE_MSG_PING:
		dev_info(edev->dev, "Msg: Ping\n");
		ret = ethosu_mailbox_ping(&edev->mailbox);
		break;
	case ETHOSU_CORE_MSG_PONG:
		dev_info(edev->dev, "Msg: Pong\n");
		break;
	case ETHOSU_CORE_MSG_INFERENCE_RSP:
		dev_info(edev->dev,
			 "Msg: Inference response. user_arg=0x%llx, ofm_count=%u, status=%u\n",
			 data.inf.user_arg, data.inf.ofm_count,
			 data.inf.status);
		ethosu_inference_rsp(edev, &data.inf);
		break;
	default:
		dev_warn(edev->dev,
			 "Msg: Unsupported msg type. type=%u, length=%u",
			 header.type, header.length);
		break;
	}

	return ret;
}

static int ethosu_open(struct inode *inode,
		       struct file *file)
{
	struct ethosu_device *edev =
		container_of(inode->i_cdev, struct ethosu_device, cdev);

	file->private_data = edev;

	dev_info(edev->dev, "Opening device node.\n");

	return nonseekable_open(inode, file);
}

static long ethosu_ioctl(struct file *file,
			 unsigned int cmd,
			 unsigned long arg)
{
	struct ethosu_device *edev = file->private_data;
	void __user *udata = (void __user *)arg;
	int ret = -EINVAL;

	ret = mutex_lock_interruptible(&edev->mutex);
	if (ret)
		return ret;

	dev_info(edev->dev, "Ioctl. cmd=%u, arg=%lu\n", cmd, arg);

	switch (cmd) {
	case ETHOSU_IOCTL_PING: {
		dev_info(edev->dev, "Ioctl: Send ping\n");
		ret = ethosu_mailbox_ping(&edev->mailbox);
		break;
	}
	case ETHOSU_IOCTL_BUFFER_CREATE: {
		struct ethosu_uapi_buffer_create uapi;

		dev_info(edev->dev, "Ioctl: Buffer create\n");

		if (copy_from_user(&uapi, udata, sizeof(uapi)))
			break;

		dev_info(edev->dev, "Ioctl: Buffer. capacity=%u\n",
			 uapi.capacity);

		ret = ethosu_buffer_create(edev, uapi.capacity);
		break;
	}
	case ETHOSU_IOCTL_NETWORK_CREATE: {
		struct ethosu_uapi_network_create uapi;

		if (copy_from_user(&uapi, udata, sizeof(uapi)))
			break;

		dev_info(edev->dev, "Ioctl: Network. fd=%u\n", uapi.fd);

		ret = ethosu_network_create(edev, &uapi);
		break;
	}
	default: {
		dev_err(edev->dev, "Invalid ioctl. cmd=%u, arg=%lu",
			cmd, arg);
		break;
	}
	}

	mutex_unlock(&edev->mutex);

	return ret;
}

static void ethosu_mbox_rx(void *user_arg)
{
	struct ethosu_device *edev = user_arg;
	int ret;

	mutex_lock(&edev->mutex);

	do {
		ret = ethosu_handle_msg(edev);
	} while (ret == 0);

	mutex_unlock(&edev->mutex);
}

int ethosu_dev_init(struct ethosu_device *edev,
		    struct device *dev,
		    struct class *class,
		    struct resource *in_queue,
		    struct resource *out_queue)
{
	static const struct file_operations fops = {
		.owner          = THIS_MODULE,
		.open           = &ethosu_open,
		.unlocked_ioctl = &ethosu_ioctl,
#ifdef CONFIG_COMPAT
		.compat_ioctl   = &ethosu_ioctl,
#endif
	};
	struct device *sysdev;
	int ret;

	edev->dev = dev;
	edev->class = class;
	mutex_init(&edev->mutex);
	INIT_LIST_HEAD(&edev->inference_list);

	ret = of_reserved_mem_device_init(edev->dev);
	if (ret)
		return ret;

	dma_set_mask_and_coherent(edev->dev, DMA_BIT_MASK(DMA_ADDR_BITS));

	ret = ethosu_mailbox_init(&edev->mailbox, dev, in_queue, out_queue,
				  ethosu_mbox_rx, edev);
	if (ret)
		goto release_reserved_mem;

	ret = alloc_chrdev_region(&edev->devt, MINOR_VERSION, MINOR_COUNT,
				  "ethosu");
	if (ret) {
		dev_err(edev->dev, "Failed to allocate chrdev region.\n");
		goto deinit_mailbox;
	}

	cdev_init(&edev->cdev, &fops);
	edev->cdev.owner = THIS_MODULE;

	ret = cdev_add(&edev->cdev, edev->devt, MINOR_COUNT);
	if (ret) {
		dev_err(edev->dev, "Failed to add character device.\n");
		goto region_unregister;
	}

	sysdev = device_create(edev->class, NULL, edev->devt, edev,
			       "ethosu%d", MAJOR(edev->devt));
	if (IS_ERR(sysdev)) {
		dev_err(edev->dev, "Failed to create device.\n");
		ret = PTR_ERR(sysdev);
		goto del_cdev;
	}

	dev_info(edev->dev,
		 "Created Arm Ethos-U device. name=%s, major=%d, minor=%d\n",
		 dev_name(sysdev), MAJOR(edev->devt), MINOR(edev->devt));

	return 0;

del_cdev:
	cdev_del(&edev->cdev);

region_unregister:
	unregister_chrdev_region(edev->devt, 1);

deinit_mailbox:
	ethosu_mailbox_deinit(&edev->mailbox);

release_reserved_mem:
	of_reserved_mem_device_release(edev->dev);

	return ret;
}

void ethosu_dev_deinit(struct ethosu_device *edev)
{
	ethosu_mailbox_deinit(&edev->mailbox);
	device_destroy(edev->class, edev->cdev.dev);
	cdev_del(&edev->cdev);
	unregister_chrdev_region(edev->devt, MINOR_COUNT);
	of_reserved_mem_device_release(edev->dev);

	dev_info(edev->dev, "%s\n", __FUNCTION__);
}
