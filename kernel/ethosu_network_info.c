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

/****************************************************************************
 * Functions
 ****************************************************************************/

static void ethosu_network_info_destroy(struct kref *kref)
{
	struct ethosu_network_info *info =
		container_of(kref, struct ethosu_network_info, kref);

	dev_info(info->edev->dev, "Network info destroy. handle=0x%pK\n", info);

	list_del(&info->msg.list);

	ethosu_network_put(info->net);

	devm_kfree(info->edev->dev, info);
}

static int ethosu_network_info_send(struct ethosu_network_info *info)
{
	int ret;

	/* Send network info request to firmware */
	ret = ethosu_mailbox_network_info_request(&info->edev->mailbox,
						  info,
						  info->net->buf,
						  info->net->index);
	if (ret)
		return ret;

	return 0;
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

struct ethosu_network_info *ethosu_network_info_create(
	struct ethosu_device *edev,
	struct ethosu_network *net,
	struct ethosu_uapi_network_info *uapi)
{
	struct ethosu_network_info *info;
	int ret;

	info = devm_kzalloc(edev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	info->edev = edev;
	info->net = net;
	info->uapi = uapi;
	kref_init(&info->kref);
	init_completion(&info->done);
	info->msg.fail = ethosu_network_info_fail;
	info->msg.resend = ethosu_network_info_resend;

	/* Insert network info to network info list */
	list_add(&info->msg.list, &edev->mailbox.pending_list);

	/* Get reference to network */
	ethosu_network_get(net);

	ret = ethosu_network_info_send(info);
	if (ret)
		goto put_info;

	dev_info(edev->dev, "Network info create. handle=%p\n", info);

	return info;

put_info:
	ethosu_network_info_put(info);

	return ERR_PTR(ret);
}

void ethosu_network_info_get(struct ethosu_network_info *info)
{
	kref_get(&info->kref);
}

int ethosu_network_info_put(struct ethosu_network_info *info)
{
	return kref_put(&info->kref, ethosu_network_info_destroy);
}

int ethosu_network_info_wait(struct ethosu_network_info *info,
			     int timeout_ms)
{
	int timeout;

	timeout = wait_for_completion_timeout(&info->done,
					      msecs_to_jiffies(timeout_ms));

	if (!timeout) {
		dev_warn(info->edev->dev,
			 "Network info timed out.");

		return -ETIME;
	}

	return info->errno;
}

void ethosu_network_info_rsp(struct ethosu_device *edev,
			     struct ethosu_core_network_info_rsp *rsp)
{
	struct ethosu_network_info *info =
		(struct ethosu_network_info *)rsp->user_arg;
	uint32_t i;
	int ret;

	ret = ethosu_mailbox_find(&edev->mailbox, &info->msg);
	if (0 != ret) {
		dev_warn(edev->dev,
			 "Handle not found in network info list. handle=0x%p\n",
			 info);

		return;
	}

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

	strncpy(info->uapi->desc, rsp->desc, sizeof(info->uapi->desc));

	info->uapi->ifm_count = rsp->ifm_count;
	for (i = 0; i < rsp->ifm_count; i++)
		info->uapi->ifm_size[i] = rsp->ifm_size[i];

	info->uapi->ofm_count = rsp->ofm_count;
	for (i = 0; i < rsp->ofm_count; i++)
		info->uapi->ofm_size[i] = rsp->ofm_size[i];

signal_complete:
	complete(&info->done);
}
