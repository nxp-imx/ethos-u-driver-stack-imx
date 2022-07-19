/* Copyright 2017 The TensorFlow Authors. All Rights Reserved.
   Copyright 2018-2022 NXP. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

/* File modified by NXP. Changes are described in file
   /middleware/eiq/tensorflow-lite/readme.txt in section "Release notes" */

#include <fstream>
#include <iostream>
#include <unistd.h>
#include <string>
#include <vector>
#include <unistd.h>

#include "pre_post_processing.h"
#include <assert.h>

uint8_t s_buffer[DECODE_BUFFER_SIZE];

int32_t IMAGE_Decode(const uint8_t* srcData, uint8_t* dstData,
                      int32_t dstWidth, int32_t dstHeight, int32_t dstChannels)
{
    const int32_t headerSize =
        *((const int32_t*)(srcData + 10));
    const int32_t width = *((const int32_t*)(srcData + 18));
    const int32_t height = *((const int32_t*)(srcData + 22));
    const int32_t bpp = *((const int32_t*)(srcData + 28));
    const int channels = bpp / 8;

    // there may be padding bytes when the width is not a multiple of 4 bytes
    // 8 * channels == bits per pixel
    const int rowSize = (8 * channels * width + 31) / 32 * 4;

    // if height is negative, data layout is top down
    // otherwise, it's bottom up
    bool topDown = (height < 0);

    if (width * height * channels > DECODE_BUFFER_SIZE)
    {
        return -1;
    }

    const uint8_t* bmpPixels = &srcData[headerSize];
    for (int i = 0; i < height; i++)
    {
        int srcPos;
        int dstPos;

        for (int j = 0; j < width; j++)
        {
            if (!topDown)
            {
                srcPos = ((height - 1 - i) * rowSize) + j * channels;
            }
            else
            {
                srcPos = i * rowSize + j * channels;
            }

            dstPos = (i * width + j) * channels;

            switch (channels)
            {
                case 1:
                    s_buffer[dstPos] = bmpPixels[srcPos];
                    break;
                case 3:
                    // BGR -> RGB
                    s_buffer[dstPos] = bmpPixels[srcPos + 2];
                    s_buffer[dstPos + 1] = bmpPixels[srcPos + 1];
                    s_buffer[dstPos + 2] = bmpPixels[srcPos];
                    break;
                default:
                    return -1;
            }
        }
    }

    assert(channels == dstChannels);
    IMAGE_Resize(s_buffer, width, height, dstData, dstWidth, dstHeight, channels);

    return 0;
}

/* Simple resize using nearest neighbor */
void IMAGE_Resize(uint8_t* srcData, int srcWidth, int srcHeight,
                  uint8_t* dstData, int dstWidth, int dstHeight, int channels)
{
    double dx = 1.0 * dstWidth / srcWidth;
    double dy = 1.0 * dstHeight / srcHeight;

    int step = channels * srcWidth;
    int scale_step = channels * dstWidth;

    int i, j, k, pre_i, pre_j, after_i, after_j;
    if (channels == 1)
    {
        for (i = 0; i < dstHeight; i++)
        {
            for (j = 0; j < dstWidth; j++)
            {
                dstData[(dstHeight - 1 - i) * scale_step + j] = 0;
            }
        }

        for (i = 0; i < dstHeight; i++)
        {
            for (j = 0; j < dstWidth; j++)
            {
              after_i = i;
              after_j = j;
              pre_i = (int)(after_i / dy + 0);
              pre_j = (int)(after_j / dx + 0);
              if (pre_i >= 0 && pre_i < srcHeight && pre_j >= 0 && pre_j < srcWidth)
              {
                  dstData[i * scale_step + j] = srcData[pre_i * step + pre_j];
              }
            }
        }
    }
    else if (channels == 3)
    {
        for (i = 0; i < dstHeight; i++)
        {
            for (j = 0; j < dstWidth; j++)
            {
                for (k = 0; k < 3; k++)
                {
                    dstData[(dstHeight - 1 - i) * scale_step + j * 3 + k] = 0;
                }
            }
        }
        for (i = 0; i < dstHeight; i++)
        {
            for (j = 0; j < dstWidth; j++)
            {
                after_i = i;
                after_j = j;
                pre_i = (int)(after_i / dy + 0.5);
                pre_j = (int)(after_j / dx + 0.5);
                if (pre_i >= 0 && pre_i < srcHeight && pre_j >= 0 && pre_j < srcWidth)
                {
                    for (k = 0; k < 3; k++)
                    {
                        dstData[i * scale_step + j * 3 + k] = srcData[pre_i * step + pre_j * 3 + k];
                    }
                }
            }
        }
    }
}

