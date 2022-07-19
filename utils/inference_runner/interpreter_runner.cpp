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
    cerr << "    -l --lbl        Lables file.\n";
    cerr << "    -P --pmu [0.." << ETHOSU_PMU_EVENT_MAX << "] eventid.\n";
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

} // namespace

int main(int argc, char *argv[]) {
    const string exe = argv[0];
    string networkArg;
    list<string> ifmArg;
    vector<uint8_t> enabledCounters(ETHOSU_PMU_EVENT_MAX);
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

    if (readLabelsFile(lblArg, &labels, &labelCount) != 0) {
        cerr << "Error: Can't read labels file " << lblArg << endl;
        exit(1);
    }

    try {
        auto interpreter = make_unique<Interpreter>(networkArg.c_str(), devArg.c_str(), arenaSizeOfMB);
        interpreter->SetPmuCycleCounters(enabledCounters, enableCycleCounter);

        auto inputInfo = interpreter->GetInputInfo()[0];
        switch (inputInfo.type) {
            case TensorType::TensorType_UINT8:
                getInputFromFile<uint8_t>(ifmArg.front(),
                          interpreter->typed_input_buffer<uint8_t>(0), inputInfo.shape);
                break;
            case TensorType::TensorType_INT8:
                getInputFromFile<int8_t>(ifmArg.front(),
                          interpreter->typed_input_buffer<int8_t>(0), inputInfo.shape);
                break;
            case TensorType::TensorType_FLOAT32:
                getInputFromFile<float>(ifmArg.front(),
                          interpreter->typed_input_buffer<float>(0), inputInfo.shape);
                break;
            default:
                cerr << "Unknown input tensor data type" << endl;
                exit(1);
        }

        auto outputInfo = interpreter->GetOutputInfo();
        std::vector<void*> outputData;
        for (size_t i = 0; i < outputInfo.size(); i ++) {
            switch (outputInfo[i].type) {
                case TensorType::TensorType_UINT8:
                    outputData.push_back(interpreter->typed_output_buffer<uint8_t>(i));
                    break;
                case TensorType::TensorType_INT8:
                    outputData.push_back(interpreter->typed_output_buffer<int8_t>(i));
                    break;
                case TensorType::TensorType_FLOAT32:
                    outputData.push_back(interpreter->typed_output_buffer<float>(i));
                    break;
                default:
                    cerr << "Unknown output tensor data type" << endl;
                    exit(1);
            }
        }
        

        interpreter->Invoke();
        /* The inference completed and has ok status */
        for (auto result : getBoundingBoxes(outputData, 4)) {
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
             ETHOSU_PMU_EVENT_MAX) {

             const std::vector<uint32_t> pmus = interpreter->GetPmuCounters();
             cout << "PMUs : [";
             for (auto p : pmus) {
                 cout << " " << p;
             }
             cout << " ]" << endl;
          }
          if (enableCycleCounter)
              cout << "Cycle counter: " << interpreter->GetCycleCounter() << endl;
    } catch (Exception &e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}
