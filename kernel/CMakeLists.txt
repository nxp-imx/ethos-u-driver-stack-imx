#
# (C) COPYRIGHT 2020 ARM Limited. All rights reserved.
#
# This program is free software and is provided to you under the terms of the
# GNU General Public License version 2 as published by the Free Software
# Foundation, and any use by you of this program is subject to the terms
# of such GNU licence.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, you can access it online at
# http://www.gnu.org/licenses/gpl-2.0.html.
#
# SPDX-License-Identifier: GPL-2.0-only
#
 
cmake_minimum_required(VERSION 3.0.2)

# Set the project name and version
project("ethosu_kernel" VERSION 1.0)

# Make sure KDIR is set
set(KDIR "" CACHE PATH "Path to Linux kernel sources")
if (NOT EXISTS ${KDIR})
    message(FATAL_ERROR "Can't build kernel module without KDIR.")
endif()

# Depend on all h and c files
file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.c" "*.h")

# Build the kernel module
add_custom_target(kernel ALL
                  COMMAND ${CMAKE_MAKE_PROGRAM} -C ${KDIR} M=${CMAKE_CURRENT_SOURCE_DIR} CONFIG_ETHOSU=m CROSS_COMPILE=aarch64-linux-gnu- ARCH=arm64 modules
                  BYPRODUCTS ethosu.ko
                  DEPENDS ${SOURCES} Kbuild Kconfig
                  COMMENT "Building ethosu.ko"
                  VERBATIM)

# Install the kernel object and headers
install(FILES ethosu.ko DESTINATION "modules")
install(FILES "uapi/ethosu.h" DESTINATION "include/uapi")
