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

#include <regex>

#include "src/sandbox/sandbox.h"
#include "src/validator/bounded.h"
#include "src/validator/invariants/conjunction.h"
#include "src/validator/invariants/equality.h"
#include "src/validator/invariants/no_signals.h"
#include "src/validator/invariants/state_equality.h"
#include "src/validator/invariants/top_zero.h"
#include "src/validator/invariants/true.h"

namespace stoke {

class BoundedValidatorBaseTest : public ::testing::Test {

public:

  BoundedValidatorBaseTest() {
    solver = new Z3Solver();
    sandbox = new Sandbox();
    sandbox->set_max_jumps(4096);
    sandbox->set_abi_check(false);
    sg_sandbox = new Sandbox();
    sg_sandbox->set_max_jumps(4096);
    sg_sandbox->set_abi_check(false);
    validator = new BoundedValidator(*solver);
    validator->set_bound(2);
    validator->set_sandbox(sandbox);
    validator->set_alias_strategy(BoundedValidator::AliasStrategy::STRING);
    validator->set_heap_out(true);
    validator->set_stack_out(true);
  }

  ~BoundedValidatorBaseTest() {
    delete validator;
    delete sandbox;
    delete sg_sandbox;
    delete solver;
  }

protected:

  static x64asm::RegSet all() {
    auto rs = x64asm::RegSet::all_gps() | x64asm::RegSet::all_ymms();
    rs = rs + x64asm::eflags_cf + x64asm::eflags_zf + x64asm::eflags_pf + x64asm::eflags_of + x64asm::eflags_sf;
    return rs;
  }

  void fail() {
    FAIL();
  }

  void check_ceg(const CpuState& tc, const Cfg& target, const Cfg& rewrite, bool print = false) {
    Sandbox sb;
    sb.set_max_jumps(4096);
    sb.set_abi_check(false);
    sb.insert_input(tc);

    sb.insert_function(target);
    sb.set_entrypoint(target.get_code()[0].get_operand<x64asm::Label>(0));

    sb.run();
    auto target_output = *sb.get_output(0);

    sb.insert_function(rewrite);
    sb.set_entrypoint(rewrite.get_code()[0].get_operand<x64asm::Label>(0));

    sb.run();
    auto rewrite_output = *sb.get_output(0);

    EXPECT_EQ(ErrorCode::NORMAL, target_output.code);
    EXPECT_NE(target_output, rewrite_output);

    if (print) {
      std::cout << "Counterexample:" << std::endl << tc << std::endl;
      std::cout << "Target state:" << std::endl << target_output << std::endl;
      std::cout << "Rewrite state:" << std::endl << rewrite_output << std::endl;
    }
  }

  Cfg make_cfg(std::stringstream& ss, x64asm::RegSet di = all(), x64asm::RegSet lo = all(), uint64_t rip_offset = 0) {
    x64asm::Code c;
    ss >> c;
    if (ss.fail()) {
      std::cerr << "Parsing error!" << std::endl;
      std::cerr << cpputil::fail_msg(ss) << std::endl;
      fail();
    }
    TUnit fxn(c, 0, rip_offset, 0);
    return Cfg(fxn, di, lo);
  }

  CpuState get_state() {
    CpuState cs;
    StateGen sg(sg_sandbox);
    sg.get(cs);
    return cs;
  }

  CpuState get_state(const Cfg& cfg) {
    CpuState cs;
    StateGen sg(sg_sandbox);
    bool b = sg.get(cs, cfg);
    if (!b) {
      std::cerr << "Couldn't generate a state!" << std::endl;
      std::cerr << sg.get_error() << std::endl;
      fail();
    }
    return cs;
  }

  SMTSolver* solver;
  BoundedValidator* validator;
  Sandbox* sandbox;
  Sandbox* sg_sandbox;
};

TEST_F(BoundedValidatorBaseTest, NoLoopsPasses) {

  auto live_outs = all();

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "incq %rax" << std::endl;
  sst << "cmpq $0x10, %rax" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, live_outs, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "addq $0x1, %rax" << std::endl;
  ssr << "cmpq $0x10, %rax" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, live_outs, live_outs);

  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
}

TEST_F(BoundedValidatorBaseTest, NoLoopsFails) {

  auto live_outs = all();

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "incq %rax" << std::endl;
  sst << "cmpq $0x10, %rax" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, live_outs, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "addq $0x1, %rax" << std::endl;
  ssr << "cmpq $0x11, %rax" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, live_outs, live_outs);

  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

}

TEST_F(BoundedValidatorBaseTest, UnsupportedInstruction) {

  auto live_outs = all();

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "cpuid" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, live_outs, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "cpuid" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, live_outs, live_outs);

  EXPECT_FALSE(validator->verify(target, rewrite));
  ASSERT_TRUE(validator->has_error());

  EXPECT_TRUE(std::regex_match(validator->error(),
                               std::regex(".*unsupported.*", std::regex_constants::icase)))
      << "Error message: " << validator->error();

}

TEST_F(BoundedValidatorBaseTest, RipOffsetEqual) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "movq 0x1000(%rip), %rax" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, all(), live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "movq 0x0fff(%rip), %rax" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, all(), live_outs);

  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error());

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

}

TEST_F(BoundedValidatorBaseTest, RipOffsetUnequal) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "movq 0x1000(%rip), %rax" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, all(), live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "movq 0x0fff(%rip), %rax" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, all(), live_outs);

  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error());

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

}

TEST_F(BoundedValidatorBaseTest, RipOffsetLoopEqual) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "movq 0x1000(%rip), %rax" << std::endl;
  sst << "incq %rdx" << std::endl;
  sst << "cmpq %rax, 0x2000(%rdx)" << std::endl;
  sst << "je .foo" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, all(), live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "movq 0x0fff(%rip), %rax" << std::endl;
  ssr << "incq %rdx" << std::endl;
  ssr << "cmpq %rax, 0x2000(%rdx)" << std::endl;
  ssr << "je .foo" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, all(), live_outs);

  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error());

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

}

TEST_F(BoundedValidatorBaseTest, RipOffsetLoopUnqual) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "movq 0x1000(%rip), %rax" << std::endl;
  sst << "incq %rdx" << std::endl;
  sst << "cmpq %rax, 0x2000(%rdx)" << std::endl;
  sst << "je .foo" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, all(), live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "movq 0x1fff(%rip), %rax" << std::endl;
  ssr << "incq %rdx" << std::endl;
  ssr << "cmpq %rax, 0x2000(%rdx)" << std::endl;
  ssr << "je .foo" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, all(), live_outs);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();


}

TEST_F(BoundedValidatorBaseTest, RipOffsetCorrectValue) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "leaq (%rip), %rax" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, all(), live_outs, 0xcafef00d);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  // (remember to add 7 b/c of instruction length)
  ssr << "movq $0xcafef014, %rax" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, all(), live_outs, 0xd00dface);

  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error());

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
}

TEST_F(BoundedValidatorBaseTest, RipWritingEquiv) {

  auto live_outs = x64asm::RegSet::empty();

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "leaq (%rip), %rax" << std::endl;
  sst << "movq $0xc0ded00d, 0x4(%rax)" << std::endl;
  sst << "xorl %eax, %eax" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, all(), live_outs, 0xcafef00d);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "movq $0xc0ded00d, (%rip)" << std::endl;
  sst << "xorl %eax, %eax" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, all(), live_outs, 0xcafef00d);

  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error());

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
}

