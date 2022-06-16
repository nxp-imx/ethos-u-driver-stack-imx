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

#include "common/pre_post_processing.h"

using namespace std;
using namespace EthosU;

namespace {
int64_t defaultTimeout = 60000000000;
int64_t defaultArenaSizeOfMB = 16;

void help(const string exe) {
    cerr << "Usage: " << exe << " [ARGS]\n";
    cerr << "\n";
    cerr << "Arguments:\n";
    cerr << "    -h --help       Print this help message.\n";
    cerr << "    -n --network    File to read network from.\n";
    cerr << "    -i --ifm        File to read IFM from.\n";
    cerr << "    -o --ofm        File to write IFM to.\n";
    cerr << "    -l --lbl        Lables file.\n";
    cerr << "    -P --pmu [0.." << Inference::getMaxPmuEventCounters() << "] eventid.\n";
    cerr << "                    PMU counter to enable followed by eventid, can be passed multiple times.\n";
    cerr << "    -C --cycles     Enable cycle counter for inference.\n";
    cerr << "    -t --timeout    Timeout in nanoseconds (default " << defaultTimeout << ").\n";
    cerr << "    -a --arena      TFLite-micro arena memory size (default " << defaultArenaSizeOfMB << "MB).\n";
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

// Takes a file name, and loads a list of labels from it, one per line, and
// returns a vector of the strings. It pads with empty strings so the length
// of the result is a multiple of 16, because our model expects that.
int readLabelsFile(const string& file_name, std::vector<string>* result,
                      size_t* found_label_count) {
  std::ifstream file(file_name);
  if (!file) {
          return -1;
  }
  result->clear();
  string line;
  while (std::getline(file, line)) {
    result->push_back(line);
  }
  *found_label_count = result->size();
  return 0;
}

shared_ptr<Inference> createInference(Device &device,
                                      shared_ptr<Network> &network,
                                      const string &filename,
                                      const std::vector<uint8_t> &counters,
                                      bool enableCycleCounter,
                                      int64_t arenaSizeOfMB = defaultArenaSizeOfMB) {
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

    // Create buffer for tensor arena
    size_t arena_buffer_size = arenaSizeOfMB << 20;
    shared_ptr<Buffer> arena_buffer = make_shared<Buffer>(device, arena_buffer_size);
    arena_buffer->resize(arena_buffer_size);
    auto inference = make_shared<Inference>(network, arena_buffer, counters, enableCycleCounter);

    // Set input buffer
    char s_buffer[DECODE_BUFFER_SIZE];
    stream.read(s_buffer, size);
    if (!stream) {
        cerr << "Error: Failed to read IFM" << endl;
        exit(1);
    }

    char* input_data = inference->getInputData(0);
    auto ifmShape = network->getIfmShapes()[0];
    IMAGE_Decode((uint8_t*)s_buffer, (uint8_t*)input_data, ifmShape[1], ifmShape[2], ifmShape[3]);
    network->convertInputData((uint8_t*)input_data, 0);

    return inference;
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
    string devArg = "/dev/ethosu0";
    string lblArg = "labels.txt";
    int64_t timeout         = defaultTimeout;
    bool print              = false;
    bool enableCycleCounter = false;
    std::vector<string> labels;
    size_t labelCount;
    int64_t arenaSizeOfMB      = defaultArenaSizeOfMB;


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
        } else if (arg == "--lbl" || arg == "-l") {
            rangeCheck(++i, argc, arg);
            lblArg = argv[i];
	} else if (arg == "--dev" || arg == "-d") {
            rangeCheck(++i, argc, arg);
            devArg = argv[i];
        } else if (arg == "--timeout" || arg == "-t") {
            rangeCheck(++i, argc, arg);
            timeout = stoll(argv[i]);
        } else if (arg == "--arena" || arg == "-a") {
            rangeCheck(++i, argc, arg);
            arenaSizeOfMB = stoll(argv[i]);
            cout << "Setting TFLite-micro arena size to " << arenaSizeOfMB << "MB" << endl;
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

    if (readLabelsFile(lblArg, &labels, &labelCount) != 0) {
        cerr << "Error: Can't read labels file " << lblArg << endl;
        exit(1);
    }

    try {
        Device device(devArg.c_str());

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
            inferences.push_back(createInference(device, network, filename, 
                                                 enabledCounters, enableCycleCounter, arenaSizeOfMB));
        }

        cout << "Wait for inferences" << endl;

        int ofmIndex = 0;
        for (auto &inference : inferences) {

            /* make sure the wait completes ok */
            try {
                inference->wait(timeout);
            } catch (std::exception &e) {
                cout << "Failed to wait for inference completion: " << e.what() << endl;
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
		for (auto result : inference->processOutput(0.23, 4)) {
	            cout << "\nDetected: " << labels[std::get<0>(result)] << ", confidence:"
		         << (int)(std::get<1>(result) * 100) << endl;

		    auto pos = std::get<2>(result);
		    if(pos.size() != 0) {
		        cout << "Location: ymin: " << pos[0] << ", xmin " << pos[1]
		             << ", ymax " << pos[2] << ", xmax " << pos[3] << endl;
		    }
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
