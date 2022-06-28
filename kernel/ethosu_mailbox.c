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

#include "ethosu_mailbox.h"

#include "ethosu_buffer.h"
#include "ethosu_core_interface.h"
#include "ethosu_device.h"
#include "ethosu_watchdog.h"

#include <linux/jiffies.h>
#include <linux/resource.h>
#include <linux/uio.h>

/****************************************************************************
 * Includes
 ****************************************************************************/

#ifndef fallthrough
#if __has_attribute(__fallthrough__)
#define fallthrough __attribute__((__fallthrough__))
#else
#define fallthrough do {} while (0)  /* fallthrough */
#endif
#endif

/****************************************************************************
 * Functions
 ****************************************************************************/

static void ethosu_wd_inc(struct ethosu_mailbox *mbox,
			  enum ethosu_core_msg_type type)
{
	switch (type) {
	case ETHOSU_CORE_MSG_PING:
		mbox->ping_count++;
		fallthrough;
	case ETHOSU_CORE_MSG_INFERENCE_REQ:
		ethosu_watchdog_inc(mbox->wdog);
		break;
	default:
		break;
	}
}

static void ethosu_wd_dec(struct ethosu_mailbox *mbox,
			  enum ethosu_core_msg_type type)
{
	switch (type) {
	case ETHOSU_CORE_MSG_PONG:
		mbox->ping_count--;
		fallthrough;
	case ETHOSU_CORE_MSG_INFERENCE_RSP:
		ethosu_watchdog_dec(mbox->wdog);
		break;
	default:
		break;
	}
}

static void ethosu_core_set_size(struct ethosu_buffer *buf,
				 struct ethosu_core_buffer *cbuf)
{
	cbuf->ptr = (uint32_t)buf->dma_addr + buf->offset;
	cbuf->size = (uint32_t)buf->size;
}

static void ethosu_core_set_capacity(struct ethosu_buffer *buf,
				     struct ethosu_core_buffer *cbuf)
{
	cbuf->ptr = (uint32_t)buf->dma_addr + buf->offset + buf->size;
	cbuf->size = (uint32_t)buf->capacity - buf->offset - buf->size;
}

static size_t ethosu_queue_available(struct ethosu_core_queue *queue)
{
	size_t size = queue->header.write - queue->header.read;

	if (queue->header.read > queue->header.write)
		size += queue->header.size;

	return size;
}

static size_t ethosu_queue_capacity(struct ethosu_core_queue *queue)
{
	return max_t(ssize_t,
		     queue->header.size - ethosu_queue_available(queue) - 1, 0);
}

static int ethosu_queue_write(struct ethosu_mailbox *mbox,
			      const struct kvec *vec,
			      size_t length)
{
	struct ethosu_core_queue *queue = mbox->in_queue;
	uint8_t *dst = &queue->data[0];
	uint32_t wpos = queue->header.write;
	size_t total_size;
	size_t i;
	int ret;

	for (i = 0, total_size = 0; i < length; i++)
		total_size += vec[i].iov_len;

	if (total_size > ethosu_queue_capacity(queue))
		return -EINVAL;

	for (i = 0; i < length; i++) {
		const uint8_t *src = vec[i].iov_base;
		const uint8_t *end = src + vec[i].iov_len;

		while (src < end) {
			dst[wpos] = *src++;
			wpos = (wpos + 1) % queue->header.size;
		}
	}

	queue->header.write = wpos;

	ret = mbox_send_message(mbox->tx, queue);
	if (ret < 0)
		return ret;

	return 0;
}

static int ethosu_queue_write_msg(struct ethosu_mailbox *mbox,
				  uint32_t type,
				  void *data,
				  size_t length)
{
	struct ethosu_core_msg msg = {
		.magic = ETHOSU_CORE_MSG_MAGIC,
		.type  = type,                 .length= length
	};
	const struct kvec vec[2] = {
		{ &msg, sizeof(msg) },
		{ data, length      }
	};
	int ret;

	ret = ethosu_queue_write(mbox, vec, 2);
	if (ret)
		return ret;

	ethosu_wd_inc(mbox, type);

	return 0;
}

