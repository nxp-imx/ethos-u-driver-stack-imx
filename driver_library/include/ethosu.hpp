/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
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

#pragma once

#include <uapi/ethosu.h>

#include <memory>
#include <string>

namespace EthosU
{

class Exception :
    public std::exception
{
public:
    Exception(const char *msg);
    virtual ~Exception() throw();
    virtual const char *what() const throw();

private:
    std::string msg;
};

class Device
{
public:
    Device(const char *device = "/dev/ethosu0");
    virtual ~Device();

    int ioctl(unsigned long cmd, void *data = nullptr);

private:
    int fd;
};

class Buffer
{
public:
    Buffer(Device &device, const size_t capacity);
    virtual ~Buffer();

    size_t capacity() const;
    void clear();
    char *data();
    void resize(size_t size, size_t offset = 0);
    size_t offset() const;
    size_t size() const;

    int getFd() const;

private:
    int fd;
    char *dataPtr;
    const size_t dataCapacity;
};

class Network
{
public:
    Network(Device &device, std::shared_ptr<Buffer> &buffer);
    virtual ~Network();

    int ioctl(unsigned long cmd, void *data = nullptr);
    std::shared_ptr<Buffer> getBuffer();

private:
    int fd;
    std::shared_ptr<Buffer> buffer;
};

class Inference
{
public:
    Inference(std::shared_ptr<Network> &network, std::shared_ptr<Buffer> &ifm, std::shared_ptr<Buffer> &ofm);
    virtual ~Inference();

    void wait(int timeoutSec = -1);
    bool failed();
    int getFd();
    std::shared_ptr<Network> getNetwork();
    std::shared_ptr<Buffer> getIfmBuffer();
    std::shared_ptr<Buffer> getOfmBuffer();

private:
    int fd;
    std::shared_ptr<Network> network;
    std::shared_ptr<Buffer> ifmBuffer;
    std::shared_ptr<Buffer> ofmBuffer;
};

}
