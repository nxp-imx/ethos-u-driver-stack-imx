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

#include "ethosu_inference.h"

#include "ethosu_buffer.h"
#include "ethosu_core_interface.h"
#include "ethosu_device.h"
#include "ethosu_network.h"
#include "uapi/ethosu.h"

#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/poll.h>

/****************************************************************************
 * Variables
 ****************************************************************************/

static int ethosu_inference_release(struct inode *inode,
				    struct file *file);

static unsigned int ethosu_inference_poll(struct file *file,
					  poll_table *wait);

static long ethosu_inference_ioctl(struct file *file,
				   unsigned int cmd,
				   unsigned long arg);

static const struct file_operations ethosu_inference_fops = {
	.release        = &ethosu_inference_release,
	.poll           = &ethosu_inference_poll,
	.unlocked_ioctl = &ethosu_inference_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = &ethosu_inference_ioctl,
#endif
};

/****************************************************************************
 * Functions
 ****************************************************************************/

static const char *status_to_string(const enum ethosu_uapi_status status)
{
	switch (status) {
	case ETHOSU_UAPI_STATUS_OK: {
		return "Ok";
	}
	case ETHOSU_UAPI_STATUS_ERROR: {
		return "Error";
	}
	default: {
		return "Unknown";
	}
	}
}

static int ethosu_inference_send(struct ethosu_inference *inf)
{
	int ret;

	if (inf->pending)
		return -EINVAL;

	inf->status = ETHOSU_UAPI_STATUS_ERROR;

	ret = ethosu_mailbox_inference(&inf->edev->mailbox, inf, inf->ifm,
				       inf->ofm, inf->net->buf);
	if (ret)
		return ret;

	inf->pending = true;

	ethosu_inference_get(inf);

	return 0;
}

static int ethosu_inference_find(struct ethosu_inference *inf,
				 struct list_head *inference_list)
{
	struct ethosu_inference *cur;

	list_for_each_entry(cur, inference_list, list) {
		if (cur == inf)
			return 0;
	}

	return -EINVAL;
}

static bool ethosu_inference_verify(struct file *file)
{
	return file->f_op == &ethosu_inference_fops;
}

static void ethosu_inference_kref_destroy(struct kref *kref)
{
	struct ethosu_inference *inf =
		container_of(kref, struct ethosu_inference, kref);

	dev_info(inf->edev->dev,
		 "Inference destroy. handle=0x%pK, status=%d\n",
		 inf, inf->status);

	list_del(&inf->list);
	ethosu_buffer_put(inf->ifm);
	ethosu_buffer_put(inf->ofm);
	ethosu_network_put(inf->net);
	devm_kfree(inf->edev->dev, inf);
}

static int ethosu_inference_release(struct inode *inode,
				    struct file *file)
{
	struct ethosu_inference *inf = file->private_data;

	dev_info(inf->edev->dev,
		 "Inference release. handle=0x%pK, status=%d\n",
		 inf, inf->status);

	ethosu_inference_put(inf);

	return 0;
}

static unsigned int ethosu_inference_poll(struct file *file,
					  poll_table *wait)
{
	struct ethosu_inference *inf = file->private_data;
	int ret = 0;

	poll_wait(file, &inf->waitq, wait);

	if (!inf->pending)
		ret |= POLLIN;

	return ret;
}

static long ethosu_inference_ioctl(struct file *file,
				   unsigned int cmd,
				   unsigned long arg)
{
	struct ethosu_inference *inf = file->private_data;
	int ret = -EINVAL;

	ret = mutex_lock_interruptible(&inf->edev->mutex);
	if (ret)
		return ret;

	dev_info(inf->edev->dev, "Ioctl: cmd=%u, arg=%lu\n", cmd, arg);

	switch (cmd) {
	case ETHOSU_IOCTL_INFERENCE_STATUS: {
		ret = inf->status;

		dev_info(inf->edev->dev,
			 "Ioctl: Inference status. status=%s (%d)\n",
			 status_to_string(ret), ret);
		break;
	}
	default: {
		dev_err(inf->edev->dev, "Invalid ioctl. cmd=%u, arg=%lu",
			cmd, arg);
		break;
	}
	}

	mutex_unlock(&inf->edev->mutex);

	return ret;
}

