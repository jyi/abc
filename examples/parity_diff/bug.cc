// Copyright 2013-2016 Stanford University
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cstdlib>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

using namespace std;

size_t parity(uint64_t x) {
    size_t total = 0;
    total += ((x & 0x10) == 0x10);
    return (total % 2);
}

int main(int argc, char** argv) {
    const auto itr = strtoull(argv[1], NULL, 10);

    auto ret = 0;
    for (auto i = 0; i < itr; ++i) {
        ret += parity(i);
    }
    return ret;
}