static int ethosu_queue_read(struct ethosu_mailbox *mbox,
			     void *data,
			     size_t length)
{
	struct ethosu_core_queue *queue = mbox->out_queue;
	uint8_t *src = &queue->data[0];
	uint8_t *dst = (uint8_t *)data;
	const uint8_t *end = dst + length;
	uint32_t rpos = queue->header.read;
	size_t queue_avail = ethosu_queue_available(queue);

	if (length == 0)
		return 0;
	else if (queue_avail == 0)
		return -ENOMSG;
	else if (length > queue_avail)
		return -EBADMSG;

	while (dst < end) {
		*dst++ = src[rpos];
		rpos = (rpos + 1) % queue->header.size;
	}

	queue->header.read = rpos;

	return 0;
}

void ethosu_mailbox_reset(struct ethosu_mailbox *mbox)
{
	mbox->out_queue->header.read = mbox->out_queue->header.write;
}

void ethosu_mailbox_wait_prepare(struct ethosu_mailbox *mbox)
{
	mbox->in_queue->header.size = 0;
	mbox->in_queue->header.read = 0xffffff;
	mbox->in_queue->header.write = 0xffffff;
}

int ethosu_mailbox_wait_firmware(struct ethosu_mailbox *mbox)
{
	const unsigned long timeout = 1000;
	const unsigned long end = jiffies + msecs_to_jiffies(timeout);
	volatile struct ethosu_core_queue_header *hdr =
		&mbox->in_queue->header;
	int ret = -ETIMEDOUT;

	/* Spin wait on mailbox initialization */
	while ((end - jiffies) < timeout)
		if (hdr->size != 0 &&
		    hdr->read != 0xffffff &&
		    hdr->write != 0xffffff) {
			ret = 0;
			break;
		}

	dev_info(mbox->dev, "mbox: Wait. ret=%d, size=%u, read=%u, write=%u",
		 ret, hdr->size, hdr->read, hdr->write);

	return ret;
}

int ethosu_mailbox_read(struct ethosu_mailbox *mbox,
			struct ethosu_core_msg *header,
			void *data,
			size_t length)
{
	int ret;

	/* Read message header magic */
	ret = ethosu_queue_read(mbox, header, sizeof(*header));
	if (ret) {
		if (ret != -ENOMSG)
			dev_warn(mbox->dev,
				 "Msg: Failed to read message header\n");

		return ret;
	}

	if (header->magic != ETHOSU_CORE_MSG_MAGIC) {
		dev_warn(mbox->dev,
			 "Msg: Invalid magic. Got: %08X but expected %08X\n",
			 header->magic, ETHOSU_CORE_MSG_MAGIC);

		return -EINVAL;
	}

	dev_info(mbox->dev,
		 "mbox: Read msg header. magic=%08X, type=%u, length=%u",
		 header->magic, header->type, header->length);

	/* Check that payload is not larger than allocated buffer */
	if (header->length > length) {
		dev_warn(mbox->dev,
			 "Msg: Buffer size (%zu) too small for message (%u)\n",
			 sizeof(data), header->length);

		return -ENOMEM;
	}

	/* Read payload data */
	ret = ethosu_queue_read(mbox, data, header->length);
	if (ret) {
		dev_warn(mbox->dev, "Msg: Failed to read payload data\n");

		return -EBADMSG;
	}

	ethosu_wd_dec(mbox, header->type);

	return 0;
}

int ethosu_mailbox_register(struct ethosu_mailbox *mbox,
			    struct ethosu_mailbox_msg *msg)
{
	msg->id = idr_alloc_cyclic(&mbox->msg_idr, msg, 0, INT_MAX, GFP_KERNEL);
	if (msg->id < 0)
		return msg->id;

	return 0;
}

void ethosu_mailbox_deregister(struct ethosu_mailbox *mbox,
			       struct ethosu_mailbox_msg *msg)
{
	idr_remove(&mbox->msg_idr, msg->id);
}

struct ethosu_mailbox_msg *ethosu_mailbox_find(struct ethosu_mailbox *mbox,
					       int msg_id)
{
	struct ethosu_mailbox_msg *ptr = (struct ethosu_mailbox_msg *)idr_find(
		&mbox->msg_idr, msg_id);

	if (ptr == NULL)
		return ERR_PTR(-EINVAL);

	return ptr;
}

