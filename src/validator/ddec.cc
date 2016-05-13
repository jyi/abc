// Copyright 2013-2016 Stanford University
//
// Licensed under the Apache License, Version 2.0 (the License);
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an AS IS BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/cfg/paths.h"
#include "src/validator/bounded.h"
#include "src/validator/ddec.h"
#include "src/validator/null.h"
#include "src/validator/invariants/conjunction.h"
#include "src/validator/invariants/disjunction.h"
#include "src/validator/invariants/equality.h"
#include "src/validator/invariants/false.h"
#include "src/validator/invariants/flag.h"
#include "src/validator/invariants/implication.h"
#include "src/validator/invariants/inequality.h"
#include "src/validator/invariants/memory_equality.h"
#include "src/validator/invariants/memory_null.h"
#include "src/validator/invariants/mod_2n.h"
#include "src/validator/invariants/nonzero.h"
#include "src/validator/invariants/no_signals.h"
#include "src/validator/invariants/sign.h"
#include "src/validator/invariants/state_equality.h"
#include "src/validator/invariants/top_zero.h"
#include "src/validator/invariants/true.h"

#include <algorithm>
#include <set>

// this is configurable via build system
#ifdef STOKE_DEBUG_DDEC
#define DDEC_DEBUG(X) { X }
#else
#define DDEC_DEBUG(X) {   }
#endif

#define DDEC_TC_DEBUG(X) { }

using namespace std;
using namespace stoke;
using namespace x64asm;

void DdecValidator::print_summary(const vector<ConjunctionInvariant*>& invariants) {
  cout << hex << endl;
  cout << endl << "*********************************************************************";
  cout << endl << "****************************   SUMMARY   ****************************";
  cout << endl << "*********************************************************************";
  cout << endl;

  auto target_cuts = cutpoints_->target_cutpoint_locations();
  auto rewrite_cuts = cutpoints_->rewrite_cutpoint_locations();

  for (size_t i = 0; i < invariants.size(); ++i) {
    cout << "Cutpoint " << i << " at blocks " << target_cuts[i] << " / " << rewrite_cuts[i] << endl;
    auto invs = invariants[i];
    for (size_t j = 0; j < invs->size(); ++j) {
      cout << "    " << *(*invs)[j] << endl;
    }
  }

  cout << dec << endl;
}

Instruction get_last_instr(const Cfg& cfg, Cfg::id_type block) {
  auto start_bs = cfg.num_instrs(block);
  return cfg.get_code()[cfg.get_index(Cfg::loc_type(block, start_bs - 1))];
}

/** Returns an invariant representing the fact that the first state transition in the path is taken. */
Invariant* get_jump_inv(const Cfg& cfg, const CfgPath& p, bool is_rewrite) {
  auto jump_type = ObligationChecker::is_jump(cfg, p, 0);

  if (jump_type == ObligationChecker::JumpType::NONE) {
    return new TrueInvariant();
  }

  auto start_block = p[0];
  auto start_bs = cfg.num_instrs(start_block);
  assert(start_bs > 0);
  auto jump_instr = cfg.get_code()[cfg.get_index(Cfg::loc_type(start_block, start_bs - 1))];

  if (!jump_instr.is_jcc()) {
    return new TrueInvariant();
  }

  bool is_fallthrough = jump_type == ObligationChecker::JumpType::FALL_THROUGH;
  auto jump_inv = new FlagInvariant(jump_instr, is_rewrite, is_fallthrough);
  return jump_inv;
}


// takes conjunction of the form (A1 and A2 ... Ak) and returns one of form
// ((B => A1) and (B => A2) ... (B => Ak))
ConjunctionInvariant* transform_with_assumption(Invariant* assume, ConjunctionInvariant* conjunction) {

  ConjunctionInvariant* output = new ConjunctionInvariant();

  for (size_t i = 0; i < conjunction->size(); ++i) {
    output->add_invariant(new ImplicationInvariant(assume, (*conjunction)[i]));
  }

  delete conjunction;
  return output;
}

/** This combines learning and checking invariants with a CEGAR-style search.
  We learn using what testcases we have.  Then, we check that they actually
  hold using a bounded validation approach.  If we don't, we take the testcases
  produced by the bounded validator and feed it into the next search iteration.*/
