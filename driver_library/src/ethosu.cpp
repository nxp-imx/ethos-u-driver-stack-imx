/*
 * Copyright (c) 2020-2021 Arm Limited. All rights reserved.
 * Copyright 2020-2022 NXP
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
#include "flatbuffers/flexbuffers.h"

#include <ethosu.hpp>
#include <ethosu.h>

#include <algorithm>
#include <queue>
#include <exception>
#include <iostream>
#include <fstream>

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
struct ModelInfo{
    vector<size_t> inputDims;
    vector<size_t> outputDims;

    vector<vector<size_t>> inputShapes;
    vector<vector<size_t>> outputShapes;

    vector<int> inputTypes;
    vector<int> outputTypes;

    std::vector<int32_t> inputDataOffset;
    std::vector<int32_t> outputDataOffset;

    bool isVelaModel;
};


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
	//Fix object detection issue
	size = size == 1 ? 1024 : size;
        size *= getTensorTypeSize(tensor->type());

        if (size > 0) {
            dims.push_back(size);
        }
    }

    return dims;
}

vector<vector<size_t>> getSubGraphShapes(const tflite::SubGraph *subgraph, const flatbuffers::Vector<int32_t> *tensorMap) {
    vector<vector<size_t>> shapes;

    for (auto index = tensorMap->begin(); index != tensorMap->end(); ++index) {
        auto tensor = subgraph->tensors()->Get(*index);
        auto shape = tensor->shape();

	vector<size_t> tmp;
        for (auto it = shape->begin(); it != shape->end(); ++it) {
            tmp.push_back(*it);
        }

        shapes.push_back(tmp);
    }

    return shapes;
}

vector<int> getSubGraphTypes(const tflite::SubGraph *subgraph, const flatbuffers::Vector<int32_t> *tensorMap) {
    vector<int> types;

    for (auto index = tensorMap->begin(); index != tensorMap->end(); ++index) {
        auto tensor = subgraph->tensors()->Get(*index);
        types.push_back(tensor->type());
    }

    return types;
}

#define OFFLINE_MEM_ALLOC_METADATA "OfflineMemoryAllocation"
ModelInfo getModelInfo(const tflite::Model *model) {
    ModelInfo info;
    info.isVelaModel = false;
    //Get adress offset
    auto *md = model->metadata();
    const int32_t* address_offsets = NULL;
    if (md) {
        for (uint32_t mid=0; mid < md->size(); ++mid) {
            const auto meta = md->Get(mid);
            if (meta->name()->str() != OFFLINE_MEM_ALLOC_METADATA)
                continue;
            // grab raw buffer and dump it..
            auto meta_vec = model->buffers()->Get(meta->buffer())->data();
            address_offsets = reinterpret_cast<const int32_t*>(meta_vec->data() + 12);
            info.isVelaModel = true;
            }
    }

    //Get input info
    auto *subgraph = *model->subgraphs()->begin();
    auto tensorMap = subgraph->inputs();
    if (subgraph == nullptr || tensorMap == nullptr) {
        throw EthosU::Exception("getSubGraphDims(): nullptr arg(s)");
    }

    for (auto index = tensorMap->begin(); index != tensorMap->end(); ++index) {
        auto tensor = subgraph->tensors()->Get(*index);
        auto shape = tensor->shape();
        size_t size = 1;

        vector<size_t> tmp;
        for (auto it = shape->begin(); it != shape->end(); ++it) {
            tmp.push_back(*it);
            size *= *it;
        }
        size *= getTensorTypeSize(tensor->type());

        info.inputTypes.push_back(tensor->type());
        info.inputDims.push_back(size);
        info.inputShapes.push_back(tmp);
        if (address_offsets != NULL) {
            info.inputDataOffset.push_back(address_offsets[*index]);
        }
    }

    //Get output info
    subgraph = *model->subgraphs()->rbegin();
    tensorMap = subgraph->outputs();
    if (subgraph == nullptr || tensorMap == nullptr) {
        throw EthosU::Exception("getSubGraphDims(): nullptr arg(s)");
    }

    auto op = subgraph->operators()->end() - 1;
    auto opcode = model->operator_codes()->Get(op->opcode_index());
    if (opcode->builtin_code() == tflite::BuiltinOperator_CUSTOM
           && opcode->custom_code()->str() == "TFLite_Detection_PostProcess") {
        constexpr int kBatchSize = 1;
        constexpr int kNumCoordBox = 4;
        auto *data = op->custom_options();
        const flexbuffers::Map& m = flexbuffers::GetRoot(data->data(), data->size()).AsMap();
        const size_t num_detected_boxes =
                    m["max_detections"].AsInt32() * m["max_classes_per_detection"].AsInt32();

        auto size = getTensorTypeSize(tflite::TensorType::TensorType_FLOAT32);
        info.outputShapes.push_back({kBatchSize, num_detected_boxes, kNumCoordBox});
        info.outputDims.push_back(size * kBatchSize * num_detected_boxes * kNumCoordBox);
        info.outputShapes.push_back({kBatchSize, num_detected_boxes});
        info.outputDims.push_back(size * kBatchSize * num_detected_boxes);
        info.outputShapes.push_back({kBatchSize, num_detected_boxes});
        info.outputDims.push_back(size * kBatchSize * num_detected_boxes);
        info.outputShapes.push_back({1});
        info.outputDims.push_back(size);

        for (auto index = tensorMap->begin(); index != tensorMap->end(); ++index) {
            if (address_offsets != NULL) {
                info.outputDataOffset.push_back(address_offsets[*index]);
            }
            info.outputTypes.push_back(tflite::TensorType::TensorType_FLOAT32);
        }
    } else {
        for (auto index = tensorMap->begin(); index != tensorMap->end(); ++index) {
            auto tensor = subgraph->tensors()->Get(*index);
            auto shape = tensor->shape();
            size_t size = 1;

            vector<size_t> tmp;
            for (auto it = shape->begin(); it != shape->end(); ++it) {
                tmp.push_back(*it);
                size *= *it;
            }
            size *= getTensorTypeSize(tensor->type());

            info.outputTypes.push_back(tensor->type());
            info.outputDims.push_back(size);
            info.outputShapes.push_back(tmp);
            if (address_offsets != NULL) {
                info.outputDataOffset.push_back(address_offsets[*index]);
            }
        }
    }

    return info;
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

    //Add some delay to fix the issue about communication instability
    usleep(10 * 1000);
    //Send Ping
    this->ioctl(ETHOSU_IOCTL_PING);
    //Send version request
    this->ioctl(ETHOSU_IOCTL_VERSION_REQ);
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

Network::Network(const Device &device, shared_ptr<Buffer> &buffer) : device(device), fd(-1), buffer(buffer) {
    // Create buffer handle
    ethosu_uapi_network_create uapi;
    uapi.fd = buffer->getFd();
    fd      = device.ioctl(ETHOSU_IOCTL_NETWORK_CREATE, static_cast<void *>(&uapi));

    // Create model handle
    const tflite::Model *model = tflite::GetModel(reinterpret_cast<void *>(buffer->data()));
    if (model->subgraphs() == nullptr) {
        try {
            eclose(fd);
        } catch (...) { std::throw_with_nested(EthosU::Exception("Failed to get subgraphs: nullptr")); }
    }

    auto info = getModelInfo(model);
    ifmDims         = info.inputDims;
    ifmShapes       = info.inputShapes;
    ifmTypes        = info.inputTypes;
    inputDataOffset = info.inputDataOffset;
    ofmDims         = info.outputDims;
    ofmShapes       = info.outputShapes;
    ofmTypes        = info.outputTypes;
    outputDataOffset= info.outputDataOffset;
    _isVelaModel    = info.isVelaModel;
}
int32_t Network::getInputDataOffset(int index){
    if (index > inputDataOffset.size() + 1){
        throw Exception("Invalid input index or non vela model");
    }
    return inputDataOffset[index];
}

int32_t Network::getOutputDataOffset(int index){
    if (index > outputDataOffset.size() + 1){
        throw Exception("Invalid output index or non vela model");
    }
    return outputDataOffset[index];
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

const std::vector<std::vector<size_t>> &Network::getIfmShapes() const {
    return ifmShapes;
}

const std::vector<int> &Network::getIfmTypes() const {
    return ifmTypes;
}

size_t Network::getIfmSize() const {
    size_t size = 0;

    for (auto s : ifmDims) {
        size += s;
    }

    return size;
}

size_t Network::getInputCount() const {
    return inputDataOffset.size();
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

size_t Network::getOutputCount() const {
    return outputDataOffset.size();
}

void Network::convertInputData(uint8_t* data, int ifmIndex) {
#define MODEL_INPUT_MEAN 127.5f
#define MODEL_INPUT_STD 127.5f

    auto shapes = ifmShapes[ifmIndex];
    int size = shapes[2] * shapes[1] * shapes[3];

    switch (ifmTypes[ifmIndex])
    {
        case tflite::TensorType::TensorType_UINT8:
            break;
        case tflite::TensorType::TensorType_INT8:
            for (int i = size - 1; i >= 0; i--)
            {
                reinterpret_cast<int8_t*>(data)[i] =
                    static_cast<int>(data[i]) - 127;
            }
            break;
        case tflite::TensorType::TensorType_FLOAT32:
            for (int i = size - 1; i >= 0; i--)
            {
                reinterpret_cast<float*>(data)[i] =
                    (static_cast<int>(data[i]) - MODEL_INPUT_MEAN) / MODEL_INPUT_STD;
            }
            break;
        default:
            assert("Unknown input tensor data type");
    }
}

const std::vector<std::vector<size_t>> &Network::getOfmShapes() const {
    return ofmShapes;
}

const std::vector<int> &Network::getOfmTypes() const {
    return ofmTypes;
}

const Device &Network::getDevice() const {
    return device;
}

bool Network::isVelaModel() const {
    return _isVelaModel;
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
    uapi.ifm_fd[uapi.ifm_count++] = arenaBuffer->getFd();
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

char* Inference::getInputData(int index){
    if (network->isVelaModel() && ifmBuffers.size() == 0) {
         int32_t offset = network->getInputDataOffset(index);
         return arenaBuffer->data() + offset;
    } else {
         return ifmBuffers[index]->data();
    }
}

char* Inference::getOutputData(int index){
    if (network->isVelaModel() && ofmBuffers.size() == 0) {
        int32_t offset = network->getOutputDataOffset(index);
        return arenaBuffer->data() + offset;
    } else {
        return ofmBuffers[index]->data();
    }
}

uint64_t Inference::getCycleCounter() const {
    ethosu_uapi_result_status uapi;

    eioctl(fd, ETHOSU_IOCTL_INFERENCE_STATUS, static_cast<void *>(&uapi));

    return uapi.pmu_count.cycle_count;
}

InferenceResult Inference::processOutput(float threshold, size_t numResults) {
    InferenceResult result;
    if (network->getOutputCount() > 1) {
        /* Object Detection Demo */
        auto output_locations = (float*)getOutputData(0);
        auto output_classes = (float*)getOutputData(1);
        auto output_scores = (float*)getOutputData(2);
        auto output_count = (float*)getOutputData(3);

	if(numResults > static_cast<size_t>(*output_count)) {
	    numResults = static_cast<size_t>(*output_count);
	}

        for (size_t j = 0; j < numResults; j++) {
	    std::vector<float> pos;
	    pos.push_back(output_locations[j * 4]);
	    pos.push_back(output_locations[j * 4 + 1]);
	    pos.push_back(output_locations[j * 4 + 2]);
	    pos.push_back(output_locations[j * 4 + 3]);

	    int label_num = static_cast<int>(output_classes[j]);
	    float scores = output_scores[j];
            result.push_back(std::make_tuple(label_num, scores, pos));
        }
    } else {
        /* Image Classification Demo */
        // Will contain top N results in ascending order.
        std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>,
        std::greater<std::pair<float, int>>> top_result_pq;

	auto ofmType = (tflite::TensorType)network->getOfmTypes()[0];
	auto data = getOutputData(0);
        const long count = network->getOfmDims()[0] / getTensorTypeSize(ofmType);
        for (int i = 0; i < count; ++i) {
            float value;
            switch (ofmType) {
                case tflite::TensorType::TensorType_FLOAT32: {
                    const float* predictions = reinterpret_cast<const float*>(data);
                    value = predictions[i];
                    break;
                }
                case tflite::TensorType::TensorType_UINT8: {
                    const uint8_t* predictions = reinterpret_cast<const uint8_t*>(data);
                    value = predictions[i] / 255.0;
                    break;
                }
                case tflite::TensorType::TensorType_INT8: {
                    const int8_t* predictions = reinterpret_cast<const int8_t*>(data);
                    value = ((int)predictions[i] + 128) / 255.0;
                    break;
                }
                default:
                    cout << "Unknown output tensor data type" << endl;
                    exit(1);
            }
            // Only add it if it beats the threshold and has a chance at being in the top N.
            if (value < threshold) {
                continue;
            }

            top_result_pq.push(std::pair<float, int>(value, i));

            // If at capacity, kick the smallest value out.
            if (top_result_pq.size() > numResults) {
                top_result_pq.pop();
            }
        }

        // Copy to output vector and reverse into descending order.
        while (!top_result_pq.empty()) {
	    auto tmp = top_result_pq.top();
            result.push_back(std::make_tuple(tmp.second, tmp.first, std::vector<float>()));
            top_result_pq.pop();
        }
        std::reverse(result.begin(), result.end());
    }
    return result;
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

