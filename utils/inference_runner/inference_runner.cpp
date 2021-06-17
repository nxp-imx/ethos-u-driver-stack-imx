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

#include <ethosu.hpp>
#include <uapi/ethosu.h>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <stdio.h>
#include <string>
#include <unistd.h>

using namespace std;
using namespace EthosU;

namespace {
int defaultTimeout = 60;

void help(const string exe) {
    cerr << "Usage: " << exe << " [ARGS]\n";
    cerr << "\n";
    cerr << "Arguments:\n";
    cerr << "    -h --help       Print this help message.\n";
    cerr << "    -n --network    File to read network from.\n";
    cerr << "    -i --ifm        File to read IFM from.\n";
    cerr << "    -o --ofm        File to write IFM to.\n";
    cerr << "    -P --pmu [0.." << Inference::getMaxPmuEventCounters() << "] eventid.\n";
    cerr << "                    PMU counter to enable followed by eventid, can be passed multiple times.\n";
    cerr << "    -C --cycles     Enable cycle counter for inference.\n";
    cerr << "    -t --timeout    Timeout in seconds (default " << defaultTimeout << ").\n";
    cerr << "    -p              Print OFM.\n";
    cerr << endl;
}

void rangeCheck(const int i, const int argc, const string arg) {
    if (i >= argc) {
        cerr << "Error: Missing argument to '" << arg << "'" << endl;
        exit(1);
    }
}

shared_ptr<Buffer> allocAndFill(Device &device, const string filename) {
    ifstream stream(filename, ios::binary);
    if (!stream.is_open()) {
        cerr << "Error: Failed to open '" << filename << "'" << endl;
        exit(1);
    }

    stream.seekg(0, ios_base::end);
    size_t size = stream.tellg();
    stream.seekg(0, ios_base::beg);

    shared_ptr<Buffer> buffer = make_shared<Buffer>(device, size);
    buffer->resize(size);
    stream.read(buffer->data(), size);

    return buffer;
}

shared_ptr<Inference> createInference(Device &device,
                                      shared_ptr<Network> &network,
                                      const string &filename,
                                      const std::vector<uint8_t> &counters,
                                      bool enableCycleCounter) {
    // Open IFM file
    ifstream stream(filename, ios::binary);
    if (!stream.is_open()) {
        cerr << "Error: Failed to open '" << filename << "'" << endl;
        exit(1);
    }

    // Get IFM file size
    stream.seekg(0, ios_base::end);
    size_t size = stream.tellg();
    stream.seekg(0, ios_base::beg);

    if (size != network->getIfmSize()) {
        cerr << "Error: IFM size does not match network size. filename=" << filename << ", size=" << size
             << ", network=" << network->getIfmSize() << endl;
        exit(1);
    }

    // Create IFM buffers
    vector<shared_ptr<Buffer>> ifm;
    for (auto size : network->getIfmDims()) {
        shared_ptr<Buffer> buffer = make_shared<Buffer>(device, size);
        buffer->resize(size);
        stream.read(buffer->data(), size);

        if (!stream) {
            cerr << "Error: Failed to read IFM" << endl;
            exit(1);
        }

        ifm.push_back(buffer);
    }

    // Create OFM buffers
    vector<shared_ptr<Buffer>> ofm;
    for (auto size : network->getOfmDims()) {
        ofm.push_back(make_shared<Buffer>(device, size));
    }

    return make_shared<Inference>(
        network, ifm.begin(), ifm.end(), ofm.begin(), ofm.end(), counters, enableCycleCounter);
}

ostream &operator<<(ostream &os, Buffer &buf) {
    char *c         = buf.data();
    const char *end = c + buf.size();

    while (c < end) {
        os << hex << setw(2) << static_cast<int>(*c++) << " " << dec;
    }

    return os;
}

} // namespace