vector<ConjunctionInvariant*> DdecValidator::find_invariants(const Cfg& target, const Cfg& rewrite) {

  NoSignalsInvariant* no_sigs = new NoSignalsInvariant();
  MemoryEqualityInvariant* mem_equ = new MemoryEqualityInvariant();
  vector<ConjunctionInvariant*> invariants;

  auto target_cuts = cutpoints_->target_cutpoint_locations();
  auto rewrite_cuts = cutpoints_->rewrite_cutpoint_locations();

  // Learn invariants based on the data we have
  invariants.clear();
  for (size_t i = 0; i < target_cuts.size(); ++i) {

    if (target_cuts[i] == target.get_entry()) {
      // Entry
      assert(rewrite_cuts[i] == rewrite.get_entry());

      auto begin = new ConjunctionInvariant();
      auto inv = new StateEqualityInvariant(target.def_ins());
      begin->add_invariant(inv);
      begin->add_invariant(no_sigs);
      begin->add_invariant(mem_equ);
      invariants.push_back(begin);
    } else if (target_cuts[i] == target.get_exit()) {
      // Exit
      assert(rewrite_cuts[i] == rewrite.get_exit());

      auto end = new ConjunctionInvariant();
      auto inv = new StateEqualityInvariant(target.live_outs());
      end->add_invariant(inv);
      end->add_invariant(no_sigs);

      if (heap_out_ || stack_out_)
        end->add_invariant(mem_equ);

      invariants.push_back(end);
    } else {
      auto inv = learn_disjunction_invariant(target, rewrite, i);
      invariants.push_back(inv);
      DDEC_DEBUG(cout << "[ddec] Learned invariant @ i=" << i << endl;)
      DDEC_DEBUG(cout << *inv << endl;)
    }
  }

  return invariants;

}


void DdecValidator::make_tcs(const Cfg& target, const Cfg& rewrite) {

  if (no_bv_) //if not using the bounded validator for testcases, skip this entirely.
    return;

  auto target_paths = CfgPaths::enumerate_paths(target, bound_);
  auto rewrite_paths = CfgPaths::enumerate_paths(rewrite, bound_);

  Code nop_code;
  nop_code.push_back(x64asm::Instruction(x64asm::NOP));
  Cfg nop_cfg(nop_code);
  vector<size_t> empty_path;
  empty_path.push_back(1);

  FalseInvariant _false;
  TrueInvariant _true;

  for (auto p : target_paths) {
    DDEC_DEBUG(cout << "Trying path " << p << " ; on target" << endl;)
    bool equiv = check(target, nop_cfg, p, empty_path, _true, _false);
    if (!equiv && checker_has_ceg()) {
      sandbox_->insert_input(checker_get_target_ceg());
    }
  }
  for (auto p : rewrite_paths) {
    DDEC_DEBUG(cout << "Trying path " << p << " ; on rewrite" << endl;)
    bool equiv = check(rewrite, nop_cfg, p, empty_path, _true, _false);
    if (!equiv && checker_has_ceg()) {
      sandbox_->insert_input(checker_get_target_ceg());
    }
  }

}


bool DdecValidator::verify(const Cfg& init_target, const Cfg& init_rewrite) {

  init_mm();

  auto target = inline_functions(init_target);
  auto rewrite = inline_functions(init_rewrite);

  DDEC_DEBUG(cout << "INLINED TARGET: " << endl << target.get_code() << endl;)
  DDEC_DEBUG(cout << "INLINED REWRITE: " << endl << rewrite.get_code() << endl;)

  try {

    sanity_checks(target, rewrite);

    make_tcs(target, rewrite);

    DDEC_TC_DEBUG(
      cout << "DDEC sandbox at " << sandbox_ << endl;
    for (size_t i = 0; i < sandbox_->size(); ++i) {
    cout << "DDEC sees this TC: " << endl;
    cout << *sandbox_->get_input(i) << endl;
    }
    )

    // Recompute the cutpoints
    if (cutpoints_)
      delete cutpoints_;
    cutpoints_ = new Cutpoints(target, rewrite, *sandbox_);

    if (cutpoints_->has_error()) {
      DDEC_DEBUG(cout << "Cutpoint system encountered: " << cutpoints_->get_error() << endl;)
      reset_mm();
      return false;
    }

    // Loop over choices of cutpoints
    while (true) {

      auto invariants = find_invariants(target, rewrite);
      DDEC_DEBUG(cout << "Got initial invariants " << invariants.size() << endl;)
      if (!invariants.size()) {
        DDEC_DEBUG(cout << "Could not find cutpoints/invariants" << endl;)
        reset_mm();
        return false;
      }

      map<size_t, vector<size_t>> failed_invariants;
      // Loop over choices of invariants (Houdini loop)
      while (true) {

        failed_invariants.clear();
        bool success = check_proof(target, rewrite, invariants, failed_invariants);
        if (success) {
          reset_mm();
          return true;
        }

        // otherwise, remove invariants that failed to validate and try again...
        // we want to be sure not to change the start/end invariants
        DDEC_DEBUG(cout << "Validation failed; attempting to remove failed invariants" << endl;)
        bool made_a_change = false;
        for (size_t i = 1; i < invariants.size() - 1; ++i) {
          auto to_remove = failed_invariants[i];
          DDEC_DEBUG(cout << "For cutpoint " << i << " there are " << to_remove.size() << " failed invariants." << endl;)
          sort(to_remove.begin(), to_remove.end());
          size_t last = (size_t)-1;
          for (auto it = to_remove.rbegin(); it != to_remove.rend(); ++it) {
            if (last == *it)
              continue;
            last = *it;
            DDEC_DEBUG(cout << "Removing " << *(*invariants[i])[*it] << endl;)
            invariants[i]->remove(*it);
            made_a_change = true;
          }
        }

        if (!made_a_change) {
          DDEC_DEBUG(cout << "Could not remove failed invariants.  Programs not proven equivalent." << endl;)
          // got a fixed point, we can't validate this; try another cutpoint
          break;
        }
      }

      if (cutpoints_->has_more()) {
        cutpoints_->next();
      } else {
        reset_mm();
        return false;
      }
    }

  } catch (validator_error e) {

    has_error_ = true;
    error_ = e.get_message();
    error_file_ = e.get_file();
    error_line_ = e.get_line();
    reset_mm();
    return false;
  }
}