/****************************************************************************
 * Interpreter
 ****************************************************************************/
Interpreter::Interpreter(const char *model, const char *_device, int64_t _arenaSizeOfMB):
             device(_device), arenaSizeOfMB(_arenaSizeOfMB){
    //Send capabilities request
    Capabilities capabilities = device.capabilities();

    cout << "Capabilities:" << endl
         << "\tversion_status:" << unsigned(capabilities.hwId.versionStatus) << endl
         << "\tversion:" << capabilities.hwId.version << endl
         << "\tproduct:" << capabilities.hwId.product << endl
         << "\tarchitecture:" << capabilities.hwId.architecture << endl
         << "\tdriver:" << capabilities.driver << endl
         << "\tmacs_per_cc:" << unsigned(capabilities.hwCfg.macsPerClockCycle) << endl
         << "\tcmd_stream_version:" << unsigned(capabilities.hwCfg.cmdStreamVersion) << endl
         << "\tcustom_dma:" << std::boolalpha << capabilities.hwCfg.customDma << endl;

    // Init network
    ifstream stream(model, ios::binary);
    if (!stream.is_open()) {
         throw Exception("Failed to open model file");
    }

    stream.seekg(0, ios_base::end);
    size_t size = stream.tellg();
    stream.seekg(0, ios_base::beg);

    networkBuffer = make_shared<Buffer>(device, size);
    networkBuffer->resize(size);
    stream.read(networkBuffer->data(), size);
    network = make_shared<Network>(device, networkBuffer);
    if (!network->isVelaModel()) {
         throw Exception("Only support models compiled by vela.");
    }

    // Init tensor arena buffer
    size_t arena_buffer_size = arenaSizeOfMB << 20;
    arenaBuffer = make_shared<Buffer>(device, arena_buffer_size);
    arenaBuffer->resize(arena_buffer_size);
}

