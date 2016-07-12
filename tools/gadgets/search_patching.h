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

#ifndef STOKE_TOOLS_GADGETS_SEARCH_H
#define STOKE_TOOLS_GADGETS_SEARCH_H

#include <random>

#include "src/search/search_patching.h"
#include "src/transform/transform.h"
#include "tools/args/search_patching.inc"

namespace stoke {

class SearchGadgetPatching : public SearchPatching {
public:
  SearchGadgetPatching(Transform* transform, std::default_random_engine::result_type seed) :
    SearchPatching(transform) {
    set_seed(seed);
    set_beta(beta_arg);
  }
};

} // namespace stoke

#endif