bool DdecValidator::check_proof(const Cfg& target, const Cfg& rewrite, const vector<ConjunctionInvariant*>& invariants, map<size_t, vector<size_t>>& failed_invariants) {

  auto target_cuts = cutpoints_->target_cutpoint_locations();
  auto rewrite_cuts = cutpoints_->rewrite_cutpoint_locations();

  for (size_t i = 0; i < target_cuts.size(); ++i) {
    for (size_t j = 0; j < rewrite_cuts.size(); ++j) {
      // For each pair of cutpoints i, j, we need to do the following three checks:
      // 1. Paths_T(i, j) finite, Paths_R(i,j) finite
      // 2. P \in Paths_T(i, j), Q \in Paths_R(i, j) =>
      //    inv(i) { P ; Q } inv(j)
      // 3. P \in Paths_T(i, j), Q \in Paths_R(i, k) =>
      //    inv(i) { P ; Q } false

      // 1. Paths_T(i, j) finite, Paths_R(i,j) finite
      auto target_paths_ij =
        CfgPaths::enumerate_paths(target, 1, target_cuts[i], target_cuts[j], &target_cuts);
      auto rewrite_paths_ij =
        CfgPaths::enumerate_paths(rewrite, 1, rewrite_cuts[i], rewrite_cuts[j], &rewrite_cuts);

      auto target_paths_ij_more =
        CfgPaths::enumerate_paths(target, 2, target_cuts[i], target_cuts[j], &target_cuts);
      auto rewrite_paths_ij_more =
        CfgPaths::enumerate_paths(rewrite, 2, rewrite_cuts[i], rewrite_cuts[j], &rewrite_cuts);

      DDEC_DEBUG(cout << "i=" << i << ", j=" << j
                 << " " << target_paths_ij.size() << " / " << target_paths_ij_more.size() << endl;)
      if (target_paths_ij.size() != target_paths_ij_more.size()) {
        DDEC_DEBUG(cout << "Infinitely many paths found between target cutpoints " << i << " and " << j << endl;)
        return false;
      }
      DDEC_DEBUG(
        cout << "i=" << i << ", j=" << j
        << " " << rewrite_paths_ij.size() << " / " << rewrite_paths_ij_more.size() << endl;)
      if (rewrite_paths_ij.size() != rewrite_paths_ij_more.size()) {
        DDEC_DEBUG(cout << "Infinitely many paths found between rewrite cutpoints " << i << " and " << j << endl;)
        return false;
      }

      DDEC_DEBUG(cout << "cutpoint blocks: " << target_cuts[i] << "  (and)  " << rewrite_cuts[j] << endl;)

      // 2. P in Paths_T(i, j), Q in Paths_R(i, j) => inv(i) { P; Q } inv(j)
      bool success = true;
      for (auto p : target_paths_ij) {
        auto target_jump_inv = get_jump_inv(target, p, false);
        if (target.num_instrs(target_cuts[i]))
          p.erase(p.begin());

        for (auto q : rewrite_paths_ij) {
          auto rewrite_jump_inv = get_jump_inv(rewrite, q, true);
          if (rewrite.num_instrs(rewrite_cuts[i]))
            q.erase(q.begin());
          auto copy = *static_cast<ConjunctionInvariant*>(invariants[i]);
          copy.add_invariant(target_jump_inv);
          copy.add_invariant(rewrite_jump_inv);

          auto end_inv = static_cast<ConjunctionInvariant*>(invariants[j]);
          /*
          for(size_t k = 0; k < end_inv->size(); ++k) {
            auto my_inv = (*end_inv)[k];
            cout << endl << endl << "WORKING ON " << *my_inv << endl << endl;
            bool ok = bv.verify_pair(target, rewrite, p, q, *invariants[i], *my_inv);
            if(!ok)
              return false;
          }
          */

          for (size_t m = 0; m < end_inv->size(); ++m) {

            DDEC_DEBUG(cout << "Checking " << copy << " { " << p << " ; " << q << " } "
                       << *(*end_inv)[m] << endl;)

            bool equiv = check(target, rewrite, p, q, copy, *(*end_inv)[m]);
            if (!equiv) {
              failed_invariants[j].push_back(m);
              success = false;
            }
          }

        }
      }
      if (!success) {
        DDEC_DEBUG(print_summary(invariants);)
        return false;
      }

      // 3. P \in Paths_T(i, j), Q \in Paths_R(i, k) => inv(i) { P ; Q } false
      for (size_t k = 0; k < rewrite_cuts.size(); ++k) {
        if (j == k)
          continue;

        auto rewrite_paths_ik =
          CfgPaths::enumerate_paths(rewrite, 1, rewrite_cuts[i], rewrite_cuts[k], &rewrite_cuts);

        for (auto p : target_paths_ij) {
          auto target_jump_inv = get_jump_inv(target, p, false);
          if (target.num_instrs(target_cuts[i]))
            p.erase(p.begin());


          for (auto q : rewrite_paths_ik) {
            auto rewrite_jump_inv = get_jump_inv(rewrite, q, true);
            if (rewrite.num_instrs(rewrite_cuts[i]))
              q.erase(q.begin());

            auto copy = *static_cast<ConjunctionInvariant*>(invariants[i]);
            copy.add_invariant(target_jump_inv);
            copy.add_invariant(rewrite_jump_inv);

            DDEC_DEBUG(cout << "Checking for cpt " << i << " -> " << j << " against " << i << " -> " << k << endl;)
            DDEC_DEBUG(cout << "Checking " << copy << " { " << p << " ; " << q << " } false " << endl;)
            FalseInvariant fi;
            bool equiv = check(target, rewrite, p, q, copy, fi);
            if (!equiv) {
              DDEC_DEBUG(print_summary(invariants);)
              return false;
            }
          }
        }
      }
    }
  }


  DDEC_DEBUG(print_summary(invariants);)

  return true;

}