void Interpreter::SetPmuCycleCounters(vector<uint8_t> counters, bool cycleCounter) {
    if (counters.size() != ETHOSU_PMU_EVENT_MAX){
        throw Exception("PMU event count is invalid.");
    }

    pmuCounters = counters;
    enableCycleCounter = cycleCounter;
}

void Interpreter::Invoke(int64_t timeoutNanos) {
    inference = make_shared<Inference>(network, arenaBuffer,
				pmuCounters, enableCycleCounter);
    inference->wait(timeoutNanos);

    if (inference->failed()) {
        throw Exception("Failed to invoke.");
    }
}

std::vector<uint32_t> Interpreter::GetPmuCounters() {
    return inference->getPmuCounters();
}

uint64_t Interpreter::GetCycleCounter() {
    return inference->getCycleCounter();
}

std::vector<TensorInfo> Interpreter::GetInputInfo() {
    std::vector<TensorInfo> ret;
    auto types = network->getIfmTypes();
    auto shapes = network->getIfmShapes();

    for (int i = 0; i < network->getInputCount(); i ++) {
        ret.push_back(TensorInfo{types[i], shapes[i]});
    }

    return ret;
}

std::vector<TensorInfo> Interpreter::GetOutputInfo(){
    std::vector<TensorInfo> ret;
    auto types = network->getOfmTypes();
    auto shapes = network->getOfmShapes();

    for (int i = 0; i < network->getOutputCount(); i ++) {
        ret.push_back(TensorInfo{types[i], shapes[i]});
    }

    return ret;
}

} // namespace EthosU