int main(int argc, char *argv[]) {
    const string exe = argv[0];
    string networkArg;
    list<string> ifmArg;
    vector<uint8_t> enabledCounters(Inference::getMaxPmuEventCounters());
    string ofmArg;
    int timeout             = defaultTimeout;
    bool print              = false;
    bool enableCycleCounter = false;

    for (int i = 1; i < argc; ++i) {
        const string arg(argv[i]);

        if (arg == "-h" || arg == "--help") {
            help(exe);
            exit(1);
        } else if (arg == "--network" || arg == "-n") {
            rangeCheck(++i, argc, arg);
            networkArg = argv[i];
        } else if (arg == "--ifm" || arg == "-i") {
            rangeCheck(++i, argc, arg);
            ifmArg.push_back(argv[i]);
        } else if (arg == "--ofm" || arg == "-o") {
            rangeCheck(++i, argc, arg);
            ofmArg = argv[i];
        } else if (arg == "--timeout" || arg == "-t") {
            rangeCheck(++i, argc, arg);
            timeout = stoi(argv[i]);
        } else if (arg == "--pmu" || arg == "-P") {
            unsigned pmu = 0, event = 0;
            rangeCheck(++i, argc, arg);
            pmu = stoi(argv[i]);

            rangeCheck(++i, argc, arg);
            event = stoi(argv[i]);

            if (pmu >= enabledCounters.size()) {
                cerr << "PMU out of bounds!" << endl;
                help(exe);
                exit(1);
            }
            cout << argv[i] << " -> Enabling " << pmu << " with event " << event << endl;
            enabledCounters[pmu] = event;
        } else if (arg == "--cycles" || arg == "-C") {
            enableCycleCounter = true;
        } else if (arg == "-p") {
            print = true;
        } else {
            cerr << "Error: Invalid argument '" << arg << "'" << endl;
            help(exe);
            exit(1);
        }
    }

    if (networkArg.empty()) {
        cerr << "Error: Missing 'network' argument" << endl;
        exit(1);
    }

    if (ifmArg.empty()) {
        cerr << "Error: Missing 'ifm' argument" << endl;
        exit(1);
    }

    if (ofmArg.empty()) {
        cerr << "Error: Missing 'ofm' argument" << endl;
        exit(1);
    }

    try {
        Device device;

        cout << "Send Ping" << endl;
        device.ioctl(ETHOSU_IOCTL_PING);

        cout << "Send version request" << endl;
        device.ioctl(ETHOSU_IOCTL_VERSION_REQ);

        cout << "Send capabilities request" << endl;
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

        /* Create network */
        cout << "Create network" << endl;
        shared_ptr<Buffer> networkBuffer = allocAndFill(device, networkArg);
        shared_ptr<Network> network      = make_shared<Network>(device, networkBuffer);

        /* Create one inference per IFM */
        list<shared_ptr<Inference>> inferences;
        for (auto &filename : ifmArg) {
            cout << "Create inference" << endl;
            inferences.push_back(createInference(device, network, filename, enabledCounters, enableCycleCounter));
        }

        cout << "Wait for inferences" << endl;

        int ofmIndex = 0;
        for (auto &inference : inferences) {

            /* make sure the wait completes ok */
            if (inference->wait(timeout) <= 0) {
                cout << "Failed to wait for inference completion" << endl;
                exit(1);
            }

            string status = inference->failed() ? "failed" : "success";
            cout << "Inference status: " << status << endl;

            string ofmFilename = ofmArg + "." + to_string(ofmIndex);
            ofstream ofmStream(ofmFilename, ios::binary);
            if (!ofmStream.is_open()) {
                cerr << "Error: Failed to open '" << ofmFilename << "'" << endl;
                exit(1);
            }

            if (!inference->failed()) {
                /* The inference completed and has ok status */
                for (auto &ofmBuffer : inference->getOfmBuffers()) {
                    cout << "OFM size: " << ofmBuffer->size() << endl;

                    if (print) {
                        cout << "OFM data: " << *ofmBuffer << endl;
                    }

                    ofmStream.write(ofmBuffer->data(), ofmBuffer->size());
                }

                /* Read out PMU counters if configured */
                if (std::count(enabledCounters.begin(), enabledCounters.end(), 0) <
                    Inference::getMaxPmuEventCounters()) {

                    const std::vector<uint32_t> pmus = inference->getPmuCounters();
                    cout << "PMUs : [";
                    for (auto p : pmus) {
                        cout << " " << p;
                    }
                    cout << " ]" << endl;
                }
                if (enableCycleCounter)
                    cout << "Cycle counter: " << inference->getCycleCounter() << endl;
            }

            ofmIndex++;
        }
    } catch (Exception &e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}
