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

#ifndef ETHOSU_H
#define ETHOSU_H

/****************************************************************************
 * Includes
 ****************************************************************************/

#include <linux/ioctl.h>
#include <linux/types.h>

/****************************************************************************
 * Defines
 ****************************************************************************/

#define ETHOSU_IOCTL_BASE               0x01
#define ETHOSU_IO(nr)                   _IO(ETHOSU_IOCTL_BASE, nr)
#define ETHOSU_IOR(nr, type)            _IOR(ETHOSU_IOCTL_BASE, nr, type)
#define ETHOSU_IOW(nr, type)            _IOW(ETHOSU_IOCTL_BASE, nr, type)
#define ETHOSU_IOWR(nr, type)           _IOWR(ETHOSU_IOCTL_BASE, nr, type)

#define ETHOSU_IOCTL_PING               ETHOSU_IO(0x00)
#define ETHOSU_IOCTL_BUFFER_CREATE      ETHOSU_IOR(0x10, \
						   struct ethosu_uapi_buffer_create)
#define ETHOSU_IOCTL_BUFFER_SET         ETHOSU_IOR(0x11, \
						   struct ethosu_uapi_buffer)
#define ETHOSU_IOCTL_BUFFER_GET         ETHOSU_IOW(0x12, \
						   struct ethosu_uapi_buffer)
#define ETHOSU_IOCTL_NETWORK_CREATE     ETHOSU_IOR(0x20, \
						   struct ethosu_uapi_network_create)
#define ETHOSU_IOCTL_INFERENCE_CREATE   ETHOSU_IOR(0x30, \
						   struct ethosu_uapi_inference_create)
#define ETHOSU_IOCTL_INFERENCE_STATUS   ETHOSU_IO(0x31)

/****************************************************************************
 * Types
 ****************************************************************************/

/**
 * enum ethosu_uapi_status - Status
 */
enum ethosu_uapi_status {
	ETHOSU_UAPI_STATUS_OK,
	ETHOSU_UAPI_STATUS_ERROR
};

/**
 * struct ethosu_uapi_buffer_create - Create buffer request
 * @capacity:	Maximum capacity of the buffer
 */
struct ethosu_uapi_buffer_create {
	__u32 capacity;
};

/**
 * struct ethosu_uapi_buffer - Buffer descriptor
 * @offset:	Offset to where the data starts
 * @size:	Size of the data
 *
 * 'offset + size' must not exceed the capacity of the buffer.
 */
struct ethosu_uapi_buffer {
	__u32 offset;
	__u32 size;
};

/**
 * struct ethosu_uapi_network_create - Create network request
 * @fd:		Buffer file descriptor
 */
struct ethosu_uapi_network_create {
	__u32 fd;
};

/**
 * struct ethosu_uapi_inference_create - Create network request
 * @ifm_fd:		IFM buffer file descriptor
 * @ofm_fd:		OFM buffer file descriptor
 */
struct ethosu_uapi_inference_create {
	__u32 ifm_fd;
	__u32 ofm_fd;
};

#endif
