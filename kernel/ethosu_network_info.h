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

#include <linux/kref.h>
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
	struct kref                     kref;
	struct list_head                list;
	struct completion               done;
	int                             errno;
};

/****************************************************************************
 * Functions
 ****************************************************************************/

/**
 * ethosu_network_info_create() - Create network info
 *
 * This function must be called in the context of a user space process.
 *
 * Return: Valid pointer on success, else ERR_PTR.
 */
struct ethosu_network_info *ethosu_network_info_create(
	struct ethosu_device *edev,
	struct ethosu_network *net,
	struct ethosu_uapi_network_info *uapi);

/**
 * ethosu_network_info_get() - Get network info
 */
void ethosu_network_info_get(struct ethosu_network_info *info);

/**
 * ethosu_network_info_put() - Put network info
 */
void ethosu_network_info_put(struct ethosu_network_info *info);

/**
 * ethosu_network_info_wait() - Get network info
 */
int ethosu_network_info_wait(struct ethosu_network_info *info,
			     int timeout);

/**
 * ethosu_network_info_rsp() - Handle network info response.
 */
void ethosu_network_info_rsp(struct ethosu_device *edev,
			     struct ethosu_core_network_info_rsp *rsp);

#endif /* ETHOSU_NETWORK_INFO_H */