/** Assumption: given a disjunction of conjuncts.
    Returns a conjunction which *may* include disjuncts. */
ConjunctionInvariant* simplify_disjunction(DisjunctionInvariant& disjs) {

  DDEC_DEBUG(cout << "SIMPLIFYING DISJUNCTS" << endl;
             cout << disjs << endl << endl;)

  FalseInvariant _false;

  // Go through disjunctions and throw out any that have a conjunction involving false...
  for (size_t i = 0; i < disjs.size(); ++i) {
    auto& conj = *static_cast<ConjunctionInvariant*>(disjs[i]);
    for (size_t j = 0; j < conj.size(); ++j) {
      if (*conj[j] == _false) {
        DDEC_DEBUG(cout << "Removing disjunct " << i << " due to index " << j << endl;)
        disjs.remove(i);
        i--;
        break;
      }
    }
  }

  DDEC_DEBUG(cout << "Finished removing dumb disjuncts" << endl;)

  // Take the conjunctions in the first disjunct.
  // If they're in the rest, then of the disjuncts, we remove it from all of them
  auto common_conjunctions = new ConjunctionInvariant();

  auto& first_conjunct = *static_cast<ConjunctionInvariant*>(disjs[0]);
  for (size_t i = 0; i < first_conjunct.size(); ++i) {
    auto leaf = first_conjunct[i];
    DDEC_DEBUG(cout << "Looking for " << *leaf << " in all disjuncts" << endl;)

    bool contained_in_all = true;
    for (size_t j = 1; j < disjs.size(); j++) {
      auto& other_conjunct = *static_cast<ConjunctionInvariant*>(disjs[j]);
      bool contained = false;
      for (size_t k = 0; k < other_conjunct.size(); ++k) {
        if (*other_conjunct[k] == *leaf) {
          contained = true;
          break;
        }
      }
      contained_in_all &= contained;
    }

    // remove the leaf from the conjunction
    if (contained_in_all) {
      DDEC_DEBUG(cout << "  found in all :)" << endl;)
      common_conjunctions->add_invariant(leaf);
      for (size_t j = 0; j < disjs.size(); j++) {
        auto& other_conjunct = *static_cast<ConjunctionInvariant*>(disjs[j]);
        bool contained = false;
        for (size_t k = 0; k < other_conjunct.size(); ++k) {
          if (*other_conjunct[k] == *leaf) {
            if (contained && j == 0) {
              //we're in trouble
              DDEC_DEBUG(cout << "OOPS!  Simplified and found same thing twice!  WARNING" << endl;)
            }
            other_conjunct.remove(k);
            contained = true;
            k--;
          }
        }
      }
      i--;
    } else {
      DDEC_DEBUG(cout << "  not found" << endl;)
    }
  }

  common_conjunctions->add_invariant(&disjs);

  DDEC_DEBUG(cout << "ALL DONE W/ SIMPLIFY" << endl;)

  return common_conjunctions;

}

