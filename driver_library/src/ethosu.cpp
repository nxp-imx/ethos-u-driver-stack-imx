/*
 * Copyright (c) 2020-2022 Arm Limited.
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
#include <fstream>
#include <iostream>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

using namespace std;

namespace EthosU {
__attribute__((weak)) int eioctl(int fd, unsigned long cmd, void *data = nullptr) {
    int ret = ::ioctl(fd, cmd, data);
    if (ret < 0) {
        throw EthosU::Exception("IOCTL failed");
    }

    return ret;
}

__attribute__((weak)) int eopen(const char *pathname, int flags) {
    int fd = ::open(pathname, flags);
    if (fd < 0) {
        throw Exception("Failed to open device");
    }

    return fd;
}

__attribute__((weak)) int
eppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, const sigset_t *sigmask) {
    int result = ::ppoll(fds, nfds, tmo_p, sigmask);
    if (result < 0) {
        throw Exception("Failed to wait for ppoll event or signal");
    }

    return result;
}

__attribute__((weak)) int eclose(int fd) {
    int result = ::close(fd);
    if (result < 0) {
        throw Exception("Failed to close file");
    }

    return result;
}
__attribute((weak)) void *emmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    void *ptr = ::mmap(addr, length, prot, flags, fd, offset);
    if (ptr == MAP_FAILED) {
        throw Exception("Failed to mmap file");
    }

    return ptr;
}

__attribute__((weak)) int emunmap(void *addr, size_t length) {
    int result = ::munmap(addr, length);
    if (result < 0) {
        throw Exception("Failed to munmap file");
    }

    return result;
}

} // namespace EthosU

/****************************************************************************
 * TFL micro helpers
 ****************************************************************************/
namespace {
size_t getShapeSize(const flatbuffers::Vector<int32_t> *shape) {
    size_t size = 1;

    if (shape == nullptr) {
        throw EthosU::Exception("getShapeSize(): nullptr arg");
    }

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
    case tflite::TensorType::TensorType_INT32:
    case tflite::TensorType::TensorType_FLOAT32:
        return 4;
    default:
        throw EthosU::Exception("Unsupported tensor type");
    }
}

