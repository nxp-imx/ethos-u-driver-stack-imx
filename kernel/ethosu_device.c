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
#include <linux/slab.h>

/****************************************************************************
 * Defines
 ****************************************************************************/

#define DMA_ADDR_BITS 32 /* Number of address bits */

#define CAPABILITIES_RESP_TIMEOUT_MS 2000

/****************************************************************************
 * Types
 ****************************************************************************/

/****************************************************************************
 * Functions
 ****************************************************************************/

static void ethosu_capabilities_destroy(struct kref *kref)
{
	struct ethosu_capabilities *cap =
		container_of(kref, struct ethosu_capabilities, refcount);

	list_del(&cap->list);

	devm_kfree(cap->edev->dev, cap);
}

static int ethosu_capabilities_find(struct ethosu_capabilities *cap,
				    struct list_head *capabilties_list)
{
	struct ethosu_capabilities *cur;

	list_for_each_entry(cur, capabilties_list, list) {
		if (cur == cap)
			return 0;
	}

	return -EINVAL;
}

static int ethosu_capability_rsp(struct ethosu_device *edev,
				 struct ethosu_core_msg_capabilities_rsp *msg)
{
	struct ethosu_capabilities *cap;
	struct ethosu_uapi_device_capabilities *capabilities;
	int ret;

	cap = (struct ethosu_capabilities *)msg->user_arg;
	ret = ethosu_capabilities_find(cap, &edev->capabilities_list);
	if (0 != ret) {
		dev_warn(edev->dev,
			 "Handle not found in capabilities list. handle=0x%p\n",
			 cap);

		/* NOTE: do not call complete or kref_put on invalid data! */
		return ret;
	}

	capabilities = cap->capabilities;

	capabilities->hw_id.version_status = msg->version_status;
	capabilities->hw_id.version_minor = msg->version_minor;
	capabilities->hw_id.version_major = msg->version_major;
	capabilities->hw_id.product_major = msg->product_major;
	capabilities->hw_id.arch_patch_rev = msg->arch_patch_rev;
	capabilities->hw_id.arch_minor_rev = msg->arch_minor_rev;
	capabilities->hw_id.arch_major_rev = msg->arch_major_rev;
	capabilities->driver_patch_rev = msg->driver_patch_rev;
	capabilities->driver_minor_rev = msg->driver_minor_rev;
	capabilities->driver_major_rev = msg->driver_major_rev;
	capabilities->hw_cfg.macs_per_cc = msg->macs_per_cc;
	capabilities->hw_cfg.cmd_stream_version = msg->cmd_stream_version;
	capabilities->hw_cfg.custom_dma = msg->custom_dma;

	complete(&cap->done);

	kref_put(&cap->refcount, ethosu_capabilities_destroy);

	return 0;
}

