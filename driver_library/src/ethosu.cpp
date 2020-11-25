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

#include "autogen/tflite_schema.hpp"

#include <ethosu.hpp>
#include <uapi/ethosu.h>

#include <algorithm>
#include <exception>
#include <iostream>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

using namespace std;

namespace {
int eioctl(int fd, unsigned long cmd, void *data = nullptr) {
    int ret = ::ioctl(fd, cmd, data);
    if (ret < 0) {
        throw EthosU::Exception("IOCTL failed");
    }

    return ret;
}

/****************************************************************************
 * TFL micro helpers
 ****************************************************************************/

size_t getShapeSize(const flatbuffers::Vector<int32_t> *shape) {
    size_t size = 1;

    for (auto it = shape->begin(); it != shape->end(); ++it) {
        size *= *it;
    }

    return size;
}

size_t getTensorTypeSize(const enum tflite::TensorType type) {
    switch (type) {
    case tflite::TensorType::TensorType_UINT8:
    case tflite::TensorType::TensorType_INT8:
        return 1;
    case tflite::TensorType::TensorType_INT16:
        return 2;
    default:
        throw EthosU::Exception("Unsupported tensor type");
    }
}

vector<size_t> getSubGraphDims(const tflite::SubGraph *subgraph, const flatbuffers::Vector<int32_t> *tensorMap) {
    vector<size_t> dims;

    for (auto index = tensorMap->begin(); index != tensorMap->end(); ++index) {
        auto tensor = subgraph->tensors()->Get(*index);
        size_t size = getShapeSize(tensor->shape());
        size *= getTensorTypeSize(tensor->type());

        if (size > 0) {
            dims.push_back(size);
        }
    }

    return dims;
}

} // namespace

