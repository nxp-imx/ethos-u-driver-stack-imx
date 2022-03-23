/*
 * Copyright (c) 2022 Arm Limited.
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

#include "ethosu_cancel_inference.h"

#include "ethosu_core_interface.h"
#include "ethosu_device.h"
#include "ethosu_inference.h"

#include <linux/wait.h>

/****************************************************************************
 * Defines
 ****************************************************************************/

#define CANCEL_INFERENCE_RESP_TIMEOUT_MS 2000

/****************************************************************************
 * Functions
 ****************************************************************************/

static void ethosu_cancel_inference_destroy(struct kref *kref)
{
	struct ethosu_cancel_inference *cancellation =
		container_of(kref, struct ethosu_cancel_inference, kref);

	dev_info(cancellation->edev->dev,
		 "Cancel inference destroy. handle=0x%p\n", cancellation);
	list_del(&cancellation->msg.list);
	/* decrease the reference on the inference we are refering to */
	ethosu_inference_put(cancellation->inf);
	devm_kfree(cancellation->edev->dev, cancellation);
}

static int ethosu_cancel_inference_send(
	struct ethosu_cancel_inference *cancellation)
{
	return ethosu_mailbox_cancel_inference(&cancellation->edev->mailbox,
					       cancellation, cancellation->inf);
}

static void ethosu_cancel_inference_fail(struct ethosu_mailbox_msg *msg)
{
	struct ethosu_cancel_inference *cancellation =
		container_of(msg, typeof(*cancellation), msg);

	if (completion_done(&cancellation->done))
		return;

	cancellation->errno = -EFAULT;
	cancellation->uapi->status = ETHOSU_UAPI_STATUS_ERROR;
	complete(&cancellation->done);
}

static int ethosu_cancel_inference_complete(struct ethosu_mailbox_msg *msg)
{
	struct ethosu_cancel_inference *cancellation =
		container_of(msg, typeof(*cancellation), msg);

	if (completion_done(&cancellation->done))
		return 0;

	cancellation->errno = 0;
	cancellation->uapi->status =
		cancellation->inf->done &&
		cancellation->inf->status != ETHOSU_UAPI_STATUS_OK ?
		ETHOSU_UAPI_STATUS_OK :
		ETHOSU_UAPI_STATUS_ERROR;
	complete(&cancellation->done);

	return 0;
}

int ethosu_cancel_inference_request(struct ethosu_inference *inf,
				    struct ethosu_uapi_cancel_inference_status *uapi)
{
	struct ethosu_cancel_inference *cancellation;
	int ret;
	int timeout;

	if (inf->done) {
		uapi->status = ETHOSU_UAPI_STATUS_ERROR;

		return 0;
	}

	cancellation =
		devm_kzalloc(inf->edev->dev,
			     sizeof(struct ethosu_cancel_inference),
			     GFP_KERNEL);
	if (!cancellation)
		return -ENOMEM;

	/* increase ref count on the inference we are refering to */
	ethosu_inference_get(inf);
	/* mark inference ABORTING to avoid resending the inference message */
	inf->status = ETHOSU_CORE_STATUS_ABORTING;

	cancellation->edev = inf->edev;
	cancellation->inf = inf;
	cancellation->uapi = uapi;
	kref_init(&cancellation->kref);
	init_completion(&cancellation->done);
	cancellation->msg.fail = ethosu_cancel_inference_fail;

	/* Never resend messages but always complete, since we have restart the
	 * whole firmware and marked the inference as aborted */
	cancellation->msg.resend = ethosu_cancel_inference_complete;

	/* Add cancel inference to pending list */
	list_add(&cancellation->msg.list,
		 &cancellation->edev->mailbox.pending_list);

	ret = ethosu_cancel_inference_send(cancellation);
	if (0 != ret)
		goto put_kref;

	/* Unlock the mutex before going to block on the condition */
	mutex_unlock(&cancellation->edev->mutex);
	/* wait for response to arrive back */
	timeout = wait_for_completion_timeout(&cancellation->done,
					      msecs_to_jiffies(
						      CANCEL_INFERENCE_RESP_TIMEOUT_MS));
	/* take back the mutex before resuming to do anything */
	ret = mutex_lock_interruptible(&cancellation->edev->mutex);
	if (0 != ret)
		goto put_kref;

	if (0 == timeout /* timed out*/) {
		dev_warn(inf->edev->dev,
			 "Msg: Cancel Inference response lost - timeout\n");
		ret = -EIO;
		goto put_kref;
	}

	if (cancellation->errno) {
		ret = cancellation->errno;
		goto put_kref;
	}

put_kref:
	kref_put(&cancellation->kref, &ethosu_cancel_inference_destroy);

	return ret;
}

void ethosu_cancel_inference_rsp(struct ethosu_device *edev,
				 struct ethosu_core_cancel_inference_rsp *rsp)
{
	struct ethosu_cancel_inference *cancellation =
		(struct ethosu_cancel_inference *)rsp->user_arg;
	int ret;

	ret = ethosu_mailbox_find(&edev->mailbox, &cancellation->msg);
	if (ret) {
		dev_warn(edev->dev,
			 "Handle not found in cancel inference list. handle=0x%p\n",
			 rsp);

		return;
	}

	if (completion_done(&cancellation->done))
		return;

	cancellation->errno = 0;
	switch (rsp->status) {
	case ETHOSU_CORE_STATUS_OK:
		cancellation->uapi->status = ETHOSU_UAPI_STATUS_OK;
		break;
	case ETHOSU_CORE_STATUS_ERROR:
		cancellation->uapi->status = ETHOSU_UAPI_STATUS_ERROR;
		break;
	}

	complete(&cancellation->done);
}
