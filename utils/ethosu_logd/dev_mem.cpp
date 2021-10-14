/*
 * Copyright (c) 2021 Arm Limited. All rights reserved.
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

#include "dev_mem.hpp"

#include <cstddef>
#include <iostream>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace EthosU {
namespace DevMem {

/****************************************************************************
 * Exception
 ****************************************************************************/

Exception::Exception(const char *msg) : msg(msg) {}

Exception::Exception(const std::string &msg) : msg(msg) {}

Exception::~Exception() throw() {}

const char *Exception::what() const throw() {
    return msg.c_str();
}

/****************************************************************************
 * DevMem
 ****************************************************************************/

DevMem::DevMem(uintptr_t address, size_t size) :
    base(nullptr), pageMask(sysconf(_SC_PAGESIZE) - 1), pageOffset(address & pageMask), size(size) {
    int fd = ::open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        throw Exception("Failed to open device");
    }

    base = reinterpret_cast<char *>(
        ::mmap(nullptr, pageOffset + size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, address & ~pageMask));
    if (base == MAP_FAILED) {
        throw Exception("MMap failed");
    }

    ::close(fd);
}

DevMem::~DevMem() {
    ::munmap(base, pageOffset + size);
}

void DevMem::read(char *dst, size_t length, size_t offset) {
    if (offset + length > size) {
        throw Exception("Read failed");
    }

    // TODO Why do not std::copy() or memcpy work?
    for (size_t i = 0; i < length; i++) {
        dst[i] = base[pageOffset + offset + i];
    }
}

void DevMem::write(char *src, size_t length, size_t offset) {
    if (offset + length > size) {
        throw Exception("Write failed");
    }

    // TODO Why do not std::copy() or memcpy work?
    for (size_t i = 0; i < length; i++) {
        base[pageOffset + offset + i] = src[i];
    }
}

/****************************************************************************
 * Log
 ****************************************************************************/

Log::Log(uintptr_t address, size_t size) : DevMem(address, size) {}

void Log::clear() {
    LogHeader header;
    read(header, 0);

    uint32_t rpos = header.write;
    write(rpos, offsetof(LogHeader, read));
}

void Log::print() {
    LogHeader header;
    read(header, 0);

    if (header.size < LOG_SIZE_MIN || header.size > LOG_SIZE_MAX) {
        std::string msg = "Incorrect ring buffer values. size=" + std::to_string(header.size) +
                          ", read=" + std::to_string(header.read) + ", write=" + std::to_string(header.write);
        throw Exception(msg.c_str());
    }

    size_t rpos = header.read;

    // Skip forward if read is more than 'size' behind
    if (rpos + header.size < header.write) {
        rpos = header.write - header.size;
    }

    while (rpos < header.write) {
        char c;
        size_t offset = rpos++ % header.size + sizeof(header);
        read(c, offset);
        std::cout << c;
    }
}

} // namespace DevMem
} // namespace EthosU
