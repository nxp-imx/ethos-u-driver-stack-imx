/*
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

#include <fstream>
#include <vector>
#include <tuple>
#include <queue>
#include <algorithm>
#include <cstdint>


#define DECODE_BUFFER_SIZE 1920 * 1080 * 3

using namespace std;

void IMAGE_Resize(uint8_t* srcData, int srcWidth, int srcHeight,
                  uint8_t* dstData, int dstWidth, int dstHeight, int channels);

int32_t IMAGE_Decode(const uint8_t* srcData, uint8_t* dstData,
                      int32_t dstWidth, int32_t dstHeight, int32_t dstChannels);

template <class T>
static int convertInputData(T* data, int size) {
#define MODEL_INPUT_MEAN 127.5f
#define MODEL_INPUT_STD 127.5f
    const std::type_info& type = typeid(T);

    if (type == typeid(uint8_t)) {
        //no need to convert
    } else if (type == typeid(int8_t)) {
        for (int i = size - 1; i >= 0; i --) {
            reinterpret_cast<int8_t*>(data)[i] =
                static_cast<int>(data[i]) - 127;
        }
    } else if (type == typeid(float)) {
        for (int i = size - 1; i >= 0; i --) {
            reinterpret_cast<float*>(data)[i] =
                (static_cast<int>(data[i]) - MODEL_INPUT_MEAN) / MODEL_INPUT_STD;
        }
    } else {
            cerr << "Unknown input tensor data type" << endl;
            return -1;
    }
    return 0;
}

template <class T>
static int getInputFromFile(const string &filename, T* inputData, vector<size_t> shape) {
    // Open IFM file
    ifstream stream(filename, ios::binary);
    if (!stream.is_open()) {
        cerr << "Error: Failed to open '" << filename << "'" << endl;
        return -1;
    }

    // Get IFM file size
    stream.seekg(0, ios_base::end);
    size_t size = stream.tellg();
    stream.seekg(0, ios_base::beg);

    // Set input buffer
    char s_buffer[DECODE_BUFFER_SIZE];
    stream.read(s_buffer, size);
    if (!stream) {
        cerr << "Error: Failed to read IFM" << endl;
        return -1;
    }
    IMAGE_Decode((uint8_t*)s_buffer, (uint8_t*)inputData, shape[1], shape[2], shape[3]);
    return convertInputData<T>(inputData, shape[1] * shape[2] * shape[3]);
}

typedef std::vector<std::tuple<int, float, std::vector<float>>> InferenceResult;
static InferenceResult getBoundingBoxes(std::vector<void*>data, size_t numResults = 10) {
    InferenceResult result;
    auto output_locations = (float*)data[0];
    auto output_classes = (float*)data[1];
    auto output_scores = (float*)data[2];
    auto output_count = (float*)data[3];
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
    return result;
}


template <class T>
static InferenceResult getTopN(T* data, float threshold, int count, float zp, float scale) {
   // Will contain top N results in ascending order.
   InferenceResult result;
   std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>,
   std::greater<std::pair<float, int>>> top_result_pq;

   for (int i = 0; i < count; ++i) {
       float value = (data[i] + zp) / scale;
       // Only add it if it beats the threshold and has a chance at being in the top N.
       if (value < threshold) {
           continue;
       }

       top_result_pq.push(std::pair<float, int>(value, i));

       // If at capacity, kick the smallest value out.
       if (top_result_pq.size() > 4) {
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
    return result;
}
