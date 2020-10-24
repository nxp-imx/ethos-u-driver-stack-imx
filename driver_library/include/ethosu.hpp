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

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace EthosU {

class Exception : public std::exception {
public:
    Exception(const char *msg);
    virtual ~Exception() throw();
    virtual const char *what() const throw();

private:
    std::string msg;
};

class Device {
public:
    Device(const char *device = "/dev/ethosu0");
    virtual ~Device();

    int ioctl(unsigned long cmd, void *data = nullptr);

private:
    int fd;
};

class Buffer {
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

class Network {
public:
    Network(Device &device, std::shared_ptr<Buffer> &buffer);
    virtual ~Network();

    int ioctl(unsigned long cmd, void *data = nullptr);
    std::shared_ptr<Buffer> getBuffer();
    const std::vector<size_t> &getIfmDims() const;
    size_t getIfmSize() const;
    const std::vector<size_t> &getOfmDims() const;
    size_t getOfmSize() const;

private:
    int fd;
    std::shared_ptr<Buffer> buffer;
    std::vector<size_t> ifmDims;
    std::vector<size_t> ofmDims;
};

class Inference {
public:
    template <typename T>
    Inference(std::shared_ptr<Network> &network,
              const T &ifmBegin,
              const T &ifmEnd,
              const T &ofmBegin,
              const T &ofmEnd) :
        network(network) {
        std::copy(ifmBegin, ifmEnd, std::back_inserter(ifmBuffers));
        std::copy(ofmBegin, ofmEnd, std::back_inserter(ofmBuffers));
        std::vector<uint32_t> counterConfigs = initializeCounterConfig();

        create(counterConfigs, false);
    }
    template <typename T, typename U>
    Inference(std::shared_ptr<Network> &network,
              const T &ifmBegin,
              const T &ifmEnd,
              const T &ofmBegin,
              const T &ofmEnd,
              const U &counters,
              bool enableCycleCounter) :
        network(network) {
        std::copy(ifmBegin, ifmEnd, std::back_inserter(ifmBuffers));
        std::copy(ofmBegin, ofmEnd, std::back_inserter(ofmBuffers));
        std::vector<uint32_t> counterConfigs = initializeCounterConfig();

        if (counters.size() > counterConfigs.size())
            throw EthosU::Exception("PMU Counters argument to large.");

        std::copy(counters.begin(), counters.end(), counterConfigs.begin());
        create(counterConfigs, enableCycleCounter);
    }

    virtual ~Inference();

    int wait(int timeoutSec = -1);
    const std::vector<uint32_t> getPmuCounters();
    uint64_t getCycleCounter();
    bool failed();
    int getFd();
    std::shared_ptr<Network> getNetwork();
    std::vector<std::shared_ptr<Buffer>> &getIfmBuffers();
    std::vector<std::shared_ptr<Buffer>> &getOfmBuffers();

    static uint32_t getMaxPmuEventCounters();

private:
    void create(std::vector<uint32_t> &counterConfigs, bool enableCycleCounter);
    std::vector<uint32_t> initializeCounterConfig();

    int fd;
    std::shared_ptr<Network> network;
    std::vector<std::shared_ptr<Buffer>> ifmBuffers;
    std::vector<std::shared_ptr<Buffer>> ofmBuffers;
};

} // namespace EthosU
