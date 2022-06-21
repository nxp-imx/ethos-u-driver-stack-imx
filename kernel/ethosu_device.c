/*
 * Copyright (c) 2020-2022 Arm Limited.
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
#include "ethosu_capabilities.h"
#include "ethosu_inference.h"
#include "ethosu_cancel_inference.h"
#include "ethosu_network.h"
#include "ethosu_network_info.h"
#include "uapi/ethosu.h"

#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_reserved_mem.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

/****************************************************************************
 * Defines
 ****************************************************************************/

#define DMA_ADDR_BITS 32 /* Number of address bits */

/****************************************************************************
 * Functions
 ****************************************************************************/

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
		struct ethosu_core_network_info_rsp     network_info;
		struct ethosu_core_cancel_inference_rsp cancellation;
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
	case ETHOSU_CORE_MSG_CANCEL_INFERENCE_RSP:
		if (header.length != sizeof(data.cancellation)) {
			dev_warn(edev->dev,
				 "Msg: Cancel Inference response of incorrect size. size=%u, expected=%zu\n", header.length,
				 sizeof(data.cancellation));
			ret = -EBADMSG;
			break;
		}

		dev_info(edev->dev,
			 "Msg: Cancel Inference response. user_arg=0x%llx, status=%u\n",
			 data.cancellation.user_arg, data.cancellation.status);
		ethosu_cancel_inference_rsp(edev, &data.cancellation);
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

		ethosu_capability_rsp(edev, &data.capabilities);
		break;
	case ETHOSU_CORE_MSG_NETWORK_INFO_RSP:
		if (header.length != sizeof(data.network_info)) {
			dev_warn(edev->dev,
				 "Msg: Network info response of incorrect size. size=%u, expected=%zu\n", header.length,
				 sizeof(data.network_info));
			ret = -EBADMSG;
			break;
		}

		dev_info(edev->dev,
			 "Msg: Network info response. user_arg=0x%llx, status=%u",
			 data.network_info.user_arg,
			 data.network_info.status);

		ethosu_network_info_rsp(edev, &data.network_info);

		break;
	default:
		/* This should not happen due to version checks */
		dev_warn(edev->dev, "Msg: Protocol error\n");
		ret = -EPROTO;
		break;
	}

	return ret;
}

int ethosu_firmware_reset(struct ethosu_device *edev)
{
	int ret;

	/* No reset control for this device */
	if (IS_ERR(edev->reset))
		return PTR_ERR(edev->reset);

	dev_info(edev->dev, "Resetting firmware.");

	ret = reset_control_assert(edev->reset);
	if (ret) {
		dev_err(edev->dev, "Failed to reset assert firmware. ret=%d",
			ret);

		return ret;
	}

	/* Initialize mailbox header with illegal values */
	ethosu_mailbox_wait_prepare(&edev->mailbox);

	/* If this call fails we have a problem. We managed halt the firmware,
	 * but not to release the reset.
	 */
	ret = reset_control_deassert(edev->reset);
	if (ret) {
		dev_err(edev->dev, "Failed to reset deassert firmware. ret=%d",
			ret);
		goto fail;
	}

	/* Wait for firmware to boot up and initialize mailbox */
	ret = ethosu_mailbox_wait_firmware(&edev->mailbox);
	if (ret) {
		dev_err(edev->dev, "Wait on firmware boot timed out. ret=%d",
			ret);
		goto fail;
	}

	edev->mailbox.ping_count = 0;
	ethosu_watchdog_reset(&edev->watchdog);

	ret = ethosu_mailbox_ping(&edev->mailbox);
	if (ret) {
		dev_warn(edev->dev,
			 "Failed to send ping after firmware reset. ret=%d",
			 ret);
		goto fail;
	}

	/* Resend messages */
	ethosu_mailbox_resend(&edev->mailbox);

	return ret;

fail:
	ethosu_mailbox_fail(&edev->mailbox);

	return ret;
}

