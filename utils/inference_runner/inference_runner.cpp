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

#include <unistd.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <string>

using namespace std;
using namespace EthosU;

namespace
{
int defaultTimeout = 60;

void help(const string exe)
{
    cerr << "Usage: " << exe << " [ARGS]\n";
    cerr << "\n";
    cerr << "Arguments:\n";
    cerr << "    -h --help       Print this help message.\n";
    cerr << "    -n --network    File to read network from.\n";
    cerr << "    -i --ifm        File to read IFM from.\n";
    cerr << "    -o --ofm        File to write IFM to.\n";
    cerr << "    -t --timeout    Timeout in seconds (default " << defaultTimeout << ").\n";
    cerr << "    -p              Print OFM.\n";
    cerr << endl;
}

void rangeCheck(const int i, const int argc, const string arg)
{
    if (i >= argc)
    {
        cerr << "Error: Missing argument to '" << arg << "'" << endl;
        exit(1);
    }
}

shared_ptr<Buffer> allocAndFill(Device &device, const string filename)
{
    ifstream stream(filename, ios::binary);
    if (!stream.is_open())
    {
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

std::ostream &operator<<(std::ostream &os, Buffer &buf)
{
    char *c = buf.data();
    const char *end = c + buf.size();

    while (c < end)
    {
        os << hex << setw(2) << static_cast<int>(*c++) << " " << dec;
    }

    return os;
}

}

int main(int argc, char *argv[])
{
    const string exe = argv[0];
    string networkArg;
    list<string> ifmArg;
    string ofmArg;
    int timeout = defaultTimeout;
    bool print = false;

    for (int i = 1; i < argc; ++i)
    {
        const string arg(argv[i]);

        if (arg == "-h" || arg == "--help")
        {
            help(exe);
            exit(1);
        }
        else if (arg == "--network" || arg == "-n")
        {
            rangeCheck(++i, argc, arg);
            networkArg = argv[i];
        }
        else if (arg == "--ifm" || arg == "-i")
        {
            rangeCheck(++i, argc, arg);
            ifmArg.push_back(argv[i]);
        }
        else if (arg == "--ofm" || arg == "-o")
        {
            rangeCheck(++i, argc, arg);
            ofmArg = argv[i];
        }
        else if (arg == "--timeout" || arg == "-t")
        {
            rangeCheck(++i, argc, arg);
            timeout = std::stoi(argv[i]);
        }
        else if (arg == "-p")
        {
            print = true;
        }
        else
        {
            cerr << "Error: Invalid argument '" << arg << "'" << endl;
            help(exe);
            exit(1);
        }
    }

    if (networkArg.empty())
    {
        cerr << "Error: Missing 'network' argument" << endl;
        exit(1);
    }

    if (ifmArg.empty())
    {
        cerr << "Error: Missing 'ifm' argument" << endl;
        exit(1);
    }

    if (ofmArg.empty())
    {
        cerr << "Error: Missing 'ofm' argument" << endl;
        exit(1);
    }

    try
    {
        Device device;

        cout << "Send ping" << endl;
        device.ioctl(ETHOSU_IOCTL_PING);

        cout << "Create network" << endl;
        shared_ptr<Buffer> networkBuffer = allocAndFill(device, networkArg);
        shared_ptr<Network> network = make_shared<Network>(device, networkBuffer);

        cout << "Queue inferences" << endl;
        list<shared_ptr<Inference>> inferences;

        for (auto &filename: ifmArg)
        {
            cout << "Create inference" << endl;
            shared_ptr<Buffer> ifmBuffer = allocAndFill(device, filename);
            shared_ptr<Buffer> ofmBuffer = make_shared<Buffer>(device, 128 * 1024);
            shared_ptr<Inference> inference = make_shared<Inference>(network, ifmBuffer, ofmBuffer);
            inferences.push_back(inference);
        }

        cout << "Wait for inferences" << endl;

        int ofmIndex = 0;
        for (auto &inference: inferences)
        {
            inference->wait(timeout);

            string status = inference->failed() ? "failed" : "success";
            cout << "Inference status: " << status << endl;

            string ofmFilename = ofmArg + "." + to_string(ofmIndex);
            ofstream ofmStream(ofmFilename, ios::binary);
            if (!ofmStream.is_open())
            {
                cerr << "Error: Failed to open '" << ofmFilename << "'" << endl;
                exit(1);
            }

            if (!inference->failed())
            {
                shared_ptr<Buffer> ofmBuffer = inference->getOfmBuffer();

                cout << "OFM size: " << ofmBuffer->size() << endl;

                if (print)
                {
                    cout << "OFM data: " << *ofmBuffer << endl;
                }

                ofmStream.write(ofmBuffer->data(), ofmBuffer->size());
            }

            ofmIndex++;
        }
    }
    catch (Exception &e)
    {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}