TEST_F(BoundedValidatorBaseTest, RipOffsetWrongValue) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "leaq 0x1(%rip), %rax" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, all(), live_outs, 0xcafef00d);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "movq $0xcafef00d, %rax" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, all(), live_outs);

  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error());

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
}

TEST_F(BoundedValidatorBaseTest, PopcntEqual) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".popcnt:" << std::endl;
  sst << "xorl %eax, %eax" << std::endl;
  sst << "testq %rdi, %rdi" << std::endl;
  sst << "je .end" << std::endl;
  sst << ".loop:" << std::endl;
  sst << "movl %edi, %edx" << std::endl;
  sst << "andl $0x1, %edx" << std::endl;
  sst << "addl %edx, %eax" << std::endl;
  sst << "shrq $0x1, %rdi" << std::endl;
  sst << "jne .loop" << std::endl;
  sst << ".end:" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, all(), live_outs);

  std::stringstream ssr;
  ssr << ".popcnt:" << std::endl;
  ssr << "popcntq %rdi, %rax" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, all(), live_outs);

  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
}

TEST_F(BoundedValidatorBaseTest, PopcntWrong) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".popcnt:" << std::endl;
  sst << "xorl %eax, %eax" << std::endl;
  sst << "testq %rdi, %rdi" << std::endl;
  sst << "je .end" << std::endl;
  sst << ".loop:" << std::endl;
  sst << "movl %edi, %edx" << std::endl;
  sst << "andl $0x1, %edx" << std::endl;
  sst << "addl %edx, %eax" << std::endl;
  sst << "shrq $0x1, %rdi" << std::endl;
  sst << "jne .loop" << std::endl;
  sst << ".end:" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, all(), live_outs);

  std::stringstream ssr;
  ssr << ".popcnt:" << std::endl;
  ssr << "cmpl $0x42, %edi" << std::endl;
  ssr << "je .gotcha" << std::endl;
  ssr << "popcntq %rdi, %rax" << std::endl;
  ssr << ".gotcha:" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, all(), live_outs);

  validator->set_bound(8);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);
}

TEST_F(BoundedValidatorBaseTest, PopcntWrongBeyondBound) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".popcnt:" << std::endl;
  sst << "xorl %eax, %eax" << std::endl;
  sst << "testq %rdi, %rdi" << std::endl;
  sst << "je .end" << std::endl;
  sst << ".loop:" << std::endl;
  sst << "movl %edi, %edx" << std::endl;
  sst << "andl $0x1, %edx" << std::endl;
  sst << "addl %edx, %eax" << std::endl;
  sst << "shrq $0x1, %rdi" << std::endl;
  sst << "jne .loop" << std::endl;
  sst << ".end:" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, all(), live_outs);

  std::stringstream ssr;
  ssr << ".popcnt:" << std::endl;
  ssr << "cmpl $0x42, %edi" << std::endl;
  ssr << "je .gotcha" << std::endl;
  ssr << "popcntq %rdi, %rax" << std::endl;
  ssr << ".gotcha:" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, all(), live_outs);

  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

}

TEST_F(BoundedValidatorBaseTest, EasyMemory) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "incq %rax" << std::endl;
  sst << "addl $0x5, (%rax)" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, live_outs, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "incq %rax" << std::endl;
  ssr << "addl $0x4, (%rax)" << std::endl;
  ssr << "addl $0x1, (%rax)" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, live_outs, live_outs);

  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

}

TEST_F(BoundedValidatorBaseTest, EasyMemoryFail) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "incq %rax" << std::endl;
  sst << "addl $0x5, (%rax)" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, live_outs, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "incq %rax" << std::endl;
  ssr << "addl $0x4, (%rax)" << std::endl;
  ssr << "addl $0x2, (%rax)" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, live_outs, live_outs);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::STRING);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

}


TEST_F(BoundedValidatorBaseTest, CanTurnOffMemoryChecking) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "incq %rax" << std::endl;
  sst << "addl $0x5, (%rax)" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, live_outs, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "incq %rax" << std::endl;
  ssr << "addl $0x4, (%rax)" << std::endl;
  ssr << "addl $0x2, (%rax)" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, live_outs, live_outs);

  validator->set_heap_out(false);
  validator->set_stack_out(false);
  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_EQ(0ul, validator->counter_examples_available());

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

}

TEST_F(BoundedValidatorBaseTest, NoHeapOutStackOutStillSensitiveToReads) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "movl (%rax), %eax" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, live_outs, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  sst << "movq (%rax), %rax" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, live_outs, live_outs);

  validator->set_heap_out(false);
  validator->set_stack_out(false);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_LE(1ul, validator->counter_examples_available());

  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);


}

TEST_F(BoundedValidatorBaseTest, WriteDifferentPointers) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax + x64asm::rdx;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "incq %rax" << std::endl;
  sst << "addl $0x5, (%rax)" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, live_outs, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "incq %rdx" << std::endl;
  ssr << "addl $0x5, (%rdx)" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, live_outs, live_outs);

  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

}

TEST_F(BoundedValidatorBaseTest, MemoryOverlapEquiv) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "movl $0xc0decafe, (%rax)" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, live_outs, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "movw $0xcafe, (%rax)" << std::endl;
  ssr << "movw $0xc0de, 0x2(%rax)" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, live_outs, live_outs);

  EXPECT_TRUE(validator->verify(target, rewrite)) << std::endl;
  EXPECT_FALSE(validator->has_error()) << validator->error();

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_TRUE(validator->verify(target, rewrite)) << std::endl;
  EXPECT_FALSE(validator->has_error()) << validator->error();

}

TEST_F(BoundedValidatorBaseTest, MemoryOverlapEquiv2) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "movl $0xc0decafe, (%rax)" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, live_outs, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "movb $0xfe, (%rax)" << std::endl;
  ssr << "movb $0xca, 0x1(%rax)" << std::endl;
  ssr << "movw $0xc0de, 0x2(%rax)" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, live_outs, live_outs);

  EXPECT_TRUE(validator->verify(target, rewrite)) << std::endl;
  EXPECT_FALSE(validator->has_error()) << validator->error();

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_TRUE(validator->verify(target, rewrite)) << std::endl;
  EXPECT_FALSE(validator->has_error()) << validator->error();
}

TEST_F(BoundedValidatorBaseTest, MemoryOverlapBad) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "movl $0xc0decafe, (%rax)" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, live_outs, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "movb $0xfe, -0x1(%rax)" << std::endl;
  ssr << "movb $0xca, 0x0(%rax)" << std::endl;
  ssr << "movw $0xc0de, 0x1(%rax)" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, live_outs, live_outs);

  EXPECT_FALSE(validator->verify(target, rewrite)) << std::endl;
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite)) << std::endl;
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);
}

TEST_F(BoundedValidatorBaseTest, LoopMemoryEquiv) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rax + x64asm::ecx + x64asm::rdx;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "incq %rax" << std::endl;
  sst << "movl %ecx, (%rdx, %rax, 4)" << std::endl;
  sst << "cmpl $0x10, %eax" << std::endl;
  sst << "jne .foo" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "movl %ecx, 0x4(%rdx, %rax, 4)" << std::endl;
  ssr << "incq %rax" << std::endl;
  ssr << "cmpl $0x10, %eax" << std::endl;
  ssr << "jne .foo" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);

  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  // Takes way too long
  /*
  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  validator->set_bound(1);
  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  */

}

