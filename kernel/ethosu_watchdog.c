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

#include "ethosu_watchdog.h"

#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/moduleparam.h>
#include <linux/version.h>

/****************************************************************************
 * Variables
 ****************************************************************************/

static unsigned long watchdog_timeout_ms = 3000;
module_param(watchdog_timeout_ms, ulong, 0664);
MODULE_PARM_DESC(watchdog_timeout_ms,
		 "Watchdog timeout in milliseconds for unresponsive firmware.");

/****************************************************************************
 * Functions
 ****************************************************************************/

static void ethosu_watchdog_update(struct ethosu_watchdog *wdog)
{
	int ret;

	ret = mod_timer(&wdog->timer,
			jiffies + msecs_to_jiffies(watchdog_timeout_ms));

	dev_info(wdog->dev,
		 "Wdog: Update watchdog timeout. ret=%d, timeout_ms=%lu, refcount=%u", ret,
		 watchdog_timeout_ms, atomic_read(&wdog->refcount));
}

static void ethosu_watchdog_work(struct work_struct *work)
{
	struct ethosu_watchdog *wdog =
		container_of(work, struct ethosu_watchdog, work);

	dev_info(wdog->dev, "Wdog: Watchdog timeout. refcount=%u",
		 atomic_read(&wdog->refcount));

	wdog->callback(wdog);
}

static void ethosu_watchdog_timeout(struct timer_list *timer)
{
	struct ethosu_watchdog *wdog =
		container_of(timer, struct ethosu_watchdog, timer);

	queue_work(system_unbound_wq, &wdog->work);
}

#if KERNEL_VERSION(4, 14, 0) > LINUX_VERSION_CODE
static void ethosu_watchdog_timeout_legacy(unsigned long data)
{
	ethosu_watchdog_timeout((struct timer_list *)data);
}

#endif

int ethosu_watchdog_init(struct ethosu_watchdog *wdog,
			 struct device *dev,
			 ethosu_watchdog_cb callback)
{
	wdog->dev = dev;
	wdog->callback = callback;
	atomic_set(&wdog->refcount, 0);
	INIT_WORK(&wdog->work, ethosu_watchdog_work);

#if KERNEL_VERSION(4, 14, 0) <= LINUX_VERSION_CODE
	timer_setup(&wdog->timer, ethosu_watchdog_timeout, 0);
#else
	setup_timer(&wdog->timer, ethosu_watchdog_timeout_legacy,
		    (unsigned long)&wdog->timer);
#endif

	return 0;
}

void ethosu_watchdog_deinit(struct ethosu_watchdog *wdog)
{
	del_timer(&wdog->timer);
}

int ethosu_watchdog_reset(struct ethosu_watchdog *wdog)
{
	del_timer(&wdog->timer);
	atomic_set(&wdog->refcount, 0);

	return 0;
}

void ethosu_watchdog_inc(struct ethosu_watchdog *wdog)
{
	atomic_inc(&wdog->refcount);
	ethosu_watchdog_update(wdog);
}

void ethosu_watchdog_dec(struct ethosu_watchdog *wdog)
{
	if (atomic_dec_and_test(&wdog->refcount)) {
		dev_info(wdog->dev, "Wdog: Cancel watchdog timeout");
		del_timer(&wdog->timer);
	} else {
		ethosu_watchdog_update(wdog);
	}
}