namespace EthosU {

/****************************************************************************
 * Exception
 ****************************************************************************/

Exception::Exception(const char *msg) : msg(msg) {}

Exception::~Exception() throw() {}

const char *Exception::what() const throw() {
    return msg.c_str();
}

/****************************************************************************
 * Device
 ****************************************************************************/

Device::Device(const char *device) {
    fd = open(device, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        throw Exception("Failed to open device");
    }
}

Device::~Device() {
    close(fd);
}

int Device::ioctl(unsigned long cmd, void *data) {
    return eioctl(fd, cmd, data);
}

/****************************************************************************
 * Buffer
 ****************************************************************************/

Buffer::Buffer(Device &device, const size_t capacity) : fd(-1), dataPtr(nullptr), dataCapacity(capacity) {
    ethosu_uapi_buffer_create uapi = {static_cast<uint32_t>(dataCapacity)};
    fd                             = device.ioctl(ETHOSU_IOCTL_BUFFER_CREATE, static_cast<void *>(&uapi));

    void *d = ::mmap(nullptr, dataCapacity, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (d == MAP_FAILED) {
        throw Exception("MMap failed");
    }

    dataPtr = reinterpret_cast<char *>(d);
}

Buffer::~Buffer() {
    close(fd);
}

size_t Buffer::capacity() const {
    return dataCapacity;
}

void Buffer::clear() {
    resize(0, 0);
}

char *Buffer::data() {
    return dataPtr + offset();
}

void Buffer::resize(size_t size, size_t offset) {
    ethosu_uapi_buffer uapi;
    uapi.offset = offset;
    uapi.size   = size;
    eioctl(fd, ETHOSU_IOCTL_BUFFER_SET, static_cast<void *>(&uapi));
}

size_t Buffer::offset() const {
    ethosu_uapi_buffer uapi;
    eioctl(fd, ETHOSU_IOCTL_BUFFER_GET, static_cast<void *>(&uapi));
    return uapi.offset;
}

size_t Buffer::size() const {
    ethosu_uapi_buffer uapi;
    eioctl(fd, ETHOSU_IOCTL_BUFFER_GET, static_cast<void *>(&uapi));
    return uapi.size;
}

int Buffer::getFd() const {
    return fd;
}

/****************************************************************************
 * Network
 ****************************************************************************/

Network::Network(Device &device, shared_ptr<Buffer> &buffer) : fd(-1), buffer(buffer) {
    // Create buffer handle
    ethosu_uapi_network_create uapi;
    uapi.fd = buffer->getFd();
    fd      = device.ioctl(ETHOSU_IOCTL_NETWORK_CREATE, static_cast<void *>(&uapi));

    // Create model handle
    const tflite::Model *model = tflite::GetModel(reinterpret_cast<void *>(buffer->data()));

    // Get input dimensions for first subgraph
    auto *subgraph = *model->subgraphs()->begin();
    ifmDims        = getSubGraphDims(subgraph, subgraph->inputs());

    // Get output dimensions for last subgraph
    subgraph = *model->subgraphs()->rbegin();
    ofmDims  = getSubGraphDims(subgraph, subgraph->outputs());
}

Network::~Network() {
    close(fd);
}

int Network::ioctl(unsigned long cmd, void *data) {
    return eioctl(fd, cmd, data);
}

shared_ptr<Buffer> Network::getBuffer() {
    return buffer;
}

const std::vector<size_t> &Network::getIfmDims() const {
    return ifmDims;
}

size_t Network::getIfmSize() const {
    size_t size = 0;

    for (auto s : ifmDims) {
        size += s;
    }

    return size;
}

const std::vector<size_t> &Network::getOfmDims() const {
    return ofmDims;
}

size_t Network::getOfmSize() const {
    size_t size = 0;

    for (auto s : ofmDims) {
        size += s;
    }

    return size;
}

/****************************************************************************
 * Inference
 ****************************************************************************/

Inference::~Inference() {
    close(fd);
}

void Inference::create(std::vector<uint32_t> &counterConfigs, bool cycleCounterEnable = false) {
    ethosu_uapi_inference_create uapi;

    if (ifmBuffers.size() > ETHOSU_FD_MAX) {
        throw Exception("IFM buffer overflow");
    }

    if (ofmBuffers.size() > ETHOSU_FD_MAX) {
        throw Exception("OFM buffer overflow");
    }

    if (counterConfigs.size() != ETHOSU_PMU_EVENT_MAX) {
        throw Exception("Wrong size of counter configurations");
    }

    uapi.ifm_count = 0;
    for (auto it : ifmBuffers) {
        uapi.ifm_fd[uapi.ifm_count++] = it->getFd();
    }

    uapi.ofm_count = 0;
    for (auto it : ofmBuffers) {
        uapi.ofm_fd[uapi.ofm_count++] = it->getFd();
    }

    for (int i = 0; i < ETHOSU_PMU_EVENT_MAX; i++) {
        uapi.pmu_config.events[i] = counterConfigs[i];
    }

    uapi.pmu_config.cycle_count = cycleCounterEnable;

    fd = network->ioctl(ETHOSU_IOCTL_INFERENCE_CREATE, static_cast<void *>(&uapi));
}

std::vector<uint32_t> Inference::initializeCounterConfig() {
    return std::vector<uint32_t>(ETHOSU_PMU_EVENT_MAX, 0);
}

uint32_t Inference::getMaxPmuEventCounters() {
    return ETHOSU_PMU_EVENT_MAX;
}

int Inference::wait(int timeoutSec) {
    pollfd pfd;

    pfd.fd      = fd;
    pfd.events  = POLLIN | POLLERR;
    pfd.revents = 0;

    int ret = ::poll(&pfd, 1, timeoutSec * 1000);

    cout << "Poll. ret=" << ret << ", revents=" << pfd.revents << endl;

    return ret;
}

bool Inference::failed() {
    ethosu_uapi_result_status uapi;

    eioctl(fd, ETHOSU_IOCTL_INFERENCE_STATUS, static_cast<void *>(&uapi));

    return uapi.status != ETHOSU_UAPI_STATUS_OK;
}

const std::vector<uint32_t> Inference::getPmuCounters() {
    ethosu_uapi_result_status uapi;
    std::vector<uint32_t> counterValues = std::vector<uint32_t>(ETHOSU_PMU_EVENT_MAX, 0);

    eioctl(fd, ETHOSU_IOCTL_INFERENCE_STATUS, static_cast<void *>(&uapi));

    for (int i = 0; i < ETHOSU_PMU_EVENT_MAX; i++) {
        if (uapi.pmu_config.events[i]) {
            counterValues.at(i) = uapi.pmu_count.events[i];
        }
    }

    return counterValues;
}

uint64_t Inference::getCycleCounter() {
    ethosu_uapi_result_status uapi;

    eioctl(fd, ETHOSU_IOCTL_INFERENCE_STATUS, static_cast<void *>(&uapi));

    return uapi.pmu_count.cycle_count;
}

int Inference::getFd() {
    return fd;
}

shared_ptr<Network> Inference::getNetwork() {
    return network;
}

vector<shared_ptr<Buffer>> &Inference::getIfmBuffers() {
    return ifmBuffers;
}

vector<shared_ptr<Buffer>> &Inference::getOfmBuffers() {
    return ofmBuffers;
}

} // namespace EthosU