TEST_F(BoundedValidatorBaseTest, LoopMemoryWrong) {

  auto live_outs = x64asm::RegSet::empty() + x64asm::rax + x64asm::rdx;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "incl %eax" << std::endl;
  sst << "movl %eax, (%rdx, %rax, 4)" << std::endl;
  sst << "cmpl $0x10, %eax" << std::endl;
  sst << "jne .foo" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, live_outs, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "movl %eax, (%rdx, %rax, 4)" << std::endl;
  ssr << "incl %eax" << std::endl;
  ssr << "cmpl $0x10, %eax" << std::endl;
  ssr << "jne .foo" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, live_outs, live_outs);

  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

}

TEST_F(BoundedValidatorBaseTest, LoopMemoryWrong2) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rax + x64asm::ecx + x64asm::rdx;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "incl %eax" << std::endl;
  sst << "movl %ecx, (%rdx, %rax, 4)" << std::endl;
  sst << "cmpl $0x10, %eax" << std::endl;
  sst << "jne .foo" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "addl $0x1, %ecx" << std::endl;
  ssr << "movl %ecx, 0x4(%rdx, %rax, 4)" << std::endl;
  ssr << "subl $0x1, %ecx" << std::endl;
  ssr << "incl %eax" << std::endl;
  ssr << "cmpl $0x10, %eax" << std::endl;
  ssr << "jne .foo" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);

  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);
}

TEST_F(BoundedValidatorBaseTest, Wcslen2ExitsPass) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rdi;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".wcslen:" << std::endl;
  sst << "movq %rdi, %rsi" << std::endl;
  sst << ".head:" << std::endl;
  sst << "movl (%rdi), %ecx" << std::endl;
  sst << "addq $0x4, %rdi" << std::endl;
  sst << "testl %ecx, %ecx" << std::endl;
  sst << "jnz .head" << std::endl;
  sst << "subq %rsi, %rdi" << std::endl;
  sst << "subq $0x4, %rdi" << std::endl;
  sst << "movq %rdi, %rax" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".wcslen:" << std::endl;
  ssr << "movq %rdi, %rsi" << std::endl;
  ssr << "movl (%rdi), %ecx" << std::endl;
  ssr << "cmpl $0x0, %ecx" << std::endl;
  ssr << "je .exit" << std::endl;
  ssr << ".head:" << std::endl;
  ssr << "addq $0x4, %rdi" << std::endl;
  ssr << "movl (%rdi), %ecx" << std::endl;
  ssr << "testl %ecx, %ecx" << std::endl;
  ssr << "jnz .head" << std::endl;
  ssr << "subq %rsi, %rdi" << std::endl;
  ssr << "movq %rdi, %rax" << std::endl;
  ssr << "retq" << std::endl;
  ssr << ".exit:" << std::endl;
  ssr << "xorl %eax, %eax" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);

  for (size_t i = 0; i < 10; ++i) {
    CpuState tc;
    uint64_t base = rand();
    tc.gp[x64asm::rdi].get_fixed_quad(0) = base;
    tc.heap.resize(base, (i+1)*4 + 1);
    for (size_t j = base; j < base + i*4; ++j) {
      tc.heap.set_valid(j, true);
      tc.heap[j] = rand() % 256;
    }
    for (size_t j = base+i*4; j < base+(i+1)*4; ++j) {
      tc.heap.set_valid(j, true);
      tc.heap[j] = 0;
    }
    sandbox->insert_input(tc);
  }

  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

}

TEST_F(BoundedValidatorBaseTest, Wcslen2ExitsFail1) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rdi;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".wcslen:" << std::endl;
  sst << "movq %rdi, %rsi" << std::endl;
  sst << ".head:" << std::endl;
  sst << "movl (%rdi), %ecx" << std::endl;
  sst << "addq $0x4, %rdi" << std::endl;
  sst << "testl %ecx, %ecx" << std::endl;
  sst << "jnz .head" << std::endl;
  sst << "subq %rsi, %rdi" << std::endl;
  // missing sutract statement
  sst << "movq %rdi, %rax" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".wcslen:" << std::endl;
  ssr << "movq %rdi, %rsi" << std::endl;
  ssr << "movl (%rdi), %ecx" << std::endl;
  ssr << "cmpl $0x0, %ecx" << std::endl;
  ssr << "je .exit" << std::endl;
  ssr << ".head:" << std::endl;
  ssr << "addq $0x4, %rdi" << std::endl;
  ssr << "movl (%rdi), %ecx" << std::endl;
  ssr << "testl %ecx, %ecx" << std::endl;
  ssr << "jnz .head" << std::endl;
  ssr << "subq %rsi, %rdi" << std::endl;
  ssr << "movq %rdi, %rax" << std::endl;
  ssr << "retq" << std::endl;
  ssr << ".exit:" << std::endl;
  ssr << "xorl %eax, %eax" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);

  for (size_t i = 0; i < 10; ++i) {
    CpuState tc;
    uint64_t base = rand();
    tc.gp[x64asm::rdi].get_fixed_quad(0) = base;
    tc.heap.resize(base, (i+1)*4 + 1);
    for (size_t j = base; j < base + i*4; ++j) {
      tc.heap.set_valid(j, true);
      tc.heap[j] = rand() % 256;
    }
    for (size_t j = base+i*4; j < base+(i+1)*4; ++j) {
      tc.heap.set_valid(j, true);
      tc.heap[j] = 0;
    }
    sandbox->insert_input(tc);
  }

  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto ceg : validator->get_counter_examples())
    check_ceg(ceg, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto ceg : validator->get_counter_examples())
    check_ceg(ceg, target, rewrite);

}

TEST_F(BoundedValidatorBaseTest, LoopMemoryWrong3) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rax + x64asm::ecx + x64asm::rdx;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "incl %eax" << std::endl;
  sst << "movl %ecx, (%rdx, %rax, 4)" << std::endl;
  sst << "cmpl $0x10, %eax" << std::endl;
  sst << "jne .foo" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "cmpl $0x10, %eax" << std::endl;
  ssr << "je .exit" << std::endl;
  ssr << "addl $0x1, %ecx" << std::endl;
  ssr << "movl %ecx, 0x4(%rdx, %rax, 4)" << std::endl;
  ssr << "subl $0x1, %ecx" << std::endl;
  ssr << "incl %eax" << std::endl;
  ssr << "cmpl $0x10, %eax" << std::endl;
  ssr << "jne .foo" << std::endl;
  ssr << ".exit:" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);

  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

}

TEST_F(BoundedValidatorBaseTest, MemcpyCorrect) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rsi + x64asm::rdi + x64asm::edx;
  auto live_outs = x64asm::RegSet::empty();

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "xorl %ecx, %ecx" << std::endl;
  sst << "testl %edx, %edx" << std::endl;
  sst << "je .exit" << std::endl;
  sst << ".top:" << std::endl;
  sst << "movl (%rdi, %rcx, 4), %eax" << std::endl;
  sst << "movl %eax, (%rsi, %rcx, 4)" << std::endl;
  sst << "incl %ecx" << std::endl;
  sst << "cmpl %ecx, %edx" << std::endl;
  sst << "jne .top" << std::endl;
  sst << ".exit:" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "movl $0x0, %ecx" << std::endl;
  ssr << "testl %edx, %edx" << std::endl;
  ssr << "je .exit" << std::endl;
  ssr << ".top:" << std::endl;
  ssr << "movl (%rdi, %rcx, 4), %r8d" << std::endl;
  ssr << "addl $0x1, %ecx" << std::endl;
  ssr << "movl %r8d, -0x4(%rsi, %rcx, 4)" << std::endl;
  ssr << "cmpl %ecx, %edx" << std::endl;
  ssr << "jne .top" << std::endl;
  ssr << ".exit:" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);

  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

}