void ethosu_mailbox_fail(struct ethosu_mailbox *mbox)
{
	struct ethosu_mailbox_msg *cur;
	int id;

	idr_for_each_entry(&mbox->msg_idr, cur, id) {
		cur->fail(cur);
	}
}

void ethosu_mailbox_resend(struct ethosu_mailbox *mbox)
{
	struct ethosu_mailbox_msg *cur;
	int id;
	int ret;

	idr_for_each_entry(&mbox->msg_idr, cur, id) {
		ret = cur->resend(cur);
		if (ret) {
			dev_warn(mbox->dev, "Failed to resend msg. ret=%d",
				 ret);
			cur->fail(cur);
		}
	}
}

int ethosu_mailbox_ping(struct ethosu_mailbox *mbox)
{
	return ethosu_queue_write_msg(mbox, ETHOSU_CORE_MSG_PING, NULL, 0);
}

int ethosu_mailbox_pong(struct ethosu_mailbox *mbox)
{
	return ethosu_queue_write_msg(mbox, ETHOSU_CORE_MSG_PONG, NULL, 0);
}

int ethosu_mailbox_version_request(struct ethosu_mailbox *mbox)
{
	return ethosu_queue_write_msg(mbox, ETHOSU_CORE_MSG_VERSION_REQ, NULL,
				      0);
}

int ethosu_mailbox_capabilities_request(struct ethosu_mailbox *mbox,
					struct ethosu_mailbox_msg *msg)
{
	struct ethosu_core_capabilities_req req = {
		.user_arg = msg->id
	};

	return ethosu_queue_write_msg(mbox, ETHOSU_CORE_MSG_CAPABILITIES_REQ,
				      &req,
				      sizeof(req));
}

int ethosu_mailbox_inference(struct ethosu_mailbox *mbox,
			     struct ethosu_mailbox_msg *msg,
			     uint32_t ifm_count,
			     struct ethosu_buffer **ifm,
			     uint32_t ofm_count,
			     struct ethosu_buffer **ofm,
			     struct ethosu_buffer *network,
			     uint32_t network_index,
			     uint8_t *pmu_event_config,
			     uint8_t pmu_event_config_count,
			     uint8_t pmu_cycle_counter_enable)
{
	struct ethosu_core_inference_req inf;
	uint32_t i;

	/* Verify that the uapi and core has the same number of pmus */
	if (pmu_event_config_count != ETHOSU_CORE_PMU_MAX) {
		dev_err(mbox->dev, "PMU count misconfigured.\n");

		return -EINVAL;
	}

	inf.user_arg = msg->id;
	inf.ifm_count = ifm_count;
	inf.ofm_count = ofm_count;
	inf.pmu_cycle_counter_enable = pmu_cycle_counter_enable;

	for (i = 0; i < ifm_count; i++)
		ethosu_core_set_size(ifm[i], &inf.ifm[i]);

	for (i = 0; i < ofm_count; i++)
		ethosu_core_set_capacity(ofm[i], &inf.ofm[i]);

	for (i = 0; i < ETHOSU_CORE_PMU_MAX; i++)
		inf.pmu_event_config[i] = pmu_event_config[i];

	if (network != NULL) {
		inf.network.type = ETHOSU_CORE_NETWORK_BUFFER;
		ethosu_core_set_size(network, &inf.network.buffer);
	} else {
		inf.network.type = ETHOSU_CORE_NETWORK_INDEX;
		inf.network.index = network_index;
	}

	return ethosu_queue_write_msg(mbox, ETHOSU_CORE_MSG_INFERENCE_REQ,
				      &inf, sizeof(inf));
}

int ethosu_mailbox_network_info_request(struct ethosu_mailbox *mbox,
					struct ethosu_mailbox_msg *msg,
					struct ethosu_buffer *network,
					uint32_t network_index)
{
	struct ethosu_core_network_info_req info;

	info.user_arg = msg->id;

	if (network != NULL) {
		info.network.type = ETHOSU_CORE_NETWORK_BUFFER;
		ethosu_core_set_size(network, &info.network.buffer);
	} else {
		info.network.type = ETHOSU_CORE_NETWORK_INDEX;
		info.network.index = network_index;
	}

	return ethosu_queue_write_msg(mbox, ETHOSU_CORE_MSG_NETWORK_INFO_REQ,
				      &info, sizeof(info));
}

