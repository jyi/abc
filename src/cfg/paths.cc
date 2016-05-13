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


#include "src/cfg/paths.h"

using namespace stoke;
using namespace std;
using namespace x64asm;

//#define DEBUG_PATH_ENUM 1

vector<vector<Cfg::id_type>> CfgPaths::enumerate_paths(const Cfg& cfg, size_t max_len, Cfg::id_type start, Cfg::id_type end, std::vector<Cfg::id_type>* nopass) {

  if (start == (Cfg::id_type)-1)
    start = cfg.get_entry();

  if (end == (Cfg::id_type)-1)
    end = cfg.get_exit();

  vector<vector<Cfg::id_type>> results;

  vector<Cfg::id_type> path_so_far;
  path_so_far.push_back(start);

  std::map<Cfg::id_type, size_t> node_counts;

  if (max_len > 0)
    enumerate_paths_helper(cfg, path_so_far, end, max_len, node_counts, results, nopass);

  // Remove all blocks with zero instructions
  for (auto& path : results) {
    for (auto iter = path.begin(); iter != path.end(); ) {
      if (!cfg.num_instrs(*iter)) {
        iter = path.erase(iter);
      } else {
        ++iter;
      }
    }
  }

  return results;
}

void CfgPaths::enumerate_paths_helper(const Cfg& cfg,
                                      std::vector<Cfg::id_type> path_so_far,
                                      Cfg::id_type end_block,
                                      size_t max_count,
                                      std::map<Cfg::id_type, size_t> counts,
                                      std::vector<std::vector<Cfg::id_type>>& results,
                                      std::vector<Cfg::id_type>* nopass) {

  size_t len = path_so_far.size();
  Cfg::id_type last_block = path_so_far[len - 1];

#ifdef DEBUG_PATH_ENUM
  for (size_t i = 0; i < len; ++i) {
    cout << "  ";
  }
  cout << "Path: ";
  for (size_t i = 0; i < len; ++i) {
    cout << path_so_far[i] << " ";
  }
  cout << endl;
#endif

  // check for end
  if (last_block == end_block && path_so_far.size() > 1) {
#ifdef DEBUG_PATH_ENUM
    for (size_t i = 0; i < len; ++i) {
      cout << "  ";
    }
    cout << "* Adding solution!" << endl;
#endif
    results.push_back(path_so_far);
  }

  if (nopass && path_so_far.size() > 1 && find(nopass->begin(), nopass->end(), last_block) != nopass->end()) {
    return;
  }

  if (last_block == cfg.get_exit())
    return;

  // iterate
  for (auto it = cfg.succ_begin(last_block), ie = cfg.succ_end(last_block); it != ie; ++it) {

    size_t this_count = ++counts[*it];
    if (this_count > max_count) {
      counts[*it]--;
#ifdef DEBUG_PATH_ENUM
      for (size_t i = 0; i < len; ++i) {
        cout << "  ";
      }
      cout << "* Successor dead end: " << *it << endl;
#endif
      continue;
    }

#ifdef DEBUG_PATH_ENUM
    for (size_t i = 0; i < len; ++i) {
      cout << "  ";
    }
    cout << "* Exploring successor: " << *it << endl;
#endif

    path_so_far.push_back(*it);
    enumerate_paths_helper(cfg, path_so_far, end_block, max_count, counts, results, nopass);
    path_so_far.erase(path_so_far.begin() + path_so_far.size()-1);
    counts[*it]--;
  }

}

/** Find the path this testcase takes through the CFG. */
bool CfgPaths::learn_path(CfgPath& path, const Cfg& cfg, const CpuState& tc) {

  auto code = cfg.get_code();
  auto label = code[0].get_operand<x64asm::Label>(0);
  sandbox_->clear_callbacks();
  sandbox_->clear_inputs();
  sandbox_->insert_input(tc);
  sandbox_->insert_function(cfg);
  sandbox_->set_entrypoint(label);

  /** Insert code either before or after the first instruction in a block to
   * record the path took */
  vector<pair<CfgPaths*, Cfg::id_type>*> to_delete;
  for (size_t i = 0; i < code.size(); ++i) {
    // figure out if we're at the beginning of a block
    auto loc = cfg.get_loc(i);
    auto steps = loc.second;
    if (steps > 0)
      continue;

    // build a pair with a pointer to our object and the basic block of this
    // instruction
    auto pair = new std::pair<CfgPaths*, Cfg::id_type>(this, loc.first);
    to_delete.push_back(pair);

    // insert callback after labels (so jumps don't skip them), but before
    // returns and everything else (so if segfault or exit we still get
    // called).
    auto instr = code[i];
    if (instr.is_label_defn()) {
      sandbox_->insert_after(label, i, learn_path_callback, pair);
    } else {
      sandbox_->insert_before(label, i, learn_path_callback, pair);
    }
  }

  // Now learn the path!
  current_path_ = &path;
  sandbox_->run();

  for (auto it : to_delete)
    delete it;

  auto err = sandbox_->get_output(0)->code;

  return (err == ErrorCode::NORMAL);

}

/** Returns true if first path is a prefix of the second. */
bool CfgPaths::is_prefix(const CfgPath& p, const CfgPath& q) {
  if (q.size() < p.size())
    return false;

  for (size_t i = 0; i < p.size(); ++i) {
    if (p[i] != q[i])
      return false;
  }
  return true;
}

/** Callback used for learning a path. */
void CfgPaths::learn_path_callback(const StateCallbackData&, void* arg) {

  auto pair = (std::pair<CfgPaths*, Cfg::id_type>*)arg;

  auto bb = pair->second;
  pair->first->current_path_->push_back(bb);


}

namespace std {

ostream& operator<<(ostream& os, const stoke::CfgPath& path) {
  for (size_t i = 0; i < path.size(); ++i) {
    os << path[i];
    if (i < path.size() - 1)
      os << " ";
  }
  return os;
}

}
