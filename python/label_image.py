#
# Copyright 2020-2022 NXP
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the License); you may
# not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an AS IS BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
import ethosu.interpreter as ethosu
import numpy as np
from PIL import Image
import argparse


def load_labels(filename):
  with open(filename, 'r') as f:
    return [line.strip() for line in f.readlines()]

parser = argparse.ArgumentParser()
parser.add_argument(
      '-i',
      '--image',
      default='grace_hopper.bmp',
      help='image to be classified')
parser.add_argument(
      '-m',
      '--model_file',
      default='mobilenet_v1_0.25_224_quant_vela.tflite',
      help='.tflite model to be executed')
parser.add_argument(
      '-l',
      '--label_file',
      default='labels.txt',
      help='name of file containing labels')
args = parser.parse_args()

interpreter = ethosu.Interpreter(args.model_file)
inputs = interpreter.get_input_details()
print("Input details:", inputs)
outputs = interpreter.get_output_details()
print("Output details:", outputs)

w, h = inputs[0]['shape'][1], inputs[0]['shape'][2]
img = Image.open(args.image).resize((w, h))
data = np.expand_dims(img, axis=0)

interpreter.set_input(0, data)
interpreter.invoke()

output_data = interpreter.get_output(0)
results = np.squeeze(output_data)

top_k = results.argsort()[-5:][::-1]
labels = load_labels(args.label_file)
for i in top_k:
    print('{:08.6f}: {}'.format(float(results[i] / 255.0), labels[i]))
