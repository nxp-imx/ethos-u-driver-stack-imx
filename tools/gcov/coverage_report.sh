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

GCDA=$1
OUT_DIR=$2
LDS_DIR="$(realpath "$(dirname "$(dirname "$(dirname "$0")")")")"

usage() {
    echo "Usage: $0 <gcov.tar.gz> <outdir>" >&2
    exit 1
}

# Verify arguments to the script
if [ ! -e "${GCDA}" ] || [ -z "${OUT_DIR}" ]
then
    usage
fi

# Recreate output directory
if [ -d "${OUT_DIR}" ]
then
    rm -rf "${OUT_DIR}"
fi

mkdir "${OUT_DIR}"

# Extract gcda files from the archive
find "${LDS_DIR}/kernel" -name "*.gcda" -delete
tar -C "${LDS_DIR}" -zxf "${GCDA}"

# Analyze coverage and generate HTML report
lcov -c -o "${OUT_DIR}/coverage.info" -d "${LDS_DIR}/kernel"
lcov --rc lcov_branch_coverage=1 --remove "${OUT_DIR}/coverage.info" \
    '*/kernel/arch/*' '*/kernel/include/*' \
    > "${OUT_DIR}/coverage.filtered.info"
genhtml -o "${OUT_DIR}" "${OUT_DIR}/coverage.filtered.info"