/* Incoming messages */
static int ethosu_handle_msg(struct ethosu_device *edev)
{
	int ret;
	struct ethosu_core_msg header;

	union {
		struct ethosu_core_msg_err              error;
		struct ethosu_core_inference_rsp        inf;
		struct ethosu_core_msg_version          version;
		struct ethosu_core_msg_capabilities_rsp capabilities;
	} data;

	/* Read message */
	ret = ethosu_mailbox_read(&edev->mailbox, &header, &data, sizeof(data));
	if (ret)
		return ret;

	switch (header.type) {
	case ETHOSU_CORE_MSG_ERR:
		if (header.length != sizeof(data.error)) {
			dev_warn(edev->dev,
				 "Msg: Error message of incorrect size. size=%u, expected=%zu\n", header.length,
				 sizeof(data.error));
			ret = -EBADMSG;
			break;
		}

		data.error.msg[sizeof(data.error.msg) - 1] = '\0';
		dev_warn(edev->dev, "Msg: Error. type=%u, msg=\"%s\"\n",
			 data.error.type, data.error.msg);
		ret = -EBADMSG;
		break;
	case ETHOSU_CORE_MSG_PING:
		dev_info(edev->dev, "Msg: Ping\n");
		ret = ethosu_mailbox_pong(&edev->mailbox);
		break;
	case ETHOSU_CORE_MSG_PONG:
		dev_info(edev->dev, "Msg: Pong\n");
		break;
	case ETHOSU_CORE_MSG_INFERENCE_RSP:
		if (header.length != sizeof(data.inf)) {
			dev_warn(edev->dev,
				 "Msg: Inference response of incorrect size. size=%u, expected=%zu\n", header.length,
				 sizeof(data.inf));
			ret = -EBADMSG;
			break;
		}

		dev_info(edev->dev,
			 "Msg: Inference response. user_arg=0x%llx, ofm_count=%u, status=%u\n",
			 data.inf.user_arg, data.inf.ofm_count,
			 data.inf.status);
		ethosu_inference_rsp(edev, &data.inf);
		break;
	case ETHOSU_CORE_MSG_VERSION_RSP:
		if (header.length != sizeof(data.version)) {
			dev_warn(edev->dev,
				 "Msg: Version response of incorrect size. size=%u, expected=%zu\n", header.length,
				 sizeof(data.version));
			ret = -EBADMSG;
			break;
		}

		dev_info(edev->dev, "Msg: Version response v%u.%u.%u\n",
			 data.version.major, data.version.minor,
			 data.version.patch);

		/* Check major and minor version match, else return error */
		if (data.version.major != ETHOSU_CORE_MSG_VERSION_MAJOR ||
		    data.version.minor != ETHOSU_CORE_MSG_VERSION_MINOR) {
			dev_warn(edev->dev, "Msg: Version mismatch detected! ");
			dev_warn(edev->dev, "Local version: v%u.%u.%u\n",
				 ETHOSU_CORE_MSG_VERSION_MAJOR,
				 ETHOSU_CORE_MSG_VERSION_MINOR,
				 ETHOSU_CORE_MSG_VERSION_PATCH);
		}

		break;
	case ETHOSU_CORE_MSG_CAPABILITIES_RSP:
		if (header.length != sizeof(data.capabilities)) {
			dev_warn(edev->dev,
				 "Msg: Capabilities response of incorrect size. size=%u, expected=%zu\n", header.length,
				 sizeof(data.capabilities));
			ret = -EBADMSG;
			break;
		}

		dev_info(edev->dev,
			 "Msg: Capabilities response ua%llx vs%hhu v%hhu.%hhu p%hhu av%hhu.%hhu.%hhu dv%hhu.%hhu.%hhu mcc%hhu csv%hhu cd%hhu\n",
			 data.capabilities.user_arg,
			 data.capabilities.version_status,
			 data.capabilities.version_major,
			 data.capabilities.version_minor,
			 data.capabilities.product_major,
			 data.capabilities.arch_major_rev,
			 data.capabilities.arch_minor_rev,
			 data.capabilities.arch_patch_rev,
			 data.capabilities.driver_major_rev,
			 data.capabilities.driver_minor_rev,
			 data.capabilities.driver_patch_rev,
			 data.capabilities.macs_per_cc,
			 data.capabilities.cmd_stream_version,
			 data.capabilities.custom_dma);

		ret = ethosu_capability_rsp(edev, &data.capabilities);
		break;
	default:
		/* This should not happen due to version checks */
		dev_warn(edev->dev, "Msg: Protocol error\n");
		ret = -EPROTO;
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

static int ethosu_send_capabilities_request(struct ethosu_device *edev,
					    void __user *udata)
{
	struct ethosu_uapi_device_capabilities uapi;
	struct ethosu_capabilities *cap;
	int ret;
	int timeout;

	cap = devm_kzalloc(edev->dev, sizeof(struct ethosu_capabilities),
			   GFP_KERNEL);
	if (!cap)
		return -ENOMEM;

	cap->edev = edev;
	cap->capabilities = &uapi;
	kref_init(&cap->refcount);
	init_completion(&cap->done);
	list_add(&cap->list, &edev->capabilities_list);

	ret = ethosu_mailbox_capabilities_request(&edev->mailbox, cap);
	if (0 != ret)
		goto put_kref;

	/*
	 * Increase ref counter since we sent the pointer out to
	 * response handler thread. That thread is responsible to
	 * decrease the ref counter before exiting. So the memory
	 * can be freed.
	 *
	 * NOTE: if no response is received back, the memory is leaked.
	 */
	kref_get(&cap->refcount);
	/* Unlock the mutex before going to block on the condition */
	mutex_unlock(&edev->mutex);
	/* wait for response to arrive back */
	timeout = wait_for_completion_timeout(&cap->done,
					      msecs_to_jiffies(
						      CAPABILITIES_RESP_TIMEOUT_MS));
	/* take back the mutex before resuming to do anything */
	ret = mutex_lock_interruptible(&edev->mutex);
	if (0 != ret)
		goto put_kref;

	if (0 == timeout /* timed out*/) {
		dev_warn(edev->dev,
			 "Msg: Capabilities response lost - timeout\n");
		ret = -EIO;
		goto put_kref;
	}

	ret = copy_to_user(udata, &uapi, sizeof(uapi)) ? -EFAULT : 0;

put_kref:
	kref_put(&cap->refcount, ethosu_capabilities_destroy);

	return ret;
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
	case ETHOSU_IOCTL_VERSION_REQ:
		dev_info(edev->dev, "Ioctl: Send version request\n");
		ret = ethosu_mailbox_version_request(&edev->mailbox);
		break;
	case ETHOSU_IOCTL_CAPABILITIES_REQ:
		dev_info(edev->dev, "Ioctl: Send capabilities request\n");
		ret = ethosu_send_capabilities_request(edev, udata);
		break;
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
		if (ret && ret != -ENOMSG)
			/* Need to start over in case of error, empty the queue
			 * by fast-forwarding read position to write position.
			 * */
			ethosu_mailbox_reset(&edev->mailbox);
	} while (ret == 0);

	mutex_unlock(&edev->mutex);
}

int ethosu_dev_init(struct ethosu_device *edev,
		    struct device *dev,
		    struct class *class,
		    dev_t devt,
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
	edev->devt = devt;
	mutex_init(&edev->mutex);
	INIT_LIST_HEAD(&edev->capabilities_list);
	INIT_LIST_HEAD(&edev->inference_list);

	ret = of_reserved_mem_device_init(edev->dev);
	if (ret)
		return ret;

	dma_set_mask_and_coherent(edev->dev, DMA_BIT_MASK(DMA_ADDR_BITS));

	ret = ethosu_mailbox_init(&edev->mailbox, dev, in_queue, out_queue,
				  ethosu_mbox_rx, edev);
	if (ret)
		goto release_reserved_mem;

	cdev_init(&edev->cdev, &fops);
	edev->cdev.owner = THIS_MODULE;

	ret = cdev_add(&edev->cdev, edev->devt, 1);
	if (ret) {
		dev_err(edev->dev, "Failed to add character device.\n");
		goto deinit_mailbox;
	}

	sysdev = device_create(edev->class, NULL, edev->devt, edev,
			       "ethosu%d", MINOR(edev->devt));
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
	of_reserved_mem_device_release(edev->dev);

	dev_info(edev->dev, "%s\n", __FUNCTION__);
}
