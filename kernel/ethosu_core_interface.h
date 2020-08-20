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

#ifndef ETHOSU_CORE_INTERFACE_H
#define ETHOSU_CORE_INTERFACE_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

/**
 * enum ethosu_core_msg_type - Message types
 *
 * Types for the messages sent between the host and the core subsystem.
 */
enum ethosu_core_msg_type {
	ETHOSU_CORE_MSG_PING = 1,
	ETHOSU_CORE_MSG_PONG,
	ETHOSU_CORE_MSG_INFERENCE_REQ,
	ETHOSU_CORE_MSG_INFERENCE_RSP,
	ETHOSU_CORE_MSG_MAX
};

/**
 * struct ethosu_core_msg - Message header
 */
struct ethosu_core_msg {
	uint32_t type;
	uint32_t length;
};

/**
 * struct ethosu_core_queue_header - Message queue header
 */
struct ethosu_core_queue_header {
	uint32_t size;
	uint32_t read;
	uint32_t write;
};

/**
 * struct ethosu_core_queue - Message queue
 *
 * Dynamically sized message queue.
 */
struct ethosu_core_queue {
	struct ethosu_core_queue_header header;
	uint8_t                         data[];
};

enum ethosu_core_status {
	ETHOSU_CORE_STATUS_OK,
	ETHOSU_CORE_STATUS_ERROR
};

struct ethosu_core_buffer {
	uint32_t ptr;
	uint32_t size;
};

struct ethosu_core_inference_req {
	uint64_t                  user_arg;
	struct ethosu_core_buffer ifm;
	struct ethosu_core_buffer ofm;
	struct ethosu_core_buffer network;
};

struct ethosu_core_inference_rsp {
	uint64_t user_arg;
	uint32_t ofm_size;
	uint32_t status;
};

#endif