int ethosu_mailbox_cancel_inference(struct ethosu_mailbox *mbox,
				    struct ethosu_mailbox_msg *msg,
				    int inference_handle)
{
	struct ethosu_core_cancel_inference_req req;

	req.user_arg = msg->id;
	req.inference_handle = inference_handle;

	return ethosu_queue_write_msg(mbox,
				      ETHOSU_CORE_MSG_CANCEL_INFERENCE_REQ,
				      &req, sizeof(req));
}

static void ethosu_mailbox_rx_work(struct work_struct *work)
{
	struct ethosu_mailbox *mbox = container_of(work, typeof(*mbox), work);

	mbox->callback(mbox->user_arg);
}

static void ethosu_mailbox_rx_callback(struct mbox_client *client,
				       void *message)
{
	struct ethosu_mailbox *mbox =
		container_of(client, typeof(*mbox), client);

	dev_info(mbox->dev, "mbox: Received message.\n");

	queue_work(mbox->wq, &mbox->work);
}

static void ethosu_mailbox_tx_done(struct mbox_client *client,
				   void *message,
				   int r)
{
	if (r)
		dev_warn(client->dev, "mbox: Failed sending message (%d)\n", r);
	else
		dev_info(client->dev, "mbox: Message sent\n");
}

int ethosu_mailbox_init(struct ethosu_mailbox *mbox,
			struct device *dev,
			struct resource *in_queue,
			struct resource *out_queue,
			ethosu_mailbox_cb callback,
			void *user_arg,
			struct ethosu_watchdog *wdog)
{
	int ret;

	mbox->dev = dev;
	mbox->callback = callback;
	mbox->user_arg = user_arg;
	mbox->wdog = wdog;
	mbox->ping_count = 0;
	idr_init(&mbox->msg_idr);

	mbox->client.dev = dev;
	mbox->client.rx_callback = ethosu_mailbox_rx_callback;
	mbox->client.tx_prepare = NULL; /* preparation of data is handled
	                                 * through the
	                                 * queue functions */
	mbox->client.tx_done = ethosu_mailbox_tx_done;
	mbox->client.tx_block = true;
	mbox->client.knows_txdone = false;
	mbox->client.tx_tout = 500;

	mbox->in_queue = devm_ioremap_resource(mbox->dev, in_queue);
	if (IS_ERR(mbox->in_queue))
		return PTR_ERR(mbox->in_queue);

	mbox->out_queue = devm_ioremap_resource(mbox->dev, out_queue);
	if (IS_ERR(mbox->out_queue)) {
		ret = PTR_ERR(mbox->out_queue);
		goto unmap_in_queue;
	}

	mbox->wq = create_singlethread_workqueue("ethosu_workqueue");
	if (!mbox->wq) {
		dev_err(mbox->dev, "Failed to create work queue\n");
		ret = -EINVAL;
		goto unmap_out_queue;
	}

	INIT_WORK(&mbox->work, ethosu_mailbox_rx_work);

	mbox->tx = mbox_request_channel_byname(&mbox->client, "tx");
	if (IS_ERR(mbox->tx)) {
		dev_warn(mbox->dev, "mbox: Failed to request tx channel\n");
		ret = PTR_ERR(mbox->tx);
		goto workqueue_destroy;
	}

	mbox->rx = mbox_request_channel_byname(&mbox->client, "rx");
	if (IS_ERR(mbox->rx)) {
		dev_info(dev, "mbox: Using same channel for RX and TX\n");
		mbox->rx = mbox->tx;
	}

	return 0;

workqueue_destroy:
	destroy_workqueue(mbox->wq);

unmap_out_queue:
	devm_iounmap(mbox->dev, mbox->out_queue);

unmap_in_queue:
	devm_iounmap(mbox->dev, mbox->in_queue);

	return ret;
}

void ethosu_mailbox_deinit(struct ethosu_mailbox *mbox)
{
	if (mbox->rx != mbox->tx)
		mbox_free_channel(mbox->rx);

	mbox_free_channel(mbox->tx);
	destroy_workqueue(mbox->wq);
	devm_iounmap(mbox->dev, mbox->out_queue);
	devm_iounmap(mbox->dev, mbox->in_queue);
}
