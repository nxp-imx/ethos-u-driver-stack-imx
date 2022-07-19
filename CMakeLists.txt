#
# Copyright (c) 2020-2021 Arm Limited. All rights reserved.
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the License); you may
# not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an AS IS BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

cmake_minimum_required(VERSION 3.0.2)

# Set the project name and version
project("linux_driver_stack" VERSION 1.0)

# Default options
option(BUILD_KERNEL "Build the kernel driver" ON)
option(BUILD_MAILBOX "Build the MHU mailbox kernel module" ON)
option(BUILD_REMOTEPROC "Build the remoteproc kernel module" ON)

# Set variables
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(ARCH "arm64")
else()
    set(ARCH "arm")
endif()

string(REGEX REPLACE "(.*)gcc$" "\\1" CROSS_COMPILE "${CMAKE_C_COMPILER}")

# Add include directory
include_directories("include")

# Build driver library
add_subdirectory(driver_library)

# Build utils
add_subdirectory(utils)
