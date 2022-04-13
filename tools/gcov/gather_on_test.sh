#!/usr/bin/env bash

#
# Copyright (c) 2022 Arm Limited.
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

set -o errexit
set -o pipefail
set -o errtrace

DEST=$1
GCDA_DIR=/sys/kernel/debug/gcov
TMP_DIR=

usage() {
    echo "Usage: $0 <output.tar.gz>" >&2
    exit 1
}

cleanup() {
    if [ -d "${TMP_DIR}" ]
    then
        rm -rf "${TMP_DIR}"
    fi
}

trap cleanup EXIT

if [ -z "${DEST}" ]
then
    usage
fi

# Find kernel directory
readarray -t DRIVER_GCDA < <(find ${GCDA_DIR} -path '*/kernel/ethosu_driver.gcda')
if [ ${#DRIVER_GCDA[*]} -le 0 ]
then
    echo "Error: Could not find Ethos-U kernel directory"
    exit 1
fi

LDS_DIR=$(dirname "$(dirname "${DRIVER_GCDA[0]}")")
DEST=$(realpath "${DEST}")
TMP_DIR=$(mktemp -d)

# Copy gcda objects to temporary directory
cd "${LDS_DIR}"
find . -name "*.gcda" -exec cp --parent {} "${TMP_DIR}" \;

# Create tar.gz archive
cd "${TMP_DIR}"
find . -type f -print0 | xargs -0 tar -zcvf "${DEST}"
