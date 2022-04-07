#
# Copyright (c) 2020,2022 Arm Limited.
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

# set the project name and version
project("driver_library" VERSION 1.0.0 LANGUAGES C CXX)

# Build the driver library
add_library(ethosu STATIC "src/ethosu.cpp")

# Add public include directory and select which files to install
target_include_directories(ethosu PUBLIC "include")
set_target_properties(ethosu PROPERTIES PUBLIC_HEADER "include/ethosu.hpp")
set_target_properties(ethosu PROPERTIES VERSION ${PROJECT_VERSION})

# Install library and public headers
install(TARGETS ethosu
        LIBRARY DESTINATION "lib"
        ARCHIVE DESTINATION "lib"
        PUBLIC_HEADER DESTINATION "include")