static void ethosu_watchdog_callback(struct ethosu_watchdog *wdog)
{
	struct ethosu_device *edev =
		container_of(wdog, struct ethosu_device, watchdog);

	mutex_lock(&edev->mutex);

	dev_warn(edev->dev, "Device watchdog timeout. ping_count=%u",
		 edev->mailbox.ping_count);

	if (edev->mailbox.ping_count < 1)
		ethosu_mailbox_ping(&edev->mailbox);
	else
		ethosu_firmware_reset(edev);

	mutex_unlock(&edev->mutex);
}

static int ethosu_open(struct inode *inode,
		       struct file *file)
{
	struct ethosu_device *edev =
		container_of(inode->i_cdev, struct ethosu_device, cdev);

	file->private_data = edev;

	dev_info(edev->dev, "Device open. file=0x%pK\n", file);

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

	dev_info(edev->dev, "Device ioctl. file=0x%pK, cmd=0x%x, arg=0x%lx\n",
		 file, cmd, arg);

	switch (cmd) {
	case ETHOSU_IOCTL_VERSION_REQ:
		dev_info(edev->dev, "Device ioctl: Send version request\n");
		ret = ethosu_mailbox_version_request(&edev->mailbox);
		break;
	case ETHOSU_IOCTL_CAPABILITIES_REQ: {
		struct ethosu_uapi_device_capabilities uapi;

		dev_info(edev->dev,
			 "Device ioctl: Send capabilities request\n");

		ret = ethosu_capabilities_request(edev, &uapi);
		if (ret)
			break;

		ret = copy_to_user(udata, &uapi, sizeof(uapi)) ? -EFAULT : 0;
		break;
	}
	case ETHOSU_IOCTL_PING: {
		dev_info(edev->dev, "Device ioctl: Send ping\n");
		ret = ethosu_mailbox_ping(&edev->mailbox);
		break;
	}
	case ETHOSU_IOCTL_BUFFER_CREATE: {
		struct ethosu_uapi_buffer_create uapi;

		if (copy_from_user(&uapi, udata, sizeof(uapi)))
			break;

		dev_info(edev->dev,
			 "Device ioctl: Buffer create. capacity=%u\n",
			 uapi.capacity);

		ret = ethosu_buffer_create(edev, uapi.capacity);
		break;
	}
	case ETHOSU_IOCTL_NETWORK_CREATE: {
		struct ethosu_uapi_network_create uapi;

		if (copy_from_user(&uapi, udata, sizeof(uapi)))
			break;

		dev_info(edev->dev,
			 "Device ioctl: Network create. type=%u, fd/index=%u\n",
			 uapi.type, uapi.fd);

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

	edev->reset = devm_reset_control_get_by_index(edev->dev, 0);
	if (IS_ERR(edev->reset))
		dev_warn(edev->dev, "No reset control found for this device.");

	ret = of_reserved_mem_device_init(edev->dev);
	if (ret)
		return ret;

	dma_set_mask_and_coherent(edev->dev, DMA_BIT_MASK(DMA_ADDR_BITS));

	ret = ethosu_watchdog_init(&edev->watchdog, dev,
				   ethosu_watchdog_callback);
	if (ret)
		goto release_reserved_mem;

	ret = ethosu_mailbox_init(&edev->mailbox, dev, in_queue, out_queue,
				  ethosu_mbox_rx, edev, &edev->watchdog);
	if (ret)
		goto deinit_watchdog;

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

	ethosu_firmware_reset(edev);

	dev_info(edev->dev,
		 "Created Arm Ethos-U device. name=%s, major=%d, minor=%d\n",
		 dev_name(sysdev), MAJOR(edev->devt), MINOR(edev->devt));

	return 0;

del_cdev:
	cdev_del(&edev->cdev);

deinit_mailbox:
	ethosu_mailbox_deinit(&edev->mailbox);

deinit_watchdog:
	ethosu_watchdog_deinit(&edev->watchdog);

release_reserved_mem:
	of_reserved_mem_device_release(edev->dev);

	return ret;
}

void ethosu_dev_deinit(struct ethosu_device *edev)
{
	ethosu_mailbox_deinit(&edev->mailbox);
	ethosu_watchdog_deinit(&edev->watchdog);
	device_destroy(edev->class, edev->cdev.dev);
	cdev_del(&edev->cdev);
	of_reserved_mem_device_release(edev->dev);

	dev_info(edev->dev, "%s\n", __FUNCTION__);
}
