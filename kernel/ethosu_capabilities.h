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

#ifndef ETHOSU_CAPABILITIES_H
#define ETHOSU_CAPABILITIES_H

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
struct ethosu_uapi_device_capabilities;

/**
 * struct ethosu_capabilities - Capabilities internal struct
 */
struct ethosu_capabilities {
	struct ethosu_device                   *edev;
	struct completion                      done;
	struct ethosu_uapi_device_capabilities *uapi;
	struct ethosu_mailbox_msg              msg;
	int                                    errno;
};

/****************************************************************************
 * Functions
 ****************************************************************************/

int ethosu_capabilities_request(struct ethosu_device *edev,
				struct ethosu_uapi_device_capabilities *uapi);

void ethosu_capability_rsp(struct ethosu_device *edev,
			   struct ethosu_core_msg_capabilities_rsp *rsp);

#endif