TEST_F(BoundedValidatorBaseTest, MemcpyVectorizedWrongWithAliasing) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rsi + x64asm::rdi + x64asm::edx;
  auto live_outs = x64asm::RegSet::empty();

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "xorl %ecx, %ecx" << std::endl;
  sst << "testl %edx, %edx" << std::endl;
  sst << "je .exit" << std::endl;
  sst << ".top:" << std::endl;
  sst << "movl (%rdi, %rcx, 4), %eax" << std::endl;
  sst << "movl %eax, (%rsi, %rcx, 4)" << std::endl;
  sst << "incl %ecx" << std::endl;
  sst << "cmpl %ecx, %edx" << std::endl;
  sst << "jne .top" << std::endl;
  sst << ".exit:" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "xorl %ecx, %ecx" << std::endl;
  ssr << "jmpq .enter" << std::endl;
  ssr << ".double:" << std::endl;
  ssr << "movq (%rdi, %rcx, 4), %rax" << std::endl;
  ssr << "movq %rax, (%rsi, %rcx, 4)" << std::endl;
  ssr << "addl $0x2, %ecx" << std::endl;
  ssr << "subl $0x2, %edx" << std::endl;
  ssr << ".enter:" << std::endl;
  ssr << "cmpl $0x1, %edx" << std::endl;
  ssr << "je .one_more" << std::endl;
  ssr << "cmpl $0x0, %edx" << std::endl;
  ssr << "je .exit" << std::endl;
  ssr << "jmpq .double" << std::endl;
  ssr << ".one_more:" << std::endl;
  ssr << "movl (%rdi, %rcx, 4), %eax" << std::endl;
  ssr << "movl %eax, (%rsi, %rcx, 4)" << std::endl;
  ssr << ".exit:" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);

  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  for (auto it : validator->get_counter_examples()) {
    check_ceg(it, target, rewrite);
  }

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);


}


TEST_F(BoundedValidatorBaseTest, MemcpyVectorizedCorrectWithoutAliasing) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rsi + x64asm::rdi + x64asm::edx;
  auto live_outs = x64asm::RegSet::empty();

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "xorl %ecx, %ecx" << std::endl;
  sst << "testl %edx, %edx" << std::endl;
  sst << "je .exit" << std::endl;
  sst << ".top:" << std::endl;
  sst << "movl (%rdi, %rcx, 4), %eax" << std::endl;
  sst << "movl %eax, (%rsi, %rcx, 4)" << std::endl;
  sst << "incl %ecx" << std::endl;
  sst << "cmpl %ecx, %edx" << std::endl;
  sst << "jne .top" << std::endl;
  sst << ".exit:" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "xorl %ecx, %ecx" << std::endl;
  ssr << "jmpq .enter" << std::endl;
  ssr << ".double:" << std::endl;
  ssr << "movq (%rdi, %rcx, 4), %rax" << std::endl;
  ssr << "movq %rax, (%rsi, %rcx, 4)" << std::endl;
  ssr << "addl $0x2, %ecx" << std::endl;
  ssr << "subl $0x2, %edx" << std::endl;
  ssr << ".enter:" << std::endl;
  ssr << "cmpl $0x1, %edx" << std::endl;
  ssr << "je .one_more" << std::endl;
  ssr << "cmpl $0x0, %edx" << std::endl;
  ssr << "je .exit" << std::endl;
  ssr << "jmpq .double" << std::endl;
  ssr << ".one_more:" << std::endl;
  ssr << "movl (%rdi, %rcx, 4), %eax" << std::endl;
  ssr << "movl %eax, (%rsi, %rcx, 4)" << std::endl;
  ssr << ".exit:" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::STRING_NO_ALIAS);
  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

}

TEST_F(BoundedValidatorBaseTest, MemcpyMissingBranch) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rsi + x64asm::rdi + x64asm::edx;
  auto live_outs = x64asm::RegSet::empty();

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "xorl %ecx, %ecx" << std::endl;
  sst << "testl %edx, %edx" << std::endl;
  sst << "je .exit" << std::endl;
  sst << ".top:" << std::endl;
  sst << "movl (%rdi, %rcx, 4), %eax" << std::endl;
  sst << "movl %eax, (%rsi, %rcx, 4)" << std::endl;
  sst << "incl %ecx" << std::endl;
  sst << "cmpl %ecx, %edx" << std::endl;
  sst << "ja .top" << std::endl;
  sst << ".exit:" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "movl $0x0, %ecx" << std::endl;
  ssr << ".top:" << std::endl;
  ssr << "movl (%rdi, %rcx, 4), %r8d" << std::endl;
  ssr << "addl $0x1, %ecx" << std::endl;
  ssr << "movl %r8d, -0x4(%rsi, %rcx, 4)" << std::endl;
  ssr << "cmpl %ecx, %edx" << std::endl;
  ssr << "ja .top" << std::endl;
  ssr << ".exit:" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);

  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

}

TEST_F(BoundedValidatorBaseTest, MemoryCounterexample) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rdi;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "movl (%rdi), %eax" << std::endl;
  sst << "shll $0x2, %eax" << std::endl;
  sst << "shrl $0x1, %eax" << std::endl;
  sst << "leaq 0x10(%rdi), %rsp" << std::endl;
  sst << "pushq %rax" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "movl (%rdi), %eax" << std::endl;
  ssr << "shll $0x1, %eax" << std::endl;
  ssr << "leaq 0x10(%rdi), %rsp" << std::endl;
  ssr << "pushq %rax" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);

  for (size_t i = 0; i < 2; ++i) {
    if (i == 0)
      validator->set_alias_strategy(BoundedValidator::AliasStrategy::STRING);
    else
      validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);

    EXPECT_FALSE(validator->verify(target, rewrite));
    EXPECT_FALSE(validator->has_error()) << validator->error();

    ASSERT_LE(1ul, validator->counter_examples_available());

    auto ceg = validator->get_counter_examples()[0];

    for (auto it : validator->get_counter_examples())
      check_ceg(it, target, rewrite);

    /** rdi is pointing to 0x40000000 */
    uint64_t addr = ceg[x64asm::rdi]+3;
    if (ceg.heap.in_range(addr))
      EXPECT_EQ(0x40, ceg.heap[ceg[x64asm::rdi]+3] & 0x40);
    else if (ceg.stack.in_range(addr))
      EXPECT_EQ(0x40, ceg.stack[ceg[x64asm::rdi]+3] & 0x40);
    else
      FAIL() << "Address " << addr << " not mapped in testcase" << std::endl;

    /** check the counterexample runs */
    Sandbox sb;
    sb.set_max_jumps(4);
    sb.set_abi_check(false);
    sb.insert_function(target);
    sb.insert_input(ceg);
    sb.set_entrypoint(target.get_code()[0].get_operand<x64asm::Label>(0));
    sb.run();

    auto target_output = *sb.get_output(0);

    sb.insert_function(rewrite);
    sb.set_entrypoint(rewrite.get_code()[0].get_operand<x64asm::Label>(0));
    sb.run();

    auto rewrite_output = *sb.get_output(0);

    EXPECT_EQ(ErrorCode::NORMAL, target_output.code);
    EXPECT_EQ(ErrorCode::NORMAL, rewrite_output.code);
    EXPECT_NE(target_output[x64asm::rax], rewrite_output[x64asm::rax]);
  }
}

