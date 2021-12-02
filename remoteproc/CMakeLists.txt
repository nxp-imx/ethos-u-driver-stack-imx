#
# Copyright (c) 2020-2021 Arm Limited. All rights reserved.
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
project("ethosu_remoteproc" VERSION 1.0)

# Make sure KDIR is set
set(KDIR "" CACHE PATH "Path to Linux kernel sources")
if (NOT EXISTS ${KDIR})
    message(FATAL_ERROR "Can't build kernel module without KDIR.")
endif()

# Depend on all h and c files
file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.c" "*.h")

file(GLOB_RECURSE OBJ RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.c")
list(TRANSFORM OBJ REPLACE "^(.*)[.]c" "\\1.o")
list(TRANSFORM OBJ PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)

set(MODULES CONFIG_ARM_ETHOSU_RPROC=m CONFIG_ARM_SGM775_ETHOSU_RESET=m CONFIG_ARM_JUNO_FPGA_RESET=m)

# Build the kernel module
add_custom_target(ethosu-remoteproc-module ALL
    COMMAND ${CMAKE_MAKE_PROGRAM} -C ${KDIR}
    EXTRA_CFLAGS=-I${KDIR}/../../../drivers/remoteproc M=${CMAKE_CURRENT_SOURCE_DIR}
                  ${MODULES}
                  CROSS_COMPILE=${CROSS_COMPILE} ARCH=${ARCH} modules
                  BYPRODUCTS
                  ${CMAKE_CURRENT_SOURCE_DIR}/ethosu_remoteproc.ko
                  ${CMAKE_CURRENT_SOURCE_DIR}/ethosu_remoteproc.mod.o
                  ${CMAKE_CURRENT_SOURCE_DIR}/modules.order
                  ${CMAKE_CURRENT_SOURCE_DIR}/Module.symvers
                  ${OBJ}
                  DEPENDS ${SOURCES} Kbuild Kconfig
                  COMMENT "Building remoteproc modules"
                  VERBATIM)

install(FILES ethosu_remoteproc.ko DESTINATION "modules")