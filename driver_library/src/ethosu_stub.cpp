/*
 * Copyright (c) 2020-2021 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <uapi/ethosu.h>

#include <ethosu.hpp>
#include <exception>
#include <iostream>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace EthosU {
int eopen(const char *p, int) {
    std::cout << "Opened filedescriptor for " << p;
    return 1;
}

int eclose(int) {
    return 0;
}

void *emmap(void *, size_t length, int, int, int, off_t) {
    void *d = malloc(length);
    return d;
}

int emunmap(void *addr, size_t) {
    free(addr);
    return 0;
}

int eioctl(int, unsigned long cmd, void *) {
    int result = 0;
    using namespace EthosU;

    switch (cmd) {
    case ETHOSU_IOCTL_PING:
        return result;
    case ETHOSU_IOCTL_VERSION_REQ:
        return result;
    case ETHOSU_IOCTL_CAPABILITIES_REQ:
        return result;
    case ETHOSU_IOCTL_BUFFER_CREATE:
        return result;
    case ETHOSU_IOCTL_BUFFER_SET:
        return result;
    case ETHOSU_IOCTL_BUFFER_GET:
        return result;
    case ETHOSU_IOCTL_NETWORK_CREATE:
        return result;
    case ETHOSU_IOCTL_INFERENCE_CREATE:
        return result;
    case ETHOSU_IOCTL_INFERENCE_STATUS:
        return result;
    default:
        throw EthosU::Exception("Unknown IOCTL");
    }
}

int epoll(struct pollfd *, nfds_t, int timeout_ms) {
    int t = 1000 * timeout_ms / 2;
    usleep(t);
    return 1;
}
} // namespace EthosU