TEST_F(BoundedValidatorBaseTest, StrlenCorrect) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rdi;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rdi;

  std::stringstream sst;
  sst << ".strlen:" << std::endl;
  sst << "movzbl (%rdi), %eax" << std::endl;
  sst << "testl %eax, %eax" << std::endl;
  sst << "je .exit" << std::endl;
  sst << "addq $0x1, %rdi" << std::endl;
  sst << "jmpq .strlen" << std::endl;
  sst << ".exit:" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".strlen:" << std::endl;
  ssr << "addq $0x1, %rdi" << std::endl;
  ssr << "movzbl -0x1(%rdi), %eax" << std::endl;
  ssr << "cmpl $0x0, %eax" << std::endl;
  ssr << "jne .strlen" << std::endl;
  ssr << "subq $0x1, %rdi" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);


  for (size_t i = 0; i < 20; ++i) {
    CpuState tc = get_state();
    size_t count = rand() % 10;
    uint64_t start = tc[x64asm::rdi];
    tc.heap.resize(start, count+1);
    for (size_t i = 0; i < count; ++i) {
      tc.heap.set_valid(start + i, true);
      tc.heap[start+i] = rand() % 256;
    }
    tc.heap.set_valid(start+count, true);
    tc.heap[start+count] = 0;

    uint64_t stack_start = tc[x64asm::rsp] - 8;
    tc.stack.resize(stack_start, 16);
    for (size_t i = stack_start; i < stack_start+16; ++i) {
      tc.stack.set_valid(i, true);
      tc.stack[i] = rand() % 256;
    }
    sandbox->insert_input(tc);
  }

  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();


}

TEST_F(BoundedValidatorBaseTest, StrlenWrongBranch) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rdi;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rdi;

  std::stringstream sst;
  sst << ".strlen:" << std::endl;
  sst << "movzbl (%rdi), %eax" << std::endl;
  sst << "testl %eax, %eax" << std::endl;
  sst << "je .exit" << std::endl;
  sst << "addq $0x1, %rdi" << std::endl;
  sst << "jmpq .strlen" << std::endl;
  sst << ".exit:" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".strlen:" << std::endl;
  ssr << "addq $0x1, %rdi" << std::endl;
  ssr << "movzbl -0x1(%rdi), %eax" << std::endl;
  ssr << "shrl $0x1, %eax" << std::endl;
  ssr << "jnz .strlen" << std::endl;
  ssr << "subq $0x1, %rdi" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);

  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  ASSERT_LE(1ul, validator->counter_examples_available());

  for (auto ceg : validator->get_counter_examples()) {
    check_ceg(ceg, target, rewrite);
  }

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

  ASSERT_LE(1ul, validator->counter_examples_available());

  for (auto ceg : validator->get_counter_examples()) {
    check_ceg(ceg, target, rewrite);
  }


}

TEST_F(BoundedValidatorBaseTest, WcslenCorrect) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rdi + x64asm::r15;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;
  validator->set_nacl(true);

  std::stringstream sst;
  sst << ".wcslen:" << std::endl; // BB 1
  sst << "leal (%rdi), %ecx" << std::endl;
  sst << "movl (%r15, %rcx), %ecx" << std::endl;
  sst << "testl %ecx, %ecx" << std::endl;
  sst << "je .L_22" << std::endl;
  sst << "movq %rdi, %rax" << std::endl; //BB 2
  sst << ".L_10:" << std::endl; // BB3
  sst << "addq $0x4, %rax" << std::endl;
  sst << "leal (%rax), %edx" << std::endl;
  sst << "movl (%r15, %rdx), %edx" << std::endl;
  sst << "testl %edx, %edx" << std::endl;
  sst << "jne .L_10" << std::endl;
  sst << "subq %rdi, %rax" << std::endl; // BB4
  sst << "sarq $0x2, %rax" << std::endl;
  sst << "retq" << std::endl;
  sst << ".L_22:" << std::endl; // BB5
  sst << "xorl %eax, %eax" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".wcslen:" << std::endl; //BB1
  ssr << "movl %edi, %eax" << std::endl;
  ssr << "movl (%r15, %rax, 1), %ecx" << std::endl;
  ssr << "testl %ecx, %ecx" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "je .L_22" << std::endl;
  ssr << ".L_10:" << std::endl; //BB2
  ssr << "addl $0x4, %eax" << std::endl;
  ssr << "movl (%r15, %rax, 1), %edx" << std::endl;
  ssr << "testl %edx, %edx" << std::endl;
  ssr << "jne .L_10" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "subl %edi, %eax" << std::endl; //BB3
  ssr << "sarq $0x2, %rax" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "retq" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << ".L_22:" << std::endl; //BB4
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "addb $0x80, %al" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "andl %ecx, %eax" << std::endl;
  ssr << "nopl %eax" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nopl %eax" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);

  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_EQ(0ul, validator->counter_examples_available());

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();

}

TEST_F(BoundedValidatorBaseTest, DISABLED_WcslenCorrect2) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rdi + x64asm::r15;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  validator->set_nacl(true);
  std::stringstream sst;
  sst << ".wcslen:" << std::endl; // BB 1
  sst << "leal (%rdi), %ecx" << std::endl;
  sst << "leaq (%r15, %rcx), %rdx" << std::endl;
  sst << "movl (%rdx), %ecx" << std::endl;
  sst << "testl %ecx, %ecx" << std::endl;
  sst << "je .L_22" << std::endl;
  sst << "movq %rdx, %rsi" << std::endl;
  sst << ".L_10:" << std::endl; // BB3
  sst << "addq $0x4, %rdx" << std::endl;
  sst << "movl (%rdx), %ecx" << std::endl;
  sst << "testl %ecx, %ecx" << std::endl;
  sst << "jne .L_10" << std::endl;
  sst << "subq %rsi, %rdx" << std::endl; // BB4
  sst << "movq %rdx, %rax" << std::endl;
  sst << "sarq $0x2, %rax" << std::endl;
  sst << "retq" << std::endl;
  sst << ".L_22:" << std::endl; // BB5
  sst << "xorl %eax, %eax" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".wcslen:" << std::endl;
  ssr << "movl %edi, %edi" << std::endl;
  ssr << "addq %r15, %rdi" << std::endl;
  ssr << "movq %rdi, %rsi" << std::endl;
  ssr << ".head:" << std::endl;
  ssr << "movl (%rdi), %ecx" << std::endl;
  ssr << "addq $0x4, %rdi" << std::endl;
  ssr << "testl %ecx, %ecx" << std::endl;
  ssr << "jnz .head" << std::endl;
  ssr << "subq %rsi, %rdi" << std::endl;
  ssr << "subq $0x4, %rdi" << std::endl;
  ssr << "shrq $0x2, %rdi" << std::endl;
  ssr << "movq %rdi, %rax" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);

  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_EQ(0ul, validator->counter_examples_available());

  for (auto it : validator->get_counter_examples()) {
    std::cout << "CEG: " << std::endl << it << std::endl;
    check_ceg(it, target, rewrite);
  }

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_EQ(0ul, validator->counter_examples_available());

  for (auto ceg : validator->get_counter_examples()) {
    check_ceg(ceg, target, rewrite);
  }


}

