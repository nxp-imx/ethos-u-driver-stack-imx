#!/usr/bin/env sh

#
# Copyright (c) 2020 Arm Limited. All rights reserved.
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

# Remove existing device nodes
rm -rf /dev/ethosu*

# Create new device nodes
i=0
for dev in `find /sys/devices/virtual/ethosu -name dev`
do
    major=`cat $dev | cut -d ':' -f 1`
    minor=`cat $dev | cut -d ':' -f 2`

    cmd="mknod /dev/ethosu$i c $major $minor"
    echo $cmd
    $cmd

    i=$(( $i+1 ))
done
