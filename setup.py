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
from setuptools import setup
from setuptools import Extension
import pybind11
import os

bind11_inc = pybind11.get_include()
ethosu_inc = './driver_library/include/'
lib_path = os.getenv("ETHOSU_LIBRARY_PATH", "/usr/lib")

wrapper = Extension('ethosu',
                    include_dirs = [bind11_inc, ethosu_inc],
                    libraries = ['ethosu'],
                    library_dirs = [lib_path],
                    sources = ['python/interpreter_wrapper.cpp'])

setup(name = "ethosu",
    version = "0.1.0",
    description = "Python wrapper for EthosU Linux driver stack",
    author = "Guo Feng",
    author_email = "feng.guo@nxp.com",
    url = "https://www.nxp.com/",
    license="Apache License 2.0",
    keywords=["ethos-u", "tflite", "npu"],
    long_description = ('The Linux driver stack for Arm(R) Ethos(TM)-U provides '
                        'an example of how a rich operating system like Linux can '
                        'dispatch inferences to an Arm Cortex(R)-M subsystem, '
                        'consisting of an Arm Cortex-M of choice and an Arm Ethos-U NPU.'),
    classifiers=[
        "Development Status :: 5 - Production/Stable",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: Apache Software License",
        "Operating System :: POSIX :: Linux",
        "Operating System :: Microsoft :: Windows :: Windows 10",
        "Programming Language :: C",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Topic :: Scientific/Engineering :: Artificial Intelligence",
        "Topic :: Software Development :: Compilers",
    ],
    install_requires=[
        "flatbuffers==1.12.0",
        "pybind11>=2.8.1",
    ],
    ext_modules = [wrapper]
) 