vector<size_t> getSubGraphDims(const tflite::SubGraph *subgraph, const flatbuffers::Vector<int32_t> *tensorMap) {
    vector<size_t> dims;

    if (subgraph == nullptr || tensorMap == nullptr) {
        throw EthosU::Exception("getSubGraphDims(): nullptr arg(s)");
    }

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
 * Semantic Version
 ****************************************************************************/

bool SemanticVersion::operator==(const SemanticVersion &other) {
    return other.major == major && other.minor == minor && other.patch == patch;
}

bool SemanticVersion::operator<(const SemanticVersion &other) {
    if (other.major > major)
        return true;
    if (other.minor > minor)
        return true;
    return other.patch > patch;
}

bool SemanticVersion::operator<=(const SemanticVersion &other) {
    return *this < other || *this == other;
}

bool SemanticVersion::operator!=(const SemanticVersion &other) {
    return !(*this == other);
}

bool SemanticVersion::operator>(const SemanticVersion &other) {
    return !(*this <= other);
}

bool SemanticVersion::operator>=(const SemanticVersion &other) {
    return !(*this < other);
}

ostream &operator<<(ostream &out, const SemanticVersion &v) {
    return out << "{ major=" << unsigned(v.major) << ", minor=" << unsigned(v.minor) << ", patch=" << unsigned(v.patch)
               << " }";
}

/****************************************************************************
 * Device
 ****************************************************************************/
Device::Device(const char *device) {
    fd = eopen(device, O_RDWR | O_NONBLOCK);
}

Device::~Device() {
    eclose(fd);
}

int Device::ioctl(unsigned long cmd, void *data) const {
    return eioctl(fd, cmd, data);
}

Capabilities Device::capabilities() const {
    ethosu_uapi_device_capabilities uapi;
    (void)eioctl(fd, ETHOSU_IOCTL_CAPABILITIES_REQ, static_cast<void *>(&uapi));

    Capabilities capabilities(
        HardwareId(uapi.hw_id.version_status,
                   SemanticVersion(uapi.hw_id.version_major, uapi.hw_id.version_minor),
                   SemanticVersion(uapi.hw_id.product_major),
                   SemanticVersion(uapi.hw_id.arch_major_rev, uapi.hw_id.arch_minor_rev, uapi.hw_id.arch_patch_rev)),
        HardwareConfiguration(uapi.hw_cfg.macs_per_cc, uapi.hw_cfg.cmd_stream_version, bool(uapi.hw_cfg.custom_dma)),
        SemanticVersion(uapi.driver_major_rev, uapi.driver_minor_rev, uapi.driver_patch_rev));
    return capabilities;
}

/****************************************************************************
 * Buffer
 ****************************************************************************/

Buffer::Buffer(const Device &device, const size_t capacity) : fd(-1), dataPtr(nullptr), dataCapacity(capacity) {
    ethosu_uapi_buffer_create uapi = {static_cast<uint32_t>(dataCapacity)};
    fd                             = device.ioctl(ETHOSU_IOCTL_BUFFER_CREATE, static_cast<void *>(&uapi));

    void *d;
    try {
        d = emmap(nullptr, dataCapacity, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    } catch (std::exception &e) {
        try {
            eclose(fd);
        } catch (...) { std::throw_with_nested(e); }
    }
    dataPtr = reinterpret_cast<char *>(d);
}

Buffer::~Buffer() {
    emunmap(dataPtr, dataCapacity);
    eclose(fd);
}

size_t Buffer::capacity() const {
    return dataCapacity;
}

void Buffer::clear() const {
    resize(0, 0);
}

char *Buffer::data() const {
    return dataPtr + offset();
}

void Buffer::resize(size_t size, size_t offset) const {
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

Network::Network(const Device &device, shared_ptr<Buffer> &buffer) : fd(-1), buffer(buffer) {
    // Create buffer handle
    ethosu_uapi_network_create uapi;
    uapi.type = ETHOSU_UAPI_NETWORK_BUFFER;
    uapi.fd   = buffer->getFd();
    fd        = device.ioctl(ETHOSU_IOCTL_NETWORK_CREATE, static_cast<void *>(&uapi));

    try {
        parseModel(buffer->data());
    } catch (...) {
        eclose(fd);
        throw;
    }
}

Network::Network(const Device &device, const unsigned index) : fd(-1) {
    // Create buffer handle
    ethosu_uapi_network_create uapi;
    uapi.type  = ETHOSU_UAPI_NETWORK_INDEX;
    uapi.index = index;
    fd         = device.ioctl(ETHOSU_IOCTL_NETWORK_CREATE, static_cast<void *>(&uapi));

    try {
        ethosu_uapi_network_info info;
        ioctl(ETHOSU_IOCTL_NETWORK_INFO, static_cast<void *>(&info));

        for (uint32_t i = 0; i < info.ifm_count; i++) {
            ifmDims.push_back(info.ifm_size[i]);
        }

        for (uint32_t i = 0; i < info.ofm_count; i++) {
            ofmDims.push_back(info.ofm_size[i]);
        }
    } catch (...) {
        eclose(fd);
        throw;
    }
}

Network::~Network() {
    eclose(fd);
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

void Network::parseModel(const char *data) {
    // Create model handle
    const tflite::Model *model = tflite::GetModel(reinterpret_cast<const void *>(data));

    if (model->subgraphs() == nullptr) {
        EthosU::Exception("Failed to get subgraphs: nullptr");
    }

    // Get input dimensions for first subgraph
    auto *subgraph = *model->subgraphs()->begin();
    ifmDims        = getSubGraphDims(subgraph, subgraph->inputs());

    // Get output dimensions for last subgraph
    subgraph = *model->subgraphs()->rbegin();
    ofmDims  = getSubGraphDims(subgraph, subgraph->outputs());
}

/****************************************************************************
 * Inference
 ****************************************************************************/

Inference::~Inference() {
    eclose(fd);
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

int Inference::wait(int64_t timeoutNanos) const {
    struct pollfd pfd;
    pfd.fd      = fd;
    pfd.events  = POLLIN | POLLERR;
    pfd.revents = 0;

    // if timeout negative wait forever
    if (timeoutNanos < 0) {
        return eppoll(&pfd, 1, NULL, NULL);
    }

    struct timespec tmo_p;
    int64_t nanosec = 1000000000;
    tmo_p.tv_sec    = timeoutNanos / nanosec;
    tmo_p.tv_nsec   = timeoutNanos % nanosec;

    return eppoll(&pfd, 1, &tmo_p, NULL);
}

bool Inference::failed() const {
    ethosu_uapi_result_status uapi;

    eioctl(fd, ETHOSU_IOCTL_INFERENCE_STATUS, static_cast<void *>(&uapi));

    return uapi.status != ETHOSU_UAPI_STATUS_OK;
}

const std::vector<uint32_t> Inference::getPmuCounters() const {
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

uint64_t Inference::getCycleCounter() const {
    ethosu_uapi_result_status uapi;

    eioctl(fd, ETHOSU_IOCTL_INFERENCE_STATUS, static_cast<void *>(&uapi));

    return uapi.pmu_count.cycle_count;
}

int Inference::getFd() const {
    return fd;
}

const shared_ptr<Network> Inference::getNetwork() const {
    return network;
}

vector<shared_ptr<Buffer>> &Inference::getIfmBuffers() {
    return ifmBuffers;
}

vector<shared_ptr<Buffer>> &Inference::getOfmBuffers() {
    return ofmBuffers;
}

} // namespace EthosU
