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

#include <chrono>
#include <iostream>
#include <sys/time.h>

#include "src/ext/cpputil/include/command_line/command_line.h"
#include "src/ext/cpputil/include/io/column.h"
#include "src/ext/cpputil/include/io/console.h"
#include "src/ext/cpputil/include/io/filterstream.h"
#include "src/ext/cpputil/include/signal/debug_handler.h"

#include "src/cfg/cfg_transforms.h"
#include "src/expr/expr.h"
#include "src/expr/expr_parser.h"
#include "src/tunit/tunit.h"
#include "src/search/progress_callback.h"
#include "src/search/statistics_callback.h"
#include "src/search/failed_verification_action.h"
#include "src/search/postprocessing.h"

#include "src/cfg/cfg.h"
#include "src/cfg/paths.h"
#include "src/symstate/memory/trivial.h"
#include "src/symstate/memory/flat.h"
#include "src/validator/bounded.h"
#include "src/validator/invariants/conjunction.h"
#include "src/validator/invariants/memory_equality.h"
#include "src/validator/invariants/state_equality.h"
#include "src/validator/invariants/true.h"
#include "src/validator/filters/default.h"
#include "src/ext/x64asm/src/code.h"
#include "src/disassembler/disassembler.h"

#include "tools/args/search.inc"
#include "tools/args/target.inc"
#include "tools/gadgets/cost_function.h"
#include "tools/gadgets/correctness_cost.h"
#include "tools/gadgets/functions.h"
#include "tools/gadgets/sandbox.h"
#include "tools/gadgets/search.h"
#include "tools/gadgets/search_state.h"
#include "tools/gadgets/seed.h"
#include "tools/gadgets/solver.h"
#include "tools/gadgets/target.h"
#include "tools/gadgets/testcases.h"
#include "tools/gadgets/transform_pools.h"
#include "tools/gadgets/validator.h"
#include "tools/gadgets/verifier.h"
#include "tools/gadgets/weighted_transform.h"
#include "tools/gadgets/rewrite.h"
#include "tools/io/postprocessing.h"
#include "tools/io/failed_verification_action.h"

using namespace cpputil;
using namespace std;
using namespace stoke;
using namespace chrono;
using namespace x64asm;

#define CONSTRAINT_DEBUG(X) {X}

enum JumpType {
  NONE, // jump target is the fallthrough
  FALL_THROUGH,
  JUMP
};

struct LineInfo {
  size_t line_number;
  x64asm::Label label;
  uint64_t rip_offset;
};

typedef std::map<size_t,LineInfo> LineMap;

auto& out = ValueArg<string>::create("out")
            .alternate("o")
            .usage("<path/to/file.smt2>")
            .description("File to write successful results to")
            .default_val("result.smt2");

Cfg rewrite_cfg_with_path(const Cfg& cfg, const CfgPath& p, map<size_t,LineInfo>& to_populate) {
  Code code;
  auto function = cfg.get_function();

  for (auto node : p) {
    if (cfg.num_instrs(node) == 0)
      continue;

    size_t start_index = cfg.get_index(std::pair<Cfg::id_type, size_t>(node, 0));
    size_t end_index = start_index + cfg.num_instrs(node);
    for (size_t i = start_index; i < end_index; ++i) {

      LineInfo li;
      li.label = function.get_leading_label();
      li.line_number = i;
      li.rip_offset = function.hex_offset(i) + function.get_rip_offset() + function.hex_size(i);
      to_populate[code.size()] = li;

      if (cfg.get_code()[i].is_jump()) {
        code.push_back(Instruction(NOP));
      } else {
        code.push_back(cfg.get_code()[i]);
      }
    }
  }

  TUnit new_fxn(code, 0, function.get_rip_offset(), 0);
  Cfg new_cfg(new_fxn, cfg.def_ins(), cfg.live_outs());

  //cout << "path cfg for " << print(p) << " is " << endl;
  //cout << TUnit(code) << endl;

  return new_cfg;
}

