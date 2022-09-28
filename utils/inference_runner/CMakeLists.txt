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

# Build executable
aux_source_directory(./common COMMON_SRCS)
add_executable(inference_runner ${COMMON_SRCS} inference_runner.cpp)
add_executable(interpreter_runner ${COMMON_SRCS} interpreter_runner.cpp)

# Link agains ethosu library
target_link_libraries(inference_runner PRIVATE ethosu flatbuffers)
target_link_libraries(interpreter_runner PRIVATE ethosu flatbuffers)


# Install target
install(TARGETS inference_runner DESTINATION "bin/ethosu/examples")
install(TARGETS interpreter_runner DESTINATION "bin/ethosu/examples")
