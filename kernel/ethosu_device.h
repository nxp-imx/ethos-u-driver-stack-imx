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

#ifndef ETHOSU_DEVICE_H
#define ETHOSU_DEVICE_H

/****************************************************************************
 * Includes
 ****************************************************************************/

#include "uapi/ethosu.h"
#include "ethosu_mailbox.h"
#include "ethosu_watchdog.h"

#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/completion.h>

/****************************************************************************
 * Types
 ****************************************************************************/

struct reset_control;

/**
 * struct ethosu_device - Device structure
 */
struct ethosu_device {
	struct device          *dev;
	struct cdev            cdev;
	struct                 class *class;
	dev_t                  devt;
	struct mutex           mutex;
	struct ethosu_mailbox  mailbox;
	struct ethosu_watchdog watchdog;
	struct reset_control   *reset;
};

/**
 * struct ethosu_capabilities - Capabilities internal struct
 */
struct ethosu_capabilities {
	struct ethosu_device                   *edev;
	struct completion                      done;
	struct kref                            refcount;
	struct ethosu_uapi_device_capabilities *capabilities;
	struct ethosu_mailbox_msg              msg;
	int                                    errno;
};

/****************************************************************************
 * Functions
 ****************************************************************************/

/**
 * ethosu_dev_init() - Initialize the device
 *
 * Return: 0 on success, else error code.
 */
int ethosu_dev_init(struct ethosu_device *edev,
		    struct device *dev,
		    struct class *class,
		    dev_t devt,
		    struct resource *in_queue,
		    struct resource *out_queue);

/**
 * ethosu_dev_deinit() - Initialize the device
 */
void ethosu_dev_deinit(struct ethosu_device *edev);

/**
 * ethosu_firmware_reset() - Reset the device running firmware
 */
int ethosu_firmware_reset(struct ethosu_device *edev);

#endif /* ETHOSU_DEVICE_H */