void build_circuit(const Cfg& cfg, Cfg::id_type bb, JumpType jump, SymState& state, size_t& line_no, const LineMap& line_map) {

  Handler& handler(*(new ComboHandler()));
  Filter* filter = new DefaultFilter(handler);
  bool nacl_ = true;

  if (cfg.num_instrs(bb) == 0)
    return;

  size_t start_index = cfg.get_index(std::pair<Cfg::id_type, size_t>(bb, 0));
  size_t end_index = start_index + cfg.num_instrs(bb);

  for (size_t i = start_index; i < end_index; ++i) {
    auto li = line_map.at(line_no);
    line_no++;
    auto instr = cfg.get_code()[i];

    if (instr.is_jcc()) {
      // get the name of the condition
      string name = opcode_write_att(instr.get_opcode());
      string condition = name.substr(1);
      auto constraint = ConditionalHandler::condition_predicate(condition, state);
      switch (jump) {
      case JumpType::JUMP:
        //cout << "Assuming jump for " << instr << endl;
        state.constraints.push_back(constraint);
        break;
      case JumpType::FALL_THROUGH:
        //cout << "Assuming fall-through for " << instr << endl;
        constraint = !constraint;
        state.constraints.push_back(constraint);
        break;
      case JumpType::NONE:
        break;
      default:
        assert(false);
      }

    } else if (instr.is_label_defn() || instr.is_nop() || instr.is_any_jump()) {
      // cout << "[DEBUG]: build_circuit_is_lbl: " << i << "] " << instr << "\n";
      continue;
    } else if (instr.is_ret()) {
      // cout << "[DEBUG]: build_circuit_is_ret: " << i << "] " << instr << "\n";
      return;
    } else {
      // Build the handler for the instruction
      state.set_lineno(line_no-1);
      state.rip = SymBitVector::constant(64, li.rip_offset);

      if (nacl_) {
        // We need to add constraints keeping the index register (if present)
        // away from the edges of the ddress space.
        if (instr.is_explicit_memory_dereference()) {
          auto mem = instr.get_operand<M8>(instr.mem_index());
          if (mem.contains_index()) {
            R64 index = mem.get_index();
            auto address = state[index];
            // cout << "[DEBUG]: build_circuit_is_nacl: " << i << "] " << instr << " | " << (address >= SymBitVector::constant(64, 0x10)) << "\n";
            // cout << "[DEBUG]: build_circuit_is_gen: " << i << "] " << instr << " | " << (address <= SymBitVector::constant(64, 0xfffffff0)) << "\n";
            state.constraints.push_back(address >= SymBitVector::constant(64, 0x10));
            state.constraints.push_back(address <= SymBitVector::constant(64, 0xfffffff0));
          }
        }
      }

      //cout << "LINE=" << line_no-1 << ": " << instr << endl;
      // cout << "[DEBUG]: build_circuit_is_non_nacl: " << i << "] " << instr << "\n";

      auto constraints = (*filter)(instr, state);
      for (auto constraint : constraints) {
        // cout << "[DEBUG]: build_circuit_is_filter: " << i << "] " << instr << " | " << constraint << "\n";
        state.constraints.push_back(constraint);
      }

      if (filter->has_error()) {
        throw VALIDATOR_ERROR(filter->error());
      }
    }
  }
}

JumpType is_jump(const Cfg& cfg, const CfgPath& P, size_t i) {

  if (i == P.size() - 1)
    return JumpType::NONE;

  auto block = P[i];

  auto itr = cfg.succ_begin(block);
  if (itr == cfg.succ_end(block)) {
    // there are no successors
    //cout << "is_jump " << block << " NONE" << endl;
    return JumpType::NONE;
  }

  itr++;
  if (itr == cfg.succ_end(block)) {
    // there is only only successor
    //cout << "is_jump " << block << " NONE" << endl;
    return JumpType::NONE;
  }

  // ok, there are at least 2 successors
  auto next_block = P[i+1];
  if (next_block == block + 1) {
    //cout << "is_jump " << block << " FALL" << endl;
    return JumpType::FALL_THROUGH;
  }
  else {
    //cout << "is_jump " << block << " JUMP" << endl;
    return JumpType::JUMP;
  }
}

