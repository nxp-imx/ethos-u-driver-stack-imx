/*
 * Copyright (c) 2021 Arm Limited. All rights reserved.
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

#include "dev_mem.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <dirent.h>

namespace {

void help(const char *prog) {
    std::cerr << "USAGE: " << prog << " [-h] [--address ADDRESS] [-c] [-C]" << std::endl << std::endl;
    std::cerr << "positional argument:" << std::endl;
    std::cerr << "  -h, --help            Show help message and exit" << std::endl;
    std::cerr << "  --address ADDRESS     Address of ring buffer" << std::endl;
    std::cerr << "  -C                    Clear the ring buffer" << std::endl;
    std::cerr << "  -c                    Read and clear the ring buffer" << std::endl;
}

class Path {
public:
    Path(const std::string path) : path(path) {}

    Path(const char *path) : path(path) {}

    std::string string() const {
        return path;
    }

    const char *c_str() const {
        return path.c_str();
    }

    Path operator/(const std::string &other) const {
        if (other.substr(0, 1) == "/") {
            return Path(other);
        } else {
            return Path(path + "/" + other);
        }
    }

    bool exists() const {
        std::ifstream f = std::ifstream(path);
        return !!f;
    }

    std::vector<Path> find(const std::string &name) const {
        std::vector<Path> files;
        find(*this, name, files);
        return files;
    }

    Path parent() const {
        return Path(path.substr(0, path.rfind("/")));
    }

private:
    const std::string path;

    static void find(const Path &path, const std::string &name, std::vector<Path> &files) {
        DIR *dir = opendir(path.c_str());
        if (dir == nullptr) {
            throw EthosU::DevMem::Exception(std::string("Failed to open ") + path.string());
        }

        const dirent *dentry;
        while ((dentry = readdir(dir)) != nullptr) {
            const std::string dname = dentry->d_name;
            const Path pathname     = path / dname;

            // Add path to list if it matches 'name'
            if (dname == name) {
                files.push_back(pathname);
            }

            // Call 'find' recursively for directories
            if (dname != "." && dname != ".." && dentry->d_type == DT_DIR) {
                find(pathname, name, files);
            }
        }

        closedir(dir);
    }
};

bool grep(const Path &path, const std::string &match) {
    std::ifstream ifs(path.c_str(), std::ios_base::binary);
    std::string content = std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    return content.find(match) != std::string::npos;
}

class DTS {
public:
    DTS(const Path &path) : path(path) {}

    void getRegByName(const std::string &name, uintptr_t &address, size_t &size) const {
        size_t index = 0;
        for (const auto &n : getString("reg-names")) {
            if (n == name) {
                getRegByIndex(index, address, size);
                return;
            }

            index++;
        }

        throw EthosU::DevMem::Exception(std::string("Failed to find 'reg-name' ") + name);
    }

    void getRegByIndex(const size_t index, uintptr_t &address, size_t &size) const {
        size_t addressCell = getAddressCells();
        size_t sizeCell    = getSizeCells();
        size_t offset      = index * (addressCell + sizeCell) * 4;

        address = getInt("reg", offset, addressCell * 4);
        size    = getInt("reg", offset + addressCell * 4, sizeCell * 4);
    }

private:
    const Path path;

    size_t getAddressCells() const {
        const Path p = path / "#address-cells";
        if (!p.exists()) {
            return 2;
        }

        return getInt(p.string(), 0, 4);
    }

    size_t getSizeCells() const {
        const Path p = path / "#size-cells";
        if (!p.exists()) {
            return 2;
        }

        return getInt(p.string(), 0, 4);
    }

    std::vector<char> getProperty(const std::string &name) const {
        const Path propertyPath = path / name;
        std::ifstream ifs(propertyPath.c_str(), std::ios_base::binary);
        std::vector<char> content =
            std::vector<char>(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
        return content;
    }

    std::vector<std::string> getString(const std::string &name) const {
        std::vector<char> property = getProperty(name);
        std::vector<std::string> names;

        for (std::vector<char>::iterator end, it = property.begin();
             (end = std::find(it, property.end(), '\0')) != property.end();
             it = end + 1) {
            names.push_back(std::string(it, end));
        }

        return names;
    }

    uint64_t getInt(const std::string &name, const size_t offset, const size_t size) const {
        const std::vector<char> property = getProperty(name);

        switch (size) {
        case 1:
            return toCpu<uint8_t>(&property[offset]);
        case 2:
            return toCpu<uint16_t>(&property[offset]);
        case 4:
            return toCpu<uint32_t>(&property[offset]);
        case 8:
            return toCpu<uint64_t>(&property[offset]);
        default:
            throw EthosU::DevMem::Exception("Illegal integer size");
        }
    }

    static constexpr bool isBigEndian() {
        int d = 0x12345678;
        return (d & 0xf) == 1;
    }

    template <typename T>
    static T toCpu(const char *src) {
        T dst;
        char *d = reinterpret_cast<char *>(&dst);

        if (isBigEndian()) {
            for (size_t i = 0; i < sizeof(T); i++) {
                d[i] = src[i];
            }
        } else {
            for (size_t i = 0; i < sizeof(T); i++) {
                d[i] = src[sizeof(T) - 1 - i];
            }
        }

        return dst;
    }
};

void getAddressSizeFromDtb(uintptr_t &address, size_t &size) {
    // This is the file system path to the device tree
    const Path devtree = "/sys/firmware/devicetree/base";

    // Search device tree for <path>/**/compatible
    for (const auto &path : devtree.find("compatible")) {
        // Grep for "ethosu" in 'compatible'
        if (grep(path, "arm,ethosu")) {
            DTS dts(path.parent());

            dts.getRegByName("print_queue", address, size);
            return;
        }
    }

    throw EthosU::DevMem::Exception("Could not find Ethos-U device tree entry with reg-name 'print_queue'");
}

} // namespace

int main(int argc, char *argv[]) {
    try {
        uintptr_t address = 0;
        size_t size       = 0;
        bool clearBefore  = false;
        bool clearAfter   = false;

        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];

            if (arg == "--address") {
                address = std::stol(argv[++i], nullptr, 0);
            } else if (arg == "-c") {
                clearAfter = true;
            } else if (arg == "-C") {
                clearBefore = true;
            } else if (arg == "-h" || arg == "--help") {
                help(argv[0]);
                ::exit(0);
            } else {
                std::cerr << "Illegal argument '" + arg + "'" << std::endl;
                help(argv[0]);
                ::exit(1);
            }
        }

        if (address == 0) {
            getAddressSizeFromDtb(address, size);
        }

        EthosU::DevMem::Log log(address, size);

        if (clearBefore) {
            log.clear();
        }

        log.print();

        if (clearAfter) {
            log.clear();
        }
    } catch (std::exception &e) {
        printf("Error: %s\n", e.what());
        return 1;
    }

    return 0;
}
