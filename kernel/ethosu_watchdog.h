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

#ifndef ETHOSU_WATCHDOG_H
#define ETHOSU_WATCHDOG_H

/****************************************************************************
 * Includes
 ****************************************************************************/

#include <linux/types.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

/****************************************************************************
 * Types
 ****************************************************************************/

struct device;
struct ethosu_watchdog;

typedef void (*ethosu_watchdog_cb)(struct ethosu_watchdog *wdog);

struct ethosu_watchdog {
	struct device      *dev;
	ethosu_watchdog_cb callback;
	struct timer_list  timer;
	struct work_struct work;
	atomic_t           refcount;
};

/****************************************************************************
 * Functions
 ****************************************************************************/

/**
 * ethosu_watchdog_init() - Initialize watchdog
 *
 * Return: 0 on success, else error code.
 */
int ethosu_watchdog_init(struct ethosu_watchdog *wdog,
			 struct device *dev,
			 ethosu_watchdog_cb callback);

/**
 * ethosu_watchdog_deinit() - Deinitialize watchdog
 */
void ethosu_watchdog_deinit(struct ethosu_watchdog *wdog);

/**
 * ethosu_watchdog_reset() - Reset watchdog
 */
int ethosu_watchdog_reset(struct ethosu_watchdog *wdog);

/**
 * ethosu_watchdog_inc() - Increment reference count
 */
void ethosu_watchdog_inc(struct ethosu_watchdog *wdog);

/**
 * ethosu_watchdog_dec() - Decrement reference count
 */
void ethosu_watchdog_dec(struct ethosu_watchdog *wdog);

#endif /* ETHOSU_WATCHDOG_H */