int main(int argc, char** argv) {
  // Parse input arguments or config file
  CommandLineConfig::strict_with_convenience(argc, argv);

  // Define some required objects
  SMTSolver* solver = new SolverGadget();
  auto bv = new BoundedValidator(*solver);

  // Load buggy and patched codes into Cfgs
  // Temporarily use --target for buggy and --rewrite for patched version
  FunctionsGadget aux_fxns;
  CfgGadget init_buggyP(target_arg.value(),aux_fxns, init_arg == Init::ZERO);
  CfgGadget init_patchedP(rewrite_arg.value(),aux_fxns, init_arg == Init::ZERO);

  // Extract all Cfg paths for both buggy and patched versions
  vector<CfgPath> buggy_paths;
  vector<CfgPath> patched_paths;

  auto buggyP = bv->inline_functions_public(init_buggyP);
  auto patchedP = bv->inline_functions_public(init_patchedP);

  // Background checks to make sure def_ins and live_outs are matched
  bv->sanity_checks_public(buggyP, patchedP);

  // Get all the paths from the enumerator (fixed max_len value to 8 - same value used in bounded validator)
  for (auto path : CfgPaths::enumerate_paths(buggyP, 8)) {
    buggy_paths.push_back(path);
  }

  for (auto path : CfgPaths::enumerate_paths(patchedP, 8)) {
    patched_paths.push_back(path);
  }

  // Sort the paths by length
  auto by_length = [](const CfgPath& lhs, const CfgPath& rhs) {
    return lhs.size() < rhs.size();
  };
  sort(buggy_paths.begin(), buggy_paths.end(), by_length);
  sort(patched_paths.begin(), patched_paths.end(), by_length);

  // Build the constraints (Handle one path for each Cfg first)
  // Build constraints on inputs and outputs based on def_ins and live_outs
  auto P = buggy_paths[0];
  auto Q = patched_paths[0];

  StateEqualityInvariant assume_state(buggyP.def_ins());
  StateEqualityInvariant prove_state(buggyP.live_outs());

  MemoryEqualityInvariant memory_equal;

  ConjunctionInvariant assume;
  assume.add_invariant(&assume_state);
  assume.add_invariant(&memory_equal);

  ConjunctionInvariant prove;
  prove.add_invariant(&prove_state);
  prove.add_invariant(&memory_equal);

  // XXX. Does not support memory objects
  auto memories = pair<CellMemory*,CellMemory*>(NULL, NULL);

  vector<SymBool> constraints,constraints2;
  vector<SymBool> EquivInput, DistinctOutput, Phi_b, Phi_p, DiffOut, SameOut;
  SymBool CC = SymBool::var("CC");
  SymState state_b("1_INIT");
  SymState state_p("2_INIT");

  FlatMemory initial_buggy_flat_memory;
  FlatMemory initial_patched_flat_memory;

  if (memories.first) {
    state_b.memory = memories.first;
    state_b.memory->set_parent(&state_b);
    state_p.memory = memories.second;
    state_p.memory->set_parent(&state_p);
  } else {
    state_b.memory = new FlatMemory();
    initial_buggy_flat_memory = *static_cast<FlatMemory*>(state_b.memory);
    state_p.memory = new FlatMemory();
    initial_patched_flat_memory = *static_cast<FlatMemory*>(state_p.memory);
  }

  // Add given assumptions
  // TODO pass line numbers as appropriate here
  size_t buggy_invariant_lineno = 0;
  size_t patched_invariant_lineno = 0;
  auto assumption = assume(state_b, state_p, buggy_invariant_lineno, patched_invariant_lineno);
  CONSTRAINT_DEBUG(cout << "Assuming " << assumption << endl;);
  constraints.push_back(assumption);

  EquivInput.push_back(assumption);

  // Compute line maps
  LineMap buggy_line_map;
  rewrite_cfg_with_path(buggyP, P, buggy_line_map);
  LineMap patched_line_map;
  rewrite_cfg_with_path(patchedP, Q, patched_line_map);

  // Build the circuits
  size_t line_no = 0;
  for (size_t i = 0; i < P.size(); ++i)
    build_circuit(buggyP, P[i], is_jump(buggyP,P,i), state_b, line_no, buggy_line_map);
  line_no = 0;
  for (size_t i = 0; i < Q.size(); ++i)
    build_circuit(patchedP, Q[i], is_jump(patchedP,Q,i), state_p, line_no, patched_line_map);

  if (memories.first)
    constraints.push_back(memories.first->aliasing_formula(*memories.second));
  else {
    auto buggy_flat = dynamic_cast<FlatMemory*>(state_b.memory);
    auto patched_flat = dynamic_cast<FlatMemory*>(state_p.memory);
    if (buggy_flat && patched_flat) {
      auto buggy_con = buggy_flat->get_constraints();
      constraints.insert(constraints.begin(),
                         buggy_con.begin(),
                         buggy_con.end());
      auto patched_con = patched_flat->get_constraints();
      constraints.insert(constraints.begin(),
                         patched_con.begin(),
                         patched_con.end());
    }
  }

  constraints.insert(constraints.begin(), state_b.constraints.begin(), state_b.constraints.end());
  constraints.insert(constraints.begin(), state_p.constraints.begin(), state_p.constraints.end());

  CONSTRAINT_DEBUG(
    cout << endl << "CONSTRAINTS" << endl << endl;;
  for (auto it : constraints) {
  cout << it << endl;
})

  // Build inequality constraint
  // TODO pass line numbers as appropriate
  auto prove_constraint = !prove(state_b, state_p, buggy_invariant_lineno, patched_invariant_lineno);
  CONSTRAINT_DEBUG(cout << "Proof inequality: " << prove_constraint << endl;)

  constraints.push_back(prove_constraint);

  DiffOut.push_back(prove_constraint);
  SameOut.push_back(prove(state_b, state_p, buggy_invariant_lineno, patched_invariant_lineno));


  // Extract the final states of target/rewrite
  SymState state_b_final("1_FINAL");
  SymState state_p_final("2_FINAL");

  for (auto it : state_b.equality_constraints(state_b_final, RegSet::universe())) {
    Phi_b.push_back(it);
    constraints.push_back(it);
  }
  for (auto it : state_p.equality_constraints(state_p_final, RegSet::universe())) {
    Phi_p.push_back(it);
    constraints.push_back(it);
  }

  SymBool firstFormula = SymBool::_true();
  firstFormula = firstFormula & !CC;
  for (auto it : EquivInput) {
    firstFormula = firstFormula & it;
  }
  for (auto it : Phi_b) {
    firstFormula = firstFormula & it;
  }
  for (auto it : Phi_p) {
    firstFormula = firstFormula & it;
  }
  for (auto it : DiffOut) {
    firstFormula = firstFormula & it;
  }

  SymBool secondFormula = SymBool::_true();
  secondFormula = secondFormula & CC;
  for (auto it : EquivInput) {
    secondFormula = secondFormula & it;
  }
  for (auto it : Phi_b) {
    secondFormula = secondFormula & it;
  }
  for (auto it : Phi_p) {
    secondFormula = secondFormula & it;
  }
  for (auto it : SameOut) {
    secondFormula = secondFormula & it;
  }

  constraints2.push_back(firstFormula | secondFormula);

  bool is_sat = solver->is_sat(constraints2);

  //cout << ((Z3Solver*)solver)->solver_.to_smt2();
  return 0;
}
