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

#ifndef DEV_MEM_HPP
#define DEV_MEM_HPP

#include <exception>
#include <string>

namespace EthosU {
namespace DevMem {

/****************************************************************************
 * Exception
 ****************************************************************************/

class Exception : public std::exception {
public:
    Exception(const char *msg);
    Exception(const std::string &msg);
    virtual ~Exception() throw();
    virtual const char *what() const throw();

private:
    std::string msg;
};

/****************************************************************************
 * DevMem
 ****************************************************************************/

class DevMem {
public:
    DevMem(uintptr_t address, size_t size);
    virtual ~DevMem();

    void read(char *dst, size_t length, size_t offset);

    template <typename T>
    void read(T &dst, size_t offset) {
        read(reinterpret_cast<char *>(&dst), sizeof(dst), offset);
    }

    void write(char *src, size_t length, size_t offset);

    template <typename T>
    void write(T &src, size_t offset) {
        write(reinterpret_cast<char *>(&src), sizeof(src), offset);
    }

private:
    char *base;
    const uintptr_t pageMask;
    const size_t pageOffset;
    const size_t size;
};

/****************************************************************************
 * Log
 ****************************************************************************/

class Log : public DevMem {
public:
    Log(uintptr_t address, size_t size = LOG_SIZE_MAX);

    void clear();
    void print();

private:
    struct LogHeader {
        uint32_t size;
        uint32_t read;
        uint32_t pad[6];
        uint32_t write;
    };

    static const size_t LOG_SIZE_MIN = 1024;
    static const size_t LOG_SIZE_MAX = 1024 * 1024;

    static uintptr_t getAddress();
};

} // namespace DevMem
} // namespace EthosU

#endif /* DEV_MEM_HPP */
