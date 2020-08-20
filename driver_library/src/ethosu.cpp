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

#include <ethosu.hpp>
#include <uapi/ethosu.h>

#include <algorithm>
#include <exception>
#include <iostream>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

using namespace std;

namespace
{
int eioctl(int fd, unsigned long cmd, void *data = nullptr)
{
    int ret = ::ioctl(fd, cmd, data);
    if (ret < 0)
    {
        throw EthosU::Exception("IOCTL failed");
    }

    return ret;
}
}

namespace EthosU
{

Exception::Exception(const char *msg) :
    msg(msg)
{}

Exception::~Exception() throw()
{}

const char *Exception::what() const throw()
{
    return msg.c_str();
}

Device::Device(const char *device)
{
    fd = open(device, O_RDWR | O_NONBLOCK);
    if (fd < 0)
    {
        throw Exception("Failed to open device");
    }
}

Device::~Device()
{
    close(fd);
}

int Device::ioctl(unsigned long cmd, void *data)
{
    return eioctl(fd, cmd, data);
}

Buffer::Buffer(Device &device, const size_t capacity) :
    fd(-1),
    dataPtr(nullptr),
    dataCapacity(capacity)
{
    ethosu_uapi_buffer_create uapi = { static_cast<uint32_t>(dataCapacity) };
    fd = device.ioctl(ETHOSU_IOCTL_BUFFER_CREATE, static_cast<void *>(&uapi));

    void *d = ::mmap(nullptr, dataCapacity, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (d == MAP_FAILED)
    {
        throw Exception("MMap failed");
    }

    dataPtr = reinterpret_cast<char *>(d);
}

Buffer::~Buffer()
{
    close(fd);
}

size_t Buffer::capacity() const
{
    return dataCapacity;
}

void Buffer::clear()
{
    resize(0, 0);
}

char *Buffer::data()
{
    return dataPtr + offset();
}

void Buffer::resize(size_t size, size_t offset)
{
    ethosu_uapi_buffer uapi;
    uapi.offset = offset;
    uapi.size = size;

    eioctl(fd, ETHOSU_IOCTL_BUFFER_SET, static_cast<void *>(&uapi));
}

size_t Buffer::offset() const
{
    ethosu_uapi_buffer uapi;
    eioctl(fd, ETHOSU_IOCTL_BUFFER_GET, static_cast<void *>(&uapi));
    return uapi.offset;
}

size_t Buffer::size() const
{
    ethosu_uapi_buffer uapi;
    eioctl(fd, ETHOSU_IOCTL_BUFFER_GET, static_cast<void *>(&uapi));
    return uapi.size;
}

int Buffer::getFd() const
{
    return fd;
}

Network::Network(Device &device, shared_ptr<Buffer> &buffer) :
    fd(-1),
    buffer(buffer)
{
    ethosu_uapi_network_create uapi;

    uapi.fd = buffer->getFd();

    fd = device.ioctl(ETHOSU_IOCTL_NETWORK_CREATE, static_cast<void *>(&uapi));
}

Network::~Network()
{
    close(fd);
}

int Network::ioctl(unsigned long cmd, void *data)
{
    return eioctl(fd, cmd, data);
}

std::shared_ptr<Buffer> Network::getBuffer()
{
    return buffer;
}

Inference::Inference(std::shared_ptr<Network> &network, std::shared_ptr<Buffer> &ifmBuffer, std::shared_ptr<Buffer> &ofmBuffer) :
    fd(-1),
    network(network),
    ifmBuffer(ifmBuffer),
    ofmBuffer(ofmBuffer)
{
    ethosu_uapi_inference_create uapi;

    uapi.ifm_fd = ifmBuffer->getFd();
    uapi.ofm_fd = ofmBuffer->getFd();

    fd = network->ioctl(ETHOSU_IOCTL_INFERENCE_CREATE, static_cast<void *>(&uapi));
}

Inference::~Inference()
{
    close(fd);
}

void Inference::wait(int timeoutSec)
{
    pollfd pfd;

    pfd.fd = fd;
    pfd.events = POLLIN | POLLERR;
    pfd.revents = 0;

    int ret = ::poll(&pfd, 1, timeoutSec * 1000);

    cout << "Poll. ret=" << ret << ", revents=" << pfd.revents << endl;
}

bool Inference::failed()
{
    ethosu_uapi_status status = static_cast<ethosu_uapi_status>(eioctl(fd, ETHOSU_IOCTL_INFERENCE_STATUS));

    return status != ETHOSU_UAPI_STATUS_OK;
}

int Inference::getFd()
{
    return fd;
}

std::shared_ptr<Network> Inference::getNetwork()
{
    return network;
}

std::shared_ptr<Buffer> Inference::getIfmBuffer()
{
    return ifmBuffer;
}

std::shared_ptr<Buffer> Inference::getOfmBuffer()
{
    return ofmBuffer;
}

}
