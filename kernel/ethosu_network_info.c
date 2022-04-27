/*
 * Copyright (c) 2022 ARM Limited.
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

#include "ethosu_network_info.h"

#include "ethosu_device.h"
#include "ethosu_network.h"
#include "ethosu_mailbox.h"
#include "uapi/ethosu.h"

#define NETWORK_INFO_RESP_TIMEOUT_MS 3000

static inline int ethosu_network_info_send(struct ethosu_network_info *info)
{
	/* Send network info request to firmware */
	return ethosu_mailbox_network_info_request(&info->edev->mailbox,
						   &info->msg,
						   info->net->buf,
						   info->net->index);
}

static void ethosu_network_info_fail(struct ethosu_mailbox_msg *msg)
{
	struct ethosu_network_info *info =
		container_of(msg, typeof(*info), msg);

	if (completion_done(&info->done))
		return;

	info->errno = -EFAULT;
	complete(&info->done);
}

static int ethosu_network_info_resend(struct ethosu_mailbox_msg *msg)
{
	struct ethosu_network_info *info =
		container_of(msg, typeof(*info), msg);
	int ret;

	/* Don't resend request if response has already been received */
	if (completion_done(&info->done))
		return 0;

	/* Resend request */
	ret = ethosu_network_info_send(info);
	if (ret)
		return ret;

	return 0;
}

int ethosu_network_info_request(struct ethosu_network *net,
				struct ethosu_uapi_network_info *uapi)
{
	struct ethosu_network_info *info;
	int ret;
	int timeout;

	info = devm_kzalloc(net->edev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->edev = net->edev;
	info->net = net;
	info->uapi = uapi;
	init_completion(&info->done);
	info->msg.fail = ethosu_network_info_fail;
	info->msg.resend = ethosu_network_info_resend;

	ret = ethosu_mailbox_register(&info->edev->mailbox, &info->msg);
	if (ret < 0)
		goto kfree;

	/* Get reference to network */
	ethosu_network_get(info->net);

	ret = ethosu_network_info_send(info);
	if (ret)
		goto deregister;

	dev_info(info->edev->dev, "Network info create. Id=%d, handle=0x%p\n\n",
		 info->msg.id, info);

	/* Unlock the device mutex and wait for completion */
	mutex_unlock(&info->edev->mutex);
	timeout = wait_for_completion_timeout(&info->done,
					      msecs_to_jiffies(
						      NETWORK_INFO_RESP_TIMEOUT_MS));
	mutex_lock(&info->edev->mutex);

	if (0 == timeout) {
		dev_warn(info->edev->dev, "Network info timed out.");

		ret = -ETIME;
		goto deregister;
	}

deregister:
	ethosu_mailbox_deregister(&info->edev->mailbox, &info->msg);
	ethosu_network_put(info->net);

kfree:
	dev_info(info->edev->dev, "Network info destroy. Id=%d, handle=0x%p\n",
		 info->msg.id, info);
	devm_kfree(info->edev->dev, info);

	return ret;
}

void ethosu_network_info_rsp(struct ethosu_device *edev,
			     struct ethosu_core_network_info_rsp *rsp)
{
	int ret;
	int id = (int)rsp->user_arg;
	struct ethosu_mailbox_msg *msg;
	struct ethosu_network_info *info;
	uint32_t i;

	msg = ethosu_mailbox_find(&edev->mailbox, id);
	if (IS_ERR(msg)) {
		dev_warn(edev->dev,
			 "Id for network info msg not found. Id=%d\n",
			 id);

		return;
	}

	info = container_of(msg, typeof(*info), msg);

	if (completion_done(&info->done))
		return;

	info->errno = 0;

	if (rsp->status != ETHOSU_CORE_STATUS_OK) {
		info->errno = -EBADF;
		goto signal_complete;
	}

	if (rsp->ifm_count > ETHOSU_FD_MAX || rsp->ofm_count > ETHOSU_FD_MAX) {
		info->errno = -ENFILE;
		goto signal_complete;
	}

	ret = strscpy(info->uapi->desc, rsp->desc, sizeof(info->uapi->desc));
	if (ret < 0) {
		info->errno = ret;
		goto signal_complete;
	}

	info->uapi->ifm_count = rsp->ifm_count;
	for (i = 0; i < rsp->ifm_count; i++)
		info->uapi->ifm_size[i] = rsp->ifm_size[i];

	info->uapi->ofm_count = rsp->ofm_count;
	for (i = 0; i < rsp->ofm_count; i++)
		info->uapi->ofm_size[i] = rsp->ofm_size[i];

signal_complete:
	complete(&info->done);
}
