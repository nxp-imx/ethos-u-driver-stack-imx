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

#ifndef ETHOSU_NETWORK_INFO_H
#define ETHOSU_NETWORK_INFO_H

/****************************************************************************
 * Includes
 ****************************************************************************/

#include "ethosu_core_interface.h"
#include "ethosu_mailbox.h"

#include <linux/types.h>
#include <linux/completion.h>

/****************************************************************************
 * Types
 ****************************************************************************/

struct ethosu_device;
struct ethosu_network;
struct ethosu_uapi_network_info;

struct ethosu_network_info {
	struct ethosu_device            *edev;
	struct ethosu_network           *net;
	struct ethosu_uapi_network_info *uapi;
	struct completion               done;
	int                             errno;
	struct ethosu_mailbox_msg       msg;
};

/****************************************************************************
 * Functions
 ****************************************************************************/

/**
 * ethosu_network_info_request() - Send a network info request
 *
 * This function must be called in the context of a user space process.
 *
 * Return: 0 on success, .
 */
int ethosu_network_info_request(struct ethosu_network *net,
				struct ethosu_uapi_network_info *uapi);

/**
 * ethosu_network_info_rsp() - Handle network info response.
 */
void ethosu_network_info_rsp(struct ethosu_device *edev,
			     struct ethosu_core_network_info_rsp *rsp);

#endif /* ETHOSU_NETWORK_INFO_H */