ConjunctionInvariant* DdecValidator::learn_disjunction_invariant(const Cfg& target, const Cfg& rewrite, size_t cutpoint) {

  /** Lets get out the relevant data here... */
  vector<CpuState> target_states = cutpoints_->data_at(cutpoint, false);
  vector<CpuState> rewrite_states = cutpoints_->data_at(cutpoint, true);

  DDEC_DEBUG(cout << "[ddec] learning cutpoint " << cutpoint << " invariant over " << target_states.size() << " target states, " << rewrite_states.size() << " rewrite states." << endl;)

  auto target_cuts = cutpoints_->target_cutpoint_locations();
  auto rewrite_cuts = cutpoints_->rewrite_cutpoint_locations();

  auto target_regs = target.def_outs(target_cuts[cutpoint]);
  auto rewrite_regs = rewrite.def_outs(rewrite_cuts[cutpoint]);

  auto last_target_instr = get_last_instr(target, target_cuts[cutpoint]);
  auto last_rewrite_instr = get_last_instr(rewrite, rewrite_cuts[cutpoint]);

  bool target_has_jcc = last_target_instr.is_jcc();
  string target_opcode = Handler::get_opcode(last_target_instr);
  string target_cc = target_opcode.substr(1, target_opcode.size() - 1);

  bool rewrite_has_jcc = last_rewrite_instr.is_jcc();
  string rewrite_opcode = Handler::get_opcode(last_rewrite_instr);
  string rewrite_cc = rewrite_opcode.substr(1, rewrite_opcode.size() - 1);

  /** Case 1: there's no conditional jump */
  if (!target_has_jcc && !rewrite_has_jcc) {
    return learn_simple_invariant(target, rewrite, target_regs, rewrite_regs, target_states, rewrite_states);
  } else if (target_has_jcc && !rewrite_has_jcc) {

    vector<CpuState> target_jump_states;
    vector<CpuState> target_fall_states;
    vector<CpuState> rewrite_jump_states;
    vector<CpuState> rewrite_fall_states;

    for (size_t i = 0; i < target_states.size(); ++i) {
      if (ConditionalHandler::condition_satisfied(target_cc, target_states[i])) {
        target_jump_states.push_back(target_states[i]);
        rewrite_jump_states.push_back(rewrite_states[i]);
      } else {
        target_fall_states.push_back(target_states[i]);
        rewrite_fall_states.push_back(rewrite_states[i]);
      }
    }

    auto jump_inv = new FlagInvariant(last_target_instr, false, false);
    auto jump_simple = learn_simple_invariant(target, rewrite, target_regs, rewrite_regs, target_jump_states, rewrite_jump_states);
    jump_simple = transform_with_assumption(jump_inv, jump_simple);

    auto fall_inv = new FlagInvariant(last_target_instr, false, true);
    auto fall_simple = learn_simple_invariant(target, rewrite, target_regs, rewrite_regs, target_fall_states, rewrite_fall_states);
    fall_simple = transform_with_assumption(fall_inv, fall_simple);

    fall_simple->add_invariants(jump_simple);

    return fall_simple;

  } else if (!target_has_jcc && rewrite_has_jcc) {

    vector<CpuState> target_jump_states;
    vector<CpuState> target_fall_states;

    vector<CpuState> rewrite_jump_states;
    vector<CpuState> rewrite_fall_states;

    for (size_t i = 0; i < rewrite_states.size(); ++i) {
      if (ConditionalHandler::condition_satisfied(rewrite_cc, rewrite_states[i])) {
        target_jump_states.push_back(target_states[i]);
        rewrite_jump_states.push_back(rewrite_states[i]);
      } else {
        target_fall_states.push_back(target_states[i]);
        rewrite_fall_states.push_back(rewrite_states[i]);
      }
    }

    auto jump_inv = new FlagInvariant(last_rewrite_instr, true, false);
    auto jump_simple = learn_simple_invariant(target, rewrite, target_regs, rewrite_regs, target_jump_states, rewrite_jump_states);
    jump_simple = transform_with_assumption(jump_inv, jump_simple);

    auto fall_inv = new FlagInvariant(last_rewrite_instr, true, true);
    auto fall_simple = learn_simple_invariant(target, rewrite, target_regs, rewrite_regs, target_fall_states, rewrite_fall_states);
    fall_simple = transform_with_assumption(fall_inv, fall_simple);

    fall_simple->add_invariants(jump_simple);

    return fall_simple;
  } else {
    // Both have jumps!
    vector<CpuState> jump_jump_states_target;
    vector<CpuState> jump_fall_states_target;
    vector<CpuState> fall_jump_states_target;
    vector<CpuState> fall_fall_states_target;

    vector<CpuState> jump_jump_states_rewrite;
    vector<CpuState> jump_fall_states_rewrite;
    vector<CpuState> fall_jump_states_rewrite;
    vector<CpuState> fall_fall_states_rewrite;

    for (size_t i = 0; i < target_states.size(); ++i) {
      if ( ConditionalHandler::condition_satisfied( target_cc, target_states[i])) {
        if ( ConditionalHandler::condition_satisfied( rewrite_cc, rewrite_states[i])) {
          jump_jump_states_target.push_back(target_states[i]);
          jump_jump_states_rewrite.push_back(rewrite_states[i]);
        } else {
          jump_fall_states_target.push_back(target_states[i]);
          jump_fall_states_rewrite.push_back(rewrite_states[i]);
        }
      } else {
        if ( ConditionalHandler::condition_satisfied( rewrite_cc, rewrite_states[i])) {
          fall_jump_states_target.push_back(target_states[i]);
          fall_jump_states_rewrite.push_back(rewrite_states[i]);
        } else {
          fall_fall_states_target.push_back(target_states[i]);
          fall_fall_states_rewrite.push_back(rewrite_states[i]);
        }
      }
    }

    auto S1 = learn_simple_invariant(target, rewrite, target_regs, rewrite_regs, jump_jump_states_target, jump_jump_states_rewrite);
    auto S1_target_path = new FlagInvariant(last_target_instr, false, false);
    auto S1_rewrite_path = new FlagInvariant(last_rewrite_instr, true, false);
    S1 = transform_with_assumption(S1_target_path->AND(S1_rewrite_path), S1);

    auto S2 = learn_simple_invariant(target, rewrite, target_regs, rewrite_regs, jump_fall_states_target, jump_fall_states_rewrite);
    auto S2_target_path = new FlagInvariant(last_target_instr, false, false);
    auto S2_rewrite_path = new FlagInvariant(last_rewrite_instr, true, true);
    S2 = transform_with_assumption(S2_target_path->AND(S2_rewrite_path), S2);

    auto S3 = learn_simple_invariant(target, rewrite, target_regs, rewrite_regs, fall_jump_states_target, fall_jump_states_rewrite);
    auto S3_target_path = new FlagInvariant(last_target_instr, false, true);
    auto S3_rewrite_path = new FlagInvariant(last_rewrite_instr, true, false);
    S3 = transform_with_assumption(S3_target_path->AND(S3_rewrite_path), S3);

    auto S4 = learn_simple_invariant(target, rewrite, target_regs, rewrite_regs, fall_fall_states_target, fall_fall_states_rewrite);
    auto S4_target_path = new FlagInvariant(last_target_instr, false, true);
    auto S4_rewrite_path = new FlagInvariant(last_rewrite_instr, true, true);
    S4 = transform_with_assumption(S4_target_path->AND(S4_rewrite_path), S4);

    S1->add_invariants(S2);
    S1->add_invariants(S3);
    S1->add_invariants(S4);

    return S1;
  }

  assert(false);
  return NULL;

}