TEST_F(BoundedValidatorBaseTest, WcslenWrong1) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rdi + x64asm::r15;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;
  validator->set_nacl(true);

  std::stringstream sst;
  sst << ".wcslen:" << std::endl; // BB 1
  sst << "leal (%rdi), %ecx" << std::endl;
  sst << "movl (%r15, %rcx), %ecx" << std::endl;
  sst << "testl %ecx, %ecx" << std::endl;
  sst << "je .L_22" << std::endl;
  sst << "movq %rdi, %rax" << std::endl; //BB 2
  sst << ".L_10:" << std::endl; // BB3
  sst << "addq $0x4, %rax" << std::endl;
  sst << "leal (%rax), %edx" << std::endl;
  sst << "movl (%r15, %rdx), %edx" << std::endl;
  sst << "testl %edx, %edx" << std::endl;
  sst << "jne .L_10" << std::endl;
  sst << "subq %rdi, %rax" << std::endl; // BB4
  sst << "sarq $0x2, %rax" << std::endl;
  sst << "retq" << std::endl;
  sst << ".L_22:" << std::endl; // BB5
  sst << "xorl %eax, %eax" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".wcslen:" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "movl %edi, %eax" << std::endl;
  ssr << "movl (%r15,%rax,1), %ecx" << std::endl;
  ssr << "testl %ecx, %ecx" << std::endl;
  ssr << "je .L_22" << std::endl;
  ssr << "nop" << std::endl;
  ssr << ".L_10:" << std::endl;
  ssr << "addl $0x4, %eax" << std::endl;
  ssr << "movl (%r15,%rax,1), %edx" << std::endl;
  ssr << "shrq $0x2, %rdx" << std::endl;
  ssr << "jne .L_10" << std::endl;
  ssr << "subq %rdi, %rax" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "sarl $0x2, %eax" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "retq" << std::endl;
  ssr << "nop" << std::endl;
  ssr << ".L_22:" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nopl %eax" << std::endl;
  ssr << "shrq $0xfd, %rax" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);

  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

}

TEST_F(BoundedValidatorBaseTest, WcslenWrong2) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rdi + x64asm::r15;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;
  validator->set_nacl(true);

  std::stringstream sst;
  sst << ".wcslen:" << std::endl; // BB 1
  sst << "movl %edi, %edi" << std::endl;
  sst << "xorl %eax, %eax" << std::endl;
  sst << "movl %edi, %edi" << std::endl;
  sst << "movl (%r15,%rdi,1), %ecx" << std::endl;
  sst << "movq %rdi, %rdx" << std::endl;
  sst << "testl %ecx, %ecx" << std::endl;
  sst << "je .L_142ce0" << std::endl;
  sst << "nop" << std::endl;
  sst << ".L_142cc0:" << std::endl;
  sst << "addl $0x4, %edx" << std::endl;
  sst << "movl %edx, %edx" << std::endl;
  sst << "movl (%r15,%rdx,1), %eax" << std::endl;
  sst << "testl %eax, %eax" << std::endl;
  sst << "jne .L_142cc0" << std::endl;
  sst << "movl %edx, %eax" << std::endl;
  sst << "subl %edi, %eax" << std::endl;
  sst << "sarl $0x2, %eax" << std::endl;
  sst << "nop" << std::endl;
  sst << ".L_142ce0:" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".wcslen:" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "movslq %edi, %rcx" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "movl %ecx, %eax" << std::endl;
  ssr << "andl (%r15,%rax,1), %edi" << std::endl;
  ssr << "je .L_22" << std::endl;
  ssr << ".L_10:" << std::endl;
  ssr << "subl $0xffffffc, %eax" << std::endl;
  ssr << "movl (%r15,%rax,1), %edx" << std::endl;
  ssr << "andq $0xfffffffe, %rdx" << std::endl;
  ssr << "jne .L_10" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "subl %ecx, %eax" << std::endl;
  ssr << "shrq $0x2, %rax" << std::endl;
  ssr << "retq" << std::endl;
  ssr << "nop" << std::endl;
  ssr << ".L_22:" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "sall $0xfb, %eax" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);


  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);


}

TEST_F(BoundedValidatorBaseTest, WcslenCorrect3) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rdi + x64asm::r15;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;
  validator->set_nacl(true);

  std::stringstream sst;
  sst << ".wcslen:" << std::endl; // BB 1
  sst << "movl %edi, %edi" << std::endl;
  sst << "xorl %eax, %eax" << std::endl;
  sst << "movl %edi, %edi" << std::endl;
  sst << "movl (%r15,%rdi,1), %ecx" << std::endl;
  sst << "movq %rdi, %rdx" << std::endl;
  sst << "testl %ecx, %ecx" << std::endl;
  sst << "je .L_142ce0" << std::endl;
  sst << "nop" << std::endl;
  sst << ".L_142cc0:" << std::endl;
  sst << "addl $0x4, %edx" << std::endl;
  sst << "movl %edx, %edx" << std::endl;
  sst << "movl (%r15,%rdx,1), %eax" << std::endl;
  sst << "testl %eax, %eax" << std::endl;
  sst << "jne .L_142cc0" << std::endl;
  sst << "movl %edx, %eax" << std::endl;
  sst << "subl %edi, %eax" << std::endl;
  sst << "sarl $0x2, %eax" << std::endl;
  sst << "nop" << std::endl;
  sst << ".L_142ce0:" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".wcslen:" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "movslq %edi, %rcx" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "movl %ecx, %eax" << std::endl;
  ssr << "movl (%r15,%rax,1), %edi" << std::endl;
  ssr << "testl %edi, %edi" << std::endl;
  ssr << "je .L_22" << std::endl;
  ssr << ".L_10:" << std::endl;
  //ssr << "subl $0xffffffc, %eax" << std::endl;
  ssr << "addl $0x4, %eax" << std::endl;
  ssr << "movl (%r15,%rax,1), %edx" << std::endl;
  //ssr << "andq $0xfffffffe, %rdx" << std::endl;
  ssr << "andq $0xffffffff, %rdx" << std::endl;
  ssr << "jne .L_10" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "subl %ecx, %eax" << std::endl;
  ssr << "shrq $0x2, %rax" << std::endl;
  ssr << "retq" << std::endl;
  ssr << "nop" << std::endl;
  ssr << ".L_22:" << std::endl;
  ssr << "nop" << std::endl;
  //ssr << "sall $0xfb, %eax" << std::endl;
  ssr << "xorl %eax, %eax" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);


  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_EQ(0ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_TRUE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_EQ(0ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);


}

TEST_F(BoundedValidatorBaseTest, WcslenWrong3) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rdi + x64asm::r15;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;
  validator->set_nacl(true);

  std::stringstream sst;
  sst << ".wcslen:" << std::endl; // BB 1
  sst << "movl %edi, %edi" << std::endl;
  sst << "xorl %eax, %eax" << std::endl;
  sst << "movl %edi, %edi" << std::endl;
  sst << "movl (%r15,%rdi,1), %ecx" << std::endl;
  sst << "movq %rdi, %rdx" << std::endl;
  sst << "testl %ecx, %ecx" << std::endl;
  sst << "je .L_142ce0" << std::endl;
  sst << "nop" << std::endl;
  sst << ".L_142cc0:" << std::endl;
  sst << "addl $0x4, %edx" << std::endl;
  sst << "movl %edx, %edx" << std::endl;
  sst << "movl (%r15,%rdx,1), %eax" << std::endl;
  sst << "testl %eax, %eax" << std::endl;
  sst << "jne .L_142cc0" << std::endl;
  sst << "movl %edx, %eax" << std::endl;
  sst << "subl %edi, %eax" << std::endl;
  sst << "sarl $0x2, %eax" << std::endl;
  sst << "nop" << std::endl;
  sst << ".L_142ce0:" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".wcslen:" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "movslq %edi, %rcx" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "movl %ecx, %eax" << std::endl;
  ssr << "movl (%r15,%rax,1), %edi" << std::endl;
  ssr << "testl %edi, %edi" << std::endl;
  ssr << "je .L_22" << std::endl;
  ssr << ".L_10:" << std::endl;
  ssr << "subl $0xffffffc, %eax" << std::endl;
  //ssr << "addl $0x4, %eax" << std::endl;
  ssr << "movl (%r15,%rax,1), %edx" << std::endl;
  //ssr << "andq $0xfffffffe, %rdx" << std::endl;
  ssr << "andq $0xffffffff, %rdx" << std::endl;
  ssr << "jne .L_10" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "subl %ecx, %eax" << std::endl;
  ssr << "shrq $0x2, %rax" << std::endl;
  ssr << "retq" << std::endl;
  ssr << "nop" << std::endl;
  ssr << ".L_22:" << std::endl;
  ssr << "nop" << std::endl;
  //ssr << "sall $0xfb, %eax" << std::endl;
  ssr << "xorl %eax, %eax" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);


  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);


}