int ethosu_inference_create(struct ethosu_device *edev,
			    struct ethosu_network *net,
			    struct ethosu_uapi_inference_create *uapi)
{
	struct ethosu_inference *inf;
	int fd;
	int ret = -ENOMEM;

	inf = devm_kzalloc(edev->dev, sizeof(*inf), GFP_KERNEL);
	if (!inf)
		return -ENOMEM;

	inf->edev = edev;
	inf->net = net;
	inf->pending = false;
	inf->status = ETHOSU_UAPI_STATUS_ERROR;
	kref_init(&inf->kref);
	init_waitqueue_head(&inf->waitq);

	/* Get pointer to IFM buffer */
	inf->ifm = ethosu_buffer_get_from_fd(uapi->ifm_fd);
	if (IS_ERR(inf->ifm)) {
		ret = PTR_ERR(inf->ifm);
		goto free_inf;
	}

	/* Get pointer to OFM buffer */
	inf->ofm = ethosu_buffer_get_from_fd(uapi->ofm_fd);
	if (IS_ERR(inf->ofm)) {
		ret = PTR_ERR(inf->ofm);
		goto put_ifm;
	}

	/* Increment network reference count */
	ethosu_network_get(net);

	/* Create file descriptor */
	ret = fd = anon_inode_getfd("ethosu-inference", &ethosu_inference_fops,
				    inf, O_RDWR | O_CLOEXEC);
	if (ret < 0)
		goto put_net;

	/* Store pointer to file structure */
	inf->file = fget(ret);
	fput(inf->file);

	/* Add inference to inference list */
	list_add(&inf->list, &edev->inference_list);

	/* Send inference request to Arm Ethos-U subsystem */
	(void)ethosu_inference_send(inf);

	dev_info(edev->dev, "Inference create. handle=0x%pK, fd=%d",
		 inf, fd);

	return fd;

put_net:
	ethosu_network_put(inf->net);
	ethosu_buffer_put(inf->ofm);

put_ifm:
	ethosu_buffer_put(inf->ifm);

free_inf:
	devm_kfree(edev->dev, inf);

	return ret;
}

struct ethosu_inference *ethosu_inference_get_from_fd(int fd)
{
	struct ethosu_inference *inf;
	struct file *file;

	file = fget(fd);
	if (!file)
		return ERR_PTR(-EINVAL);

	if (!ethosu_inference_verify(file)) {
		fput(file);

		return ERR_PTR(-EINVAL);
	}

	inf = file->private_data;
	ethosu_inference_get(inf);
	fput(file);

	return inf;
}

void ethosu_inference_get(struct ethosu_inference *inf)
{
	kref_get(&inf->kref);
}

void ethosu_inference_put(struct ethosu_inference *inf)
{
	kref_put(&inf->kref, &ethosu_inference_kref_destroy);
}

void ethosu_inference_rsp(struct ethosu_device *edev,
			  struct ethosu_core_inference_rsp *rsp)
{
	struct ethosu_inference *inf =
		(struct ethosu_inference *)rsp->user_arg;
	int ret;

	ret = ethosu_inference_find(inf, &edev->inference_list);
	if (ret) {
		dev_warn(edev->dev,
			 "Handle not found in inference list. handle=0x%p\n",
			 rsp);

		return;
	}

	inf->pending = false;

	if (rsp->status == ETHOSU_CORE_STATUS_OK) {
		inf->status = ETHOSU_UAPI_STATUS_OK;

		ret = ethosu_buffer_resize(inf->ofm,
					   inf->ofm->size + rsp->ofm_size,
					   inf->ofm->offset);
		if (ret)
			inf->status = ETHOSU_UAPI_STATUS_ERROR;
	} else {
		inf->status = ETHOSU_UAPI_STATUS_ERROR;
	}

	wake_up_interruptible(&inf->waitq);

	ethosu_inference_put(inf);
}