/** Return a set of possible memory null invariants */
vector<MemoryNullInvariant*> build_memory_null_invariants(RegSet target_regs, RegSet rewrite_regs, const Cfg& target, const Cfg& rewrite) {
  vector<MemoryNullInvariant*> invariants;

  for (size_t k = 0; k < 2; ++k) {
    bool is_rewrite = k;
    const Cfg& cfg = is_rewrite ? rewrite : target;
    auto code = cfg.get_code();
    auto regs = is_rewrite ? rewrite_regs : target_regs;

    set<x64asm::Mem> memory_operands;
    for (auto instr : code) {
      if (instr.is_explicit_memory_dereference()) {
        auto mem = instr.get_operand<x64asm::Mem>((size_t)instr.mem_index());
        //cout << "Considering operand " << mem << endl;
        memory_operands.insert(mem);
      }
    }

    for (auto it : memory_operands) {
      //cout << "And now it is: " << it << endl;
      if (it.contains_seg())
        continue;
      if (it.contains_base() && !regs.contains(it.get_base()))
        continue;
      if (it.contains_index() && !regs.contains(it.get_index()))
        continue;

      auto mni = new MemoryNullInvariant(it, is_rewrite, true);
      invariants.push_back(mni);

      /*
      cout << "Ok, made a " << *mni << endl;

      mni = new MemoryNullInvariant(it, is_rewrite, false);
      invariants.push_back(mni);
      */
    }


  }

  /*
  for (auto it : invariants) {
    cout << *it << endl;
  }
  */

  return invariants;
}

/** Return a set of possible inequality invariants. */
vector<InequalityInvariant*> build_inequality_invariants(RegSet target_regs, RegSet rewrite_regs) {

  vector<InequalityInvariant*> inequalities;

  // For now, let's look at unsigned target-target and rewrite-rewrite inequalities

  for (size_t k = 0; k < 2; ++k) {
    auto regs = k ? rewrite_regs : target_regs;

    for (auto i = regs.gp_begin(); i != regs.gp_end(); ++i) {
      for (auto j = regs.gp_begin(); j != regs.gp_end(); ++j) {
        if (*i == *j)
          continue;
        if ((*i).size() != (*j).size())
          continue;

        if ((*i).size() == 32) {
          inequalities.push_back(new InequalityInvariant(*i, *j, k, k, false, false));
          inequalities.push_back(new InequalityInvariant(*i, *j, k, k, true, false));
        } else if ((*i).size() == 64) {
          inequalities.push_back(new InequalityInvariant(*i, *j, k, k, false, false));
          inequalities.push_back(new InequalityInvariant(*i, *j, k, k, true, false));

          inequalities.push_back(new InequalityInvariant(r32s[*i], r32s[*j], k, k, false, false));
          inequalities.push_back(new InequalityInvariant(r32s[*i], r32s[*j], k, k, true, false));
        }
      }
    }
  }

  return inequalities;
}