TEST_F(BoundedValidatorBaseTest, WcslenWrong4) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rdi + x64asm::r15;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;
  validator->set_nacl(true);

  std::stringstream sst;
  sst << ".wcslen:" << std::endl; // BB 1
  sst << "movl %edi, %edi" << std::endl;
  sst << "xorl %eax, %eax" << std::endl;
  sst << "movl %edi, %edi" << std::endl;
  sst << "movl (%r15,%rdi,1), %ecx" << std::endl;
  sst << "movq %rdi, %rdx" << std::endl;
  sst << "testl %ecx, %ecx" << std::endl;
  sst << "je .L_142ce0" << std::endl;
  sst << "nop" << std::endl;
  sst << ".L_142cc0:" << std::endl;
  sst << "addl $0x4, %edx" << std::endl;
  sst << "movl %edx, %edx" << std::endl;
  sst << "movl (%r15,%rdx,1), %eax" << std::endl;
  sst << "testl %eax, %eax" << std::endl;
  sst << "jne .L_142cc0" << std::endl;
  sst << "movl %edx, %eax" << std::endl;
  sst << "subl %edi, %eax" << std::endl;
  sst << "sarl $0x2, %eax" << std::endl;
  sst << "nop" << std::endl;
  sst << ".L_142ce0:" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".wcslen:" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "movslq %edi, %rcx" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "movl %ecx, %eax" << std::endl;
  ssr << "movl (%r15,%rax,1), %edi" << std::endl;
  ssr << "testl %edi, %edi" << std::endl;
  ssr << "je .L_22" << std::endl;
  ssr << ".L_10:" << std::endl;
  //ssr << "subl $0xffffffc, %eax" << std::endl;
  ssr << "addl $0x4, %eax" << std::endl;
  ssr << "movl (%r15,%rax,1), %edx" << std::endl;
  ssr << "andq $0xfffffffe, %rdx" << std::endl;
  //ssr << "andq $0xffffffff, %rdx" << std::endl;
  ssr << "jne .L_10" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "subl %ecx, %eax" << std::endl;
  ssr << "shrq $0x2, %rax" << std::endl;
  ssr << "retq" << std::endl;
  ssr << "nop" << std::endl;
  ssr << ".L_22:" << std::endl;
  ssr << "nop" << std::endl;
  //ssr << "sall $0xfb, %eax" << std::endl;
  ssr << "xorl %eax, %eax" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);


  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);


}

TEST_F(BoundedValidatorBaseTest, WcslenWrong5) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rdi + x64asm::r15;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;
  validator->set_nacl(true);

  std::stringstream sst;
  sst << ".wcslen:" << std::endl; // BB 1
  sst << "movl %edi, %edi" << std::endl;
  sst << "xorl %eax, %eax" << std::endl;
  sst << "movl %edi, %edi" << std::endl;
  sst << "movl (%r15,%rdi,1), %ecx" << std::endl;
  sst << "movq %rdi, %rdx" << std::endl;
  sst << "testl %ecx, %ecx" << std::endl;
  sst << "je .L_142ce0" << std::endl;
  sst << "nop" << std::endl;
  sst << ".L_142cc0:" << std::endl;
  sst << "addl $0x4, %edx" << std::endl;
  sst << "movl %edx, %edx" << std::endl;
  sst << "movl (%r15,%rdx,1), %eax" << std::endl;
  sst << "testl %eax, %eax" << std::endl;
  sst << "jne .L_142cc0" << std::endl;
  sst << "movl %edx, %eax" << std::endl;
  sst << "subl %edi, %eax" << std::endl;
  sst << "sarl $0x2, %eax" << std::endl;
  sst << "nop" << std::endl;
  sst << ".L_142ce0:" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".wcslen:" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "movslq %edi, %rcx" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "movl %ecx, %eax" << std::endl;
  ssr << "movl (%r15,%rax,1), %edi" << std::endl;
  ssr << "testl %edi, %edi" << std::endl;
  ssr << "je .L_22" << std::endl;
  ssr << ".L_10:" << std::endl;
  //ssr << "subl $0xffffffc, %eax" << std::endl;
  ssr << "addl $0x4, %eax" << std::endl;
  ssr << "movl (%r15,%rax,1), %edx" << std::endl;
  //ssr << "andq $0xfffffffe, %rdx" << std::endl;
  ssr << "andq $0xffffffff, %rdx" << std::endl;
  ssr << "jne .L_10" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "subl %ecx, %eax" << std::endl;
  ssr << "shrq $0x2, %rax" << std::endl;
  ssr << "retq" << std::endl;
  ssr << "nop" << std::endl;
  ssr << ".L_22:" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "sall $0xfb, %eax" << std::endl;
  //ssr << "xorl %eax, %eax" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);


  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);


}

TEST_F(BoundedValidatorBaseTest, WcscpyWrong1) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rdi + x64asm::rsi + x64asm::r15;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;
  validator->set_nacl(true);

  std::stringstream sst;
  sst << ".wcscpy:" << std::endl;
  sst << "movl %edi, %eax" << std::endl;
  sst << "movl %esi, %esi" << std::endl;
  sst << "movl %eax, %eax" << std::endl;
  sst << "movl $0x0, (%r15,%rax,1)" << std::endl;
  sst << "movl %esi, %esi" << std::endl;
  sst << "movl (%r15,%rsi,1), %ecx" << std::endl;
  sst << "movq %rax, %rdx" << std::endl;
  sst << "testl %ecx, %ecx" << std::endl;
  sst << "je .L_140f20" << std::endl;
  sst << "nop" << std::endl;
  sst << ".L_140f00:" << std::endl;
  sst << "addl $0x4, %esi" << std::endl;
  sst << "movl %edx, %edx" << std::endl;
  sst << "movl %ecx, (%r15,%rdx,1)" << std::endl;
  sst << "addl $0x4, %edx" << std::endl;
  sst << "movl %esi, %esi" << std::endl;
  sst << "movl (%r15,%rsi,1), %ecx" << std::endl;
  sst << "testl %ecx, %ecx" << std::endl;
  sst << "jne .L_140f00" << std::endl;
  sst << "nop" << std::endl;
  sst << ".L_140f20:" << std::endl;
  sst << "movl %edx, %edx" << std::endl;
  sst << "movl $0x0, (%r15,%rdx,1)" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  sst << ".wcscpy:" << std::endl;
  sst << "movl %esi, %edx" << std::endl;
  sst << "movl (%r15,%rdx,1), %ecx" << std::endl;
  sst << "movq %rdi, %rax" << std::endl;
  sst << "testl %edx, %ecx" << std::endl;
  sst << "nop" << std::endl;
  sst << "movw %ax, %dx" << std::endl;
  sst << "je .L_140f20" << std::endl;
  sst << "nop" << std::endl;
  sst << ".L_140f00:" << std::endl;
  sst << "orl %esp, %edx" << std::endl;
  sst << "movq %rcx, (%r15,%rdx,1)" << std::endl;
  sst << "addl $0x4, %esi" << std::endl;
  sst << "movl (%r15,%rsi,1), %ecx" << std::endl;
  sst << "addl $0x4, %edx" << std::endl;
  sst << "testl %ecx, %ecx" << std::endl;
  sst << "jne .L_140f00" << std::endl;
  sst << "nop" << std::endl;
  sst << ".L_140f20:" << std::endl;
  sst << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);


  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_LE(1ul, validator->counter_examples_available());
  for (auto it : validator->get_counter_examples())
    check_ceg(it, target, rewrite);


}


