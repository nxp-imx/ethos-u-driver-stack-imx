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

#ifndef ETHOSU_INFERENCE_H
#define ETHOSU_INFERENCE_H

/****************************************************************************
 * Includes
 ****************************************************************************/

#include "uapi/ethosu.h"

#include <linux/kref.h>
#include <linux/types.h>
#include <linux/wait.h>

/****************************************************************************
 * Types
 ****************************************************************************/

struct ethosu_buffer;
struct ethosu_core_inference_rsp;
struct ethosu_device;
struct ethosu_network;
struct ethosu_uapi_inference_create;
struct file;

/**
 * struct ethosu_inference - Inference struct
 * @edev:	Arm Ethos-U device
 * @file:	File handle
 * @kref:	Reference counter
 * @waitq:	Wait queue
 * @ifm:	Pointer to IFM buffer
 * @ofm:	Pointer to OFM buffer
 * @net:	Pointer to network
 * @pending:	Pending response from the firmware
 * @status:	Inference status
 */
struct ethosu_inference {
	struct ethosu_device    *edev;
	struct file             *file;
	struct kref             kref;
	wait_queue_head_t       waitq;
	struct ethosu_buffer    *ifm;
	struct ethosu_buffer    *ofm;
	struct ethosu_network   *net;
	bool                    pending;
	enum ethosu_uapi_status status;
	struct list_head        list;
};

/****************************************************************************
 * Functions
 ****************************************************************************/

/**
 * ethosu_inference_create() - Create inference
 *
 * This function must be called in the context of a user space process.
 *
 * Return: fd on success, else error code.
 */
int ethosu_inference_create(struct ethosu_device *edev,
			    struct ethosu_network *net,
			    struct ethosu_uapi_inference_create *uapi);

/**
 * ethosu_inference_get_from_fd() - Get inference handle from fd
 *
 * This function must be called from a user space context.
 *
 * Return: Pointer on success, else ERR_PTR.
 */
struct ethosu_inference *ethosu_inference_get_from_fd(int fd);

/**
 * ethosu_inference_get() - Get inference
 */
void ethosu_inference_get(struct ethosu_inference *inf);

/**
 * ethosu_inference_put() - Put inference
 */
void ethosu_inference_put(struct ethosu_inference *inf);

/**
 * ethosu_inference_rsp() - Handle inference response
 */
void ethosu_inference_rsp(struct ethosu_device *edev,
			  struct ethosu_core_inference_rsp *rsp);

#endif /* ETHOSU_INFERENCE_H */