/** Return a set of possible lower-n bit invariants. */
vector<Mod2NInvariant*> build_mod2n_invariants(RegSet target_regs, RegSet rewrite_regs) {

  vector<Mod2NInvariant*> invariants;

  for (size_t k = 0; k < 2; ++k) {
    auto regs = k ? rewrite_regs : target_regs;

    for (auto i = regs.gp_begin(); i != regs.gp_end(); ++i)
      for (auto j = 1; j < 5; ++j)
        invariants.push_back(new Mod2NInvariant(*i, k, j));
  }

  return invariants;
}

/** Return a set of sign invariants. */
vector<SignInvariant*> build_sign_invariants(RegSet target_regs, RegSet rewrite_regs) {

  vector<SignInvariant*> invariants;

  for (size_t k = 0; k < 2; ++k) {
    auto regs = k ? rewrite_regs : target_regs;

    for (auto i = regs.gp_begin(); i != regs.gp_end(); ++i) {
      invariants.push_back(new SignInvariant(*i, k, true));
      invariants.push_back(new SignInvariant(*i, k, false));
    }
  }

  return invariants;
}

ConjunctionInvariant* DdecValidator::learn_simple_invariant(const Cfg& target, const Cfg& rewrite, x64asm::RegSet target_regs, x64asm::RegSet rewrite_regs, const vector<CpuState>& target_states, const vector<CpuState>& rewrite_states) {

  assert(target_states.size() == rewrite_states.size());

  //TODO leaks memory

  MemoryEqualityInvariant* mem_equ = new MemoryEqualityInvariant();

  NoSignalsInvariant* no_sigs = new NoSignalsInvariant();
  ConjunctionInvariant* conj = new ConjunctionInvariant();
  conj->add_invariant(no_sigs);
  conj->add_invariant(mem_equ);

  if (target_states.size() == 0 || rewrite_states.size() == 0) {
    conj->add_invariant(new FalseInvariant());
    return conj;
  }

  RegSet r64_exclude = RegSet::empty();

  // TopZero and NonZero invariants
  for (size_t k = 0; k < 2; ++k) {
    auto& states = k ? rewrite_states : target_states;
    auto& regs = k ? rewrite_regs : target_regs;

    for (auto it = regs.gp_begin(); it != regs.gp_end(); ++it) {
      bool all_topzero = true;
      bool all_nonzero = true;

      if ((*it).size() == 64) {
        for (auto state : states) {
          if (state.gp[*it].get_fixed_double(1) != 0) {
            all_topzero = false;
          }
        }
      } else {
        all_topzero = false;
      }

      for (auto state : states) {
        if (state.gp[*it].get_fixed_quad(0) == 0) {
          all_nonzero = false;
        }
      }

      if (all_topzero) {
        auto tzi = new TopZeroInvariant(r64s[*it], k);
        if (tzi->check(target_states, rewrite_states)) {
          conj->add_invariant(tzi);
          r64_exclude = r64_exclude + r64s[*it];
        } else {
          DDEC_DEBUG(cout << "GOT BAD INVARIANT " << *tzi << endl;)
          delete tzi;
        }
      }

      if (all_nonzero) {
        auto nz = new NonzeroInvariant(r64s[*it], k);
        if (nz->check(target_states, rewrite_states)) {
          conj->add_invariant(nz);
        } else {
          DDEC_DEBUG(cout << "GOT BAD INVARIANT " << *nz << endl;)
          delete nz;
        }
      }
    }
  }

  // mod2^n invariants
  auto potential_mod2n = build_mod2n_invariants(target_regs, rewrite_regs);
  for (auto modulo : potential_mod2n) {
    if (modulo->check(target_states, rewrite_states)) {
      conj->add_invariant(modulo);
    } else {
      delete modulo;
    }
  }

  // sign invariants
  auto potential_sign = build_sign_invariants(target_regs, rewrite_regs);
  for (auto sign : potential_sign) {
    if (sign->check(target_states, rewrite_states)) {
      conj->add_invariant(sign);
    } else {
      delete sign;
    }
  }

  // Inequality invariants
  auto potential_inequalities = build_inequality_invariants(target_regs, rewrite_regs);
  for (auto ineq : potential_inequalities) {
    if (ineq->check(target_states, rewrite_states)) {
      conj->add_invariant(ineq);
    } else {
      delete ineq;
    }
  }

  auto potential_memory_nulls = build_memory_null_invariants(target_regs, rewrite_regs, target, rewrite);
  for (auto mem_null : potential_memory_nulls) {
    //cout << "Testing " << *mem_null << endl;
    if (mem_null->check(target_states, rewrite_states)) {
      //cout << " * pass" << endl;
      conj->add_invariant(mem_null);
    } else {
      //cout << " * fail" << endl;
      delete mem_null;
    }
  }


  // Define columns that will be used to learn equalities
  vector<EqualityInvariant::Term> columns;

  DDEC_DEBUG(cout << "try sign extend: " << try_sign_extend_ << endl;)

  for (size_t k = 0; k < 2; ++k) {
    auto def_ins = k ? rewrite_regs : target_regs;
    for (auto r = def_ins.gp_begin(); r != def_ins.gp_end(); ++r) {
      EqualityInvariant::Term c;
      c.reg = *r;
      c.is_rewrite = k;
      c.sign_extend = false;
      c.index = 0;
      columns.push_back(c);
    }
    for (auto r = def_ins.sse_begin(); r != def_ins.sse_end(); ++r) {
      for (size_t i = 0; i < (*r).size()/64; ++i) {
        EqualityInvariant::Term c;
        c.reg = *r;
        c.is_rewrite = k;
        c.sign_extend = false;
        c.index = i;
        columns.push_back(c);
      }
    }
  }

  DDEC_DEBUG(
  for (auto it : columns) {
  cout << "Column reg " << it.reg << " rewrite? " << it.is_rewrite << " zx? " << it.sign_extend << " index? " << it.index << endl;
});

  size_t num_columns = columns.size() + 1;
  size_t tc_count = target_states.size();

  // Find some of the simple equalities by brute force
  DDEC_DEBUG(cout << "looking for simple equalities" << endl;)

  for (size_t i = 0; i < columns.size(); ++i) {
    for (size_t j = i+1; j < columns.size(); ++j) {
      // check if column i matches column j
      bool match = true;
      for (size_t k = 0; k < tc_count; ++k) {
        if (columns[i].from_state(target_states[k], rewrite_states[k]) !=
            columns[j].from_state(target_states[k], rewrite_states[k])) {
          match = false;
          break;
        }
      }
      // add equality asserting column[i] matches column[j].
      if (match) {
        vector<EqualityInvariant::Term> terms;
        columns[i].coefficient = 1;
        columns[j].coefficient = -1;
        terms.push_back(columns[i]);
        terms.push_back(columns[j]);

        auto ei = new EqualityInvariant(terms, 0);
        conj->add_invariant(ei);
        DDEC_DEBUG(cout << "generating " << *ei << endl;)
      }
    }
  }

  // Build the nullspace matrix
  DDEC_DEBUG(cout << dec << "allocating the matrix of size " << tc_count << " x " << num_columns << hex << endl;)
  uint64_t* matrix = new uint64_t[tc_count*num_columns];

  for (size_t i = 0; i < tc_count; ++i) {
    for (size_t j = 0; j < columns.size(); ++j) {
      matrix[i*num_columns + j] = columns[j].from_state(target_states[i], rewrite_states[i]);
    }
    matrix[i*num_columns + num_columns - 1] = 1;
  }

  DDEC_DEBUG(
  for (size_t i = 0; i < tc_count; ++i) {
  for (size_t j = 0; j < num_columns; ++j) {
      cout << hex << matrix[i*num_columns + j] << dec << " ";
    }
    cout << endl;
  }
  );

  // Compute the nullspace
  uint64_t** nullspace_out;
  size_t dim;

  dim = Nullspace::bv_nullspace(matrix, tc_count, num_columns, &nullspace_out);
  delete matrix;

  // Extract the data from the nullspace
  for (size_t i = 0; i < dim; ++i) {
    vector<EqualityInvariant::Term> terms;

    for (size_t j = 0; j < num_columns - 1; ++j) {
      auto column = columns[j];

      if (nullspace_out[i][j]) {
        column.coefficient = nullspace_out[i][j];
        terms.push_back(column);
      }
    }

    auto ei = new EqualityInvariant(terms, -nullspace_out[i][num_columns-1]);
    if (ei->check(target_states, rewrite_states)) {
      conj->add_invariant(ei);
      DDEC_DEBUG(cout << *ei << endl;)
    } else {
      DDEC_DEBUG(cout << "GOT BAD INVARIANT ? " << *ei << endl;)
    }
  }

  for (size_t i = 0; i < dim; ++i)
    delete nullspace_out[i];
  delete nullspace_out;

  DDEC_DEBUG(cout << "Nullspace dimension:" << dec << dim << endl;)
  DDEC_DEBUG(cout << "Column count: " << dec << num_columns << endl;)

  return conj;
}