/*  // TODO: this can be a test for the obligation checker
TEST_F(BoundedValidatorBaseTest, WcpcpyA) {

  auto def_ins = x64asm::RegSet::empty() + x64asm::rsi + x64asm::rdi + x64asm::r15 + x64asm::rax;
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "movl %edi, %edi" << std::endl;
  sst << "movl %esi, %esi" << std::endl;
  sst << "nop" << std::endl;
  sst << "nop" << std::endl;
  sst << ".L_top:" << std::endl;
  sst << "movl %esi, %esi" << std::endl;
  sst << "movl (%r15,%rsi,1), %edx" << std::endl;
  sst << "movq %rdi, %rax" << std::endl;
  sst << "addl $0x4, %esi" << std::endl;
  sst << "movl %edi, %edi" << std::endl;
  sst << "movl %edx, (%r15, %rdi, 1)" << std::endl;
  sst << "addl $0x4, %edi" << std::endl;
  sst << "testl %edx, %edx" << std::endl;
  sst << "jne .L_top" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << ".L_top:" << std::endl;
  ssr << "addl $0x4, %esi" << std::endl;
  ssr << "movl -0x4(%r15,%rsi,1), %edx" << std::endl;
  ssr << "movl %edi, %eax" << std::endl;
  ssr << "addl $0x4, %edi" << std::endl;
  ssr << "movl %edx, -0x4(%r15,%rdi,1)" << std::endl;
  ssr << "testl %edx, %edx" << std::endl;
  ssr << "jne .L_top" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);

  // rax = rax'
  std::map<std::pair<x64asm::R, bool>, long> t1;
  std::map<std::pair<x64asm::R, bool>, long> r1;
  t1[std::pair<x64asm::R, bool>(x64asm::rax, false)] = 1;
  r1[std::pair<x64asm::R, bool>(x64asm::rax, false)] = -1;
  EqualityInvariant inv_rax(t1, r1, 0);

  // edi = edi'
  std::map<std::pair<x64asm::R, bool>, long> t2;
  std::map<std::pair<x64asm::R, bool>, long> r2;
  t2[std::pair<x64asm::R, bool>(x64asm::edi, false)] = 1;
  r2[std::pair<x64asm::R, bool>(x64asm::edi, false)] = -1;
  EqualityInvariant inv_rdi(t2, r2, 0);

  // top 32 bits of rdi are 0.
  TopZeroInvariant target_edi_0(x64asm::rdi, false);

  // esi = esi'
  std::map<std::pair<x64asm::R, bool>, long> t3;
  std::map<std::pair<x64asm::R, bool>, long> r3;
  t3[std::pair<x64asm::R, bool>(x64asm::esi, false)] = 1;
  r3[std::pair<x64asm::R, bool>(x64asm::esi, false)] = -1;
  EqualityInvariant inv_rsi(t3, r3, 0);

  // r15 = r15'
  std::map<std::pair<x64asm::R, bool>, long> t4;
  std::map<std::pair<x64asm::R, bool>, long> r4;
  t4[std::pair<x64asm::R, bool>(x64asm::r15, false)] = 1;
  r4[std::pair<x64asm::R, bool>(x64asm::r15, false)] = -1;
  EqualityInvariant inv_r15(t4, r4, 0);

  // no signals
  NoSignalsInvariant no_signals;

  // conjunction
  ConjunctionInvariant inv_all;
  inv_all.add_invariant(&inv_rax);
  inv_all.add_invariant(&inv_rdi);
  inv_all.add_invariant(&inv_rsi);
  inv_all.add_invariant(&inv_r15);
  inv_all.add_invariant(&target_edi_0);
  inv_all.add_invariant(&no_signals);

  StateEqualityInvariant start_same(def_ins);
  StateEqualityInvariant exit_same(live_outs);

  ConjunctionInvariant inv_entry;
  inv_entry.add_invariant(&no_signals);
  inv_entry.add_invariant(&start_same);

  ConjunctionInvariant inv_exit;
  inv_exit.add_invariant(&no_signals);
  inv_exit.add_invariant(&exit_same);

  auto path = CfgPaths::enumerate_paths(target, 1)[0];

  std::vector<Cfg::id_type> top_segment;
  top_segment.push_back(path[0]);
  top_segment.push_back(path[1]);

  std::vector<Cfg::id_type> middle_segment;
  middle_segment.push_back(path[1]);

  std::vector<Cfg::id_type> end_segment;
  end_segment.push_back(path[2]);

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::STRING_NO_ALIAS);
  EXPECT_TRUE(validator->verify_pair(target, rewrite, top_segment, top_segment, inv_entry, inv_all));
  EXPECT_TRUE(validator->verify_pair(target, rewrite, middle_segment, middle_segment, inv_all, inv_all));
  EXPECT_TRUE(validator->verify_pair(target, rewrite, end_segment, end_segment, inv_all, inv_exit));

  for (auto it : validator->get_counter_examples()) {
    std::cout << it << std::endl;
  }

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_TRUE(validator->verify_pair(target, rewrite, top_segment, top_segment, inv_entry, inv_all));
  EXPECT_TRUE(validator->verify_pair(target, rewrite, middle_segment, middle_segment, inv_all, inv_all));
  EXPECT_TRUE(validator->verify_pair(target, rewrite, end_segment, end_segment, inv_all, inv_exit));

  for (auto it : validator->get_counter_examples()) {
    std::cout << it << std::endl;
  }
}
*/

TEST_F(BoundedValidatorBaseTest, NoSpuriousCeg) {

  auto def_ins = x64asm::RegSet::empty();
  auto live_outs = x64asm::RegSet::empty() + x64asm::rax;

  // These won't validate because def_ins are empty, but it can't get a
  // counterexample either.

  std::stringstream sst;
  sst << ".foo:" << std::endl;
  sst << "retq" << std::endl;
  auto target = make_cfg(sst, def_ins, live_outs);

  std::stringstream ssr;
  ssr << ".foo:" << std::endl;
  ssr << "nop" << std::endl;
  ssr << "retq" << std::endl;
  auto rewrite = make_cfg(ssr, def_ins, live_outs);

  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_EQ(0ul, validator->counter_examples_available());

  validator->set_alias_strategy(BoundedValidator::AliasStrategy::FLAT);
  EXPECT_FALSE(validator->verify(target, rewrite));
  EXPECT_FALSE(validator->has_error()) << validator->error();
  EXPECT_EQ(0ul, validator->counter_examples_available());

}


} //namespace stoke
