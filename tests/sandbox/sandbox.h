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


#include "src/ext/x64asm/src/reg_set.h"
#include "src/state/cpu_state.h"
#include "src/stategen/stategen.h"
#include "src/sandbox/sandbox.h"

namespace stoke {

TEST(SandboxTest, TrivialExampleWorks) {

  x64asm::Code c;
  std::stringstream ss;

  // Here's the input program
  ss << ".foo:" << std::endl;
  ss << "incq %rcx" << std::endl;
  ss << "addq $0x8, %rdx" << std::endl;
  ss << "retq" << std::endl;

  ss >> c;

  // Setup the sandbox
  Sandbox sb;
  CpuState tc;

  sb.set_max_jumps(1);
  sb.insert_input(tc);

  // Run it
  sb.run(Cfg(TUnit(c)));

  ASSERT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);

  CpuState output = *sb.result_begin();

  ASSERT_EQ((uint64_t)1, output.gp[1].get_fixed_quad(0));
  ASSERT_EQ((uint64_t)8, output.gp[2].get_fixed_quad(0));
}

TEST(SandboxTest, AllGPRegistersWork) {

  x64asm::Code c;
  std::stringstream ss;

  // Here's the input program
  ss << ".foo:" << std::endl;
  ss << "addq $0x1, %rax" << std::endl;
  ss << "addq $0x2, %rcx" << std::endl;
  ss << "addq $0x3, %rdx" << std::endl;
  ss << "addq $0x4, %rbx" << std::endl;
  ss << "addq $0x5, %rsp" << std::endl;
  ss << "addq $0x6, %rbp" << std::endl;
  ss << "addq $0x7, %rsi" << std::endl;
  ss << "addq $0x8, %rdi" << std::endl;
  ss << "addq $0x9, %r8" << std::endl;
  ss << "addq $0xa, %r9" << std::endl;
  ss << "addq $0xb, %r10" << std::endl;
  ss << "addq $0xc, %r11" << std::endl;
  ss << "addq $0xd, %r12" << std::endl;
  ss << "addq $0xe, %r13" << std::endl;
  ss << "addq $0xf, %r14" << std::endl;
  ss << "addq $0x10, %r15" << std::endl;
  ss << "retq" << std::endl;

  ss >> c;

  // Setup the sandbox
  Sandbox sb;
  sb.set_abi_check(false);
  CpuState tc;

  sb.set_max_jumps(1);
  sb.insert_input(tc);

  // Run it
  sb.run(Cfg(TUnit(c)));

  ASSERT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);

  CpuState output = *sb.result_begin();

  for (uint64_t i = 0; i < 16; ++i) {
    ASSERT_EQ(1 + i, output.gp[i].get_fixed_quad(0));
  }


}


TEST(SandboxTest, RegisterValuesArePreserved) {

  x64asm::Code c;
  std::stringstream ss;

  // Here's the input program
  ss << ".foo:" << std::endl;
  ss << "retq" << std::endl;

  ss >> c;

  // Setup the sandbox
  Sandbox sb;
  sb.set_abi_check(false);

  CpuState tc;
  StateGen sg(&sb);
  sg.get(tc);

  sb.set_max_jumps(1);
  sb.insert_input(tc);

  // Run it
  sb.run(Cfg(TUnit(c)));

  ASSERT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);

  CpuState output = *sb.result_begin();

  for (int i = 0; i < 16; ++i) {
    ASSERT_EQ(tc.gp[i].get_fixed_quad(0), output.gp[i].get_fixed_quad(0)) << " i = " << i;
  }


}





/* This test exists because the Sandbox used to throw a
   segfault if the code violates the ABI */
TEST(SandboxTest, ModifyingRbxWorks) {

  x64asm::Code c;
  std::stringstream ss;

  // Here's the input program
  ss << ".foo:" << std::endl;
  ss << "incq %rcx" << std::endl;
  ss << "addq $0x8, %rbx" << std::endl;
  ss << "retq" << std::endl;

  ss >> c;

  // Setup the sandbox
  Sandbox sb;
  sb.set_abi_check(false);

  CpuState tc;

  sb.set_max_jumps(1);
  sb.insert_input(tc);

  // Run it
  sb.run(Cfg(TUnit(c)));

  ASSERT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);

  CpuState output = *sb.result_begin();

  ASSERT_EQ((uint64_t)1, output.gp[1].get_fixed_quad(0));
  ASSERT_EQ((uint64_t)8, output.gp[3].get_fixed_quad(0));


}


TEST(SandboxTest, ModifyingRbxFailsIfAbiEnforced) {

  x64asm::Code c;
  std::stringstream ss;

  // Here's the input program
  ss << ".foo:" << std::endl;
  ss << "incq %rcx" << std::endl;
  ss << "addq $0x8, %rbx" << std::endl;
  ss << "retq" << std::endl;

  ss >> c;

  // Setup the sandbox; it's going to check abi.
  Sandbox sb;

  CpuState tc;

  sb.set_max_jumps(1);
  sb.insert_input(tc);

  // Run it
  sb.run(Cfg(TUnit(c)));

  ASSERT_EQ(ErrorCode::SIGCUSTOM_ABI_VIOLATION, sb.result_begin()->code);
}


TEST(SandboxTest, RflagsRegistersArePreserved) {

  x64asm::Code c;
  std::stringstream ss;

  // Here's the input program
  ss << ".foo:" << std::endl;
  ss << "cmovzl %ebp, %esp" << std::endl;
  ss << "retq" << std::endl;
  ss >> c;

  // Setup the sandbox and generate a random state
  Sandbox sb;

  CpuState tc;
  tc.rf.set(7, 1);
  //set rsp
  tc.gp[4].get_fixed_quad(0) = 0xfffaffe4;
  //set rbp
  tc.gp[5].get_fixed_quad(0) = 0x05001b;

  sb.set_max_jumps(2)
  .set_abi_check(false)
  .insert_input(tc);

  // Run it
  sb.run(Cfg(TUnit(c), x64asm::RegSet::universe(), x64asm::RegSet::universe()));

  // Check answers
  CpuState result = *sb.result_begin();
  EXPECT_EQ(tc.rf.is_set(0), result.rf.is_set(0));
  EXPECT_EQ(tc.rf.is_set(2), result.rf.is_set(2));
  EXPECT_EQ(tc.rf.is_set(4), result.rf.is_set(4));
  EXPECT_EQ(tc.rf.is_set(6), result.rf.is_set(6));
  EXPECT_EQ(tc.rf.is_set(7), result.rf.is_set(7));
  EXPECT_EQ(tc.rf.is_set(11), result.rf.is_set(11));

}


TEST(SandboxTest, NullDereferenceFails) {

  x64asm::Code c;
  std::stringstream ss;

  // Here's the input program
  ss << ".foo:" << std::endl;
  ss << "incq %rcx" << std::endl;
  ss << "movq (%rax), %rcx" << std::endl;
  ss << "addq $0x8, %rdx" << std::endl;
  ss << "retq" << std::endl;

  ss >> c;

  // Setup the sandbox
  Sandbox sb;
  CpuState tc;

  sb.set_max_jumps(1);
  sb.insert_input(tc);

  // Run it
  sb.run(Cfg(TUnit(c)));

  ASSERT_EQ(ErrorCode::SIGSEGV_, sb.result_begin()->code);


}


TEST(SandboxTest, DivideByZeroFails) {

  x64asm::Code c;
  std::stringstream ss;

  // Here's the input program
  ss << ".foo:" << std::endl;
  ss << "divq %rcx" << std::endl;
  ss << "addq $0x8, %rdx" << std::endl;
  ss << "retq" << std::endl;

  ss >> c;

  // Setup the sandbox
  Sandbox sb;
  CpuState tc;

  sb.set_max_jumps(1);
  sb.insert_input(tc);

  // Run it
  sb.run(Cfg(TUnit(c)));

  ASSERT_EQ(ErrorCode::SIGFPE_, sb.result_begin()->code);
}

TEST(SandboxTest, InfiniteLoopFails) {

  x64asm::Code c;
  std::stringstream ss;

  // Here's the input program
  ss << ".foo:" << std::endl;
  ss << "xorq %rcx, %rcx" << std::endl;
  ss << ".L1:" << std::endl;
  ss << "incq %rcx" << std::endl;
  ss << "jmpq .L1" << std::endl;
  ss << "retq" << std::endl;

  ss >> c;

  // Setup the sandbox
  Sandbox sb;
  CpuState tc;
  StateGen sg(&sb);
  sg.get(tc);

  sb.set_max_jumps(100);
  sb.insert_input(tc);

  // Run it
  sb.run(Cfg(TUnit(c)));

  ASSERT_EQ(ErrorCode::SIGCUSTOM_EXCEEDED_MAX_JUMPS, sb.result_begin()->code);


}



TEST(SandboxTest, ShortLoopMaxIterationsOk) {

  x64asm::Code c;
  std::stringstream ss;

  // Here's the input program
  ss << ".foo:" << std::endl;
  ss << "xorq %rcx, %rcx" << std::endl;
  ss << ".L1:" << std::endl;
  ss << "incq %rcx" << std::endl;
  ss << "cmpq $0x10, %rcx" << std::endl;
  ss << "jne .L1" << std::endl;
  ss << "retq" << std::endl;

  ss >> c;

  // Setup the sandbox
  Sandbox sb;
  CpuState tc;
  StateGen sg(&sb);
  sg.get(tc);

  sb.set_max_jumps(17);
  sb.insert_input(tc);
  sb.set_abi_check(false);

  // Run it
  sb.run(Cfg(TUnit(c)));

  ASSERT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);


}

TEST(SandboxTest, ShortLoopOneTooManyIterations) {

  x64asm::Code c;
  std::stringstream ss;

  // Here's the input program
  ss << ".foo:" << std::endl;
  ss << "xorq %rcx, %rcx" << std::endl;
  ss << ".L1:" << std::endl;
  ss << "incq %rcx" << std::endl;
  ss << "cmpq $0x10, %rcx" << std::endl;
  ss << "jne .L1" << std::endl;
  ss << "retq" << std::endl;

  ss >> c;

  // Setup the sandbox
  Sandbox sb;
  CpuState tc;
  StateGen sg(&sb);
  sg.get(tc);

  sb.set_max_jumps(16);
  sb.insert_input(tc);
  sb.set_abi_check(false);

  // Run it
  sb.run(Cfg(TUnit(c)));

  ASSERT_EQ(ErrorCode::SIGCUSTOM_EXCEEDED_MAX_JUMPS, sb.result_begin()->code);


}

TEST(SandboxTest, LahfSahfOkay) {
  x64asm::Code c;
  std::stringstream ss;

  // Here's the input program
  ss << ".foo:" << std::endl;
  ss << "xorq %rax, %rax" << std::endl;
  ss << "lahf" << std::endl;
  ss << "sahf" << std::endl;
  ss << "retq" << std::endl;

  ss >> c;

  // Setup the sandbox
  Sandbox sb;
  CpuState tc;
  StateGen sg(&sb);
  sg.get(tc);

  sb.insert_input(tc);

  // Run it
  sb.run(Cfg(TUnit(c)));
  ASSERT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);
}

TEST(SandboxTest, UndefSymbolError) {
  x64asm::Code c;
  std::stringstream ss;

  // Here's the input program
  ss << ".foo:" << std::endl;
  ss << "xorq %rax, %rax" << std::endl;
  ss << "callq .no_target" << std::endl;
  ss << "retq" << std::endl;

  ss >> c;

  // Setup the sandbox
  Sandbox sb;
  CpuState tc;
  sb.insert_input(tc);

  // Run it (we shouldn't ever actually run, so testcase doesn't matter)
  sb.run(Cfg(TUnit(c)));
  ASSERT_EQ(ErrorCode::SIGCUSTOM_LINKER_ERROR, sb.result_begin()->code);
}

TEST(SandboxTest, Issue239) {
  x64asm::Code c;
  std::stringstream ss;

  // Here's the input program
  ss << ".foo:" << std::endl;
  ss << "movl $0x3300, %esp" << std::endl;
  ss << "movl $0x81d1, %r14d" << std::endl;
  ss << "leaw 0x40(%rsp,%r14,1), %bx" << std::endl;
  ss << "retq" << std::endl;

  ss >> c;

  // Setup the sandbox
  Sandbox sb;
  CpuState tc;
  sb.set_abi_check(false);
  sb.insert_input(tc);

  // Run it (we shouldn't ever actually run, so testcase doesn't matter)
  sb.run(Cfg(TUnit(c), x64asm::RegSet::empty(), x64asm::RegSet::empty() + x64asm::bx));
  ASSERT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);

  CpuState output = *sb.result_begin();

  ASSERT_EQ((uint64_t)0xb511, output.gp[x64asm::rbx].get_fixed_quad(0));
}

TEST(SandboxTest, LDDQU_VLDDQU) {
#ifdef __AVX2__
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "lddqu -0x21(%rsp), %xmm0" << std::endl;
  ss << "vlddqu -0x21(%rsp), %ymm0" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);
  // @todo Why does this test require 64 bytes of stack to begin
  // with? Can't we automatically provide that?
  StateGen sg(&sb, 64);
  sg.get(tc);
  sb.insert_input(tc);

  sb.run(Cfg(TUnit(c)));
  ASSERT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);
#endif
}

TEST(SandboxTest, PUSH_POP) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "pushw -0x18(%rsp)" << std::endl;
  ss << "pushw -0x18(%rsp)" << std::endl;
  ss << "pushw %ax" << std::endl;
  ss << "pushw %ax" << std::endl;
  ss << "pushq -0x18(%rsp)" << std::endl;
  ss << "pushq %rax" << std::endl;
  ss << "pushq $0xffffffaa" << std::endl;
  ss << "popq  %rax" << std::endl;
  ss << "popq  %rax" << std::endl;
  ss << "popq  %rax" << std::endl;
  ss << "popq  %rax" << std::endl;
  ss << "pushq $0xffffffaa" << std::endl;
  ss << "pushq $0xffffbbbb" << std::endl;
  ss << "pushq $0xcccccccc" << std::endl;
  ss << "popq %rax" << std::endl;
  ss << "popw %ax" << std::endl;
  ss << "popq (%rsp)" << std::endl;
  ss << "popw (%rsp)" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);
  StateGen sg(&sb);
  sg.get(tc);               // this has 32 bytes of space on the stack only
  sb.insert_input(tc);

  sb.run(Cfg(TUnit(c)));
  ASSERT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);
}

TEST(SandboxTest, PushImm16SignExtend) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;

  // put garbage on the stack
  ss << "pushw $0xaaaa" << std::endl;
  ss << "pushw $0xaaaa" << std::endl;
  ss << "pushw $0xaaaa" << std::endl;
  ss << "pushw $0xaaaa" << std::endl;

  // this should push 0xffffffffffffc0de
  ss << "pushq $0xc0de #OPC=pushq_imm16" << std::endl;

  // make sure the right number of bytes were pushed
  ss << "popq %rax" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);
  StateGen sg(&sb);
  sg.get(tc);
  sb.insert_input(tc);

  sb.run(Cfg(TUnit(c)));
  auto result = *sb.result_begin();

  EXPECT_EQ(ErrorCode::NORMAL, result.code);
  EXPECT_EQ(0xffffffffffffc0de, result[rax]);

}

TEST(SandboxTest, PushImm32ZeroExtend) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;

  // put garbage on the stack
  ss << "pushw $0xaaaa" << std::endl;
  ss << "pushw $0xaaaa" << std::endl;
  ss << "pushw $0xaaaa" << std::endl;
  ss << "pushw $0xaaaa" << std::endl;

  // this should push 0xc0de
  ss << "pushq $0xc0de #OPC=pushq_imm32" << std::endl;

  // make sure the right number of bytes were pushed
  ss << "popq %rax" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);
  StateGen sg(&sb);
  sg.get(tc);
  sb.insert_input(tc);

  sb.run(Cfg(TUnit(c)));
  auto result = *sb.result_begin();

  EXPECT_EQ(ErrorCode::NORMAL, result.code);
  EXPECT_EQ(0xc0deul, result[rax]);

}

TEST(SandboxTest, PushImm32SignExtend) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;

  // put garbage on the stack
  ss << "pushw $0xaaaa" << std::endl;
  ss << "pushw $0xaaaa" << std::endl;
  ss << "pushw $0xaaaa" << std::endl;
  ss << "pushw $0xaaaa" << std::endl;

  // this should push 0xffffffffc0def00d
  ss << "pushq $0xc0def00d" << std::endl;

  // make sure the right number of bytes were pushed
  ss << "popq %rax" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);
  StateGen sg(&sb);
  sg.get(tc);
  sb.insert_input(tc);

  sb.run(Cfg(TUnit(c)));
  auto result = *sb.result_begin();

  EXPECT_EQ(ErrorCode::NORMAL, result.code);
  EXPECT_EQ(0xffffffffc0def00d, result[rax]);

}

TEST(SandboxTest, MEM_DIV) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "movq $0x1, %rax" << std::endl;
  ss << "movq $0x1, %rdx" << std::endl;
  ss << "movq $0x20, -0x8(%rsp)" << std::endl;
  ss << "divb -0x8(%rsp)" << std::endl;
  ss << "divw -0x8(%rsp)" << std::endl;
  ss << "divl -0x8(%rsp)" << std::endl;
  ss << "divq -0x8(%rsp)" << std::endl;
  ss << "idivb -0x8(%rsp)" << std::endl;
  ss << "idivw -0x8(%rsp)" << std::endl;
  ss << "idivl -0x8(%rsp)" << std::endl;
  ss << "movq $0x0, %rdx" << std::endl;
  ss << "movq $0x20, %rax" << std::endl;
  ss << "idivq -0x8(%rsp)" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);
  StateGen sg(&sb);
  sg.get(tc);
  sb.insert_input(tc);

  sb.run(Cfg(TUnit(c)));
  ASSERT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);
}

TEST(SandboxTest, RSP_WITH_JMPS) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "pushq %rbp" << std::endl;
  ss << "movq %rsp, %rbp" << std::endl;
  ss << "movl $0x0, -0x18(%rbp)" << std::endl;
  ss << "cmpl $0x0, -0x18(%rbp)" << std::endl;
  ss << "je .L_4006f9" << std::endl;
  ss << ".L_4006f9:" << std::endl;
  ss << "popq %rbp" << std::endl;
  ss << "xorq %rax, %rax" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);
  StateGen sg(&sb);
  sg.get(tc);
  sb.insert_input(tc);

  sb.run(Cfg(TUnit(c)));
  ASSERT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);
}

TEST(SandboxTest, PushfWorks) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "pushf" << std::endl;
  ss << "pushfq" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);
  StateGen sg(&sb);
  sg.get(tc);
  sb.insert_input(tc);

  sb.run(Cfg(TUnit(c)));
  ASSERT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);
}

TEST(SandboxTest, PopfFailCase) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "movq $-0x1, %rax" << std::endl;
  ss << "pushq %rax" << std::endl;
  ss << "popf" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);
  StateGen sg(&sb);
  sg.get(tc);
  sb.insert_input(tc);

  sb.run(Cfg(TUnit(c)));
  ASSERT_EQ(ErrorCode::SIGCUSTOM_INVALID_POPF, sb.result_begin()->code);
}

TEST(SandboxTest, PopfqFailCase) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "movq $-0x1, %rax" << std::endl;
  ss << "pushq %rax" << std::endl;
  ss << "popfq" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);
  StateGen sg(&sb);
  sg.get(tc);
  sb.insert_input(tc);

  sb.run(Cfg(TUnit(c)));
  ASSERT_EQ(ErrorCode::SIGCUSTOM_INVALID_POPF, sb.result_begin()->code);
}

TEST(SandboxTest, PopfqWorksCase) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "pushfq" << std::endl;
  ss << "movl $0x000008d5, %edi" << std::endl;
  ss << "popq %rax" << std::endl;
  ss << "orq %rax, %rdi" << std::endl;
  ss << "pushq %rdi" << std::endl;
  ss << "popfq" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);
  StateGen sg(&sb);
  sg.get(tc);
  sb.insert_input(tc);

  sb.run(Cfg(TUnit(c)));
  ASSERT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);
}

TEST(SandboxTest, fld_family) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "flds -0x20(%rsp)" << std::endl;
  ss << "fldl -0x20(%rsp)" << std::endl;
  ss << "fldt -0x20(%rsp)" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);
  StateGen sg(&sb);
  sg.get(tc);
  sb.insert_input(tc);

  sb.run(Cfg(TUnit(c)));
  ASSERT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);
}

TEST(SandboxTest, PUSH_POP2) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "pushq %rax" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);
  StateGen sg(&sb);
  sg.get(tc);
  sb.insert_input(tc);

  sb.run(Cfg(TUnit(c)));
  ASSERT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);
}



TEST(SandboxTest, Issue633) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "pushq %rax" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;
  auto cfg = Cfg(TUnit(c));

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);
  StateGen sg(&sb);
  EXPECT_TRUE(sg.get(tc, cfg)) << sg.get_error();
  sb.insert_input(tc);

  sb.run(cfg);
  ASSERT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);


}

TEST(SandboxTest, Issue709_1) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "movl (%rdi), %ecx" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;
  auto cfg = Cfg(TUnit(c));

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);

  uint64_t base = 0x7ffffffffffffffc;
  tc.gp[x64asm::rdi].get_fixed_quad(0) =  base;
  tc.heap.resize(base, 0x8);
  for (uint64_t i = base; i < base + 4; ++i) {
    tc.heap.set_valid(i, true);
    tc.heap[i] = 0x10;
  }

  sb.insert_input(tc);
  sb.run(cfg);
  EXPECT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);
  EXPECT_EQ(0x10101010ul, (*sb.result_begin())[x64asm::ecx]);

}

TEST(SandboxTest, Issue709_2) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "movl (%rdi), %ecx" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;
  auto cfg = Cfg(TUnit(c));

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);

  uint64_t base = 0xfffffffffffffffc;
  tc.gp[x64asm::rdi].get_fixed_quad(0) =  base;
  tc.heap.resize(base, 0x4);
  for (uint64_t i = 0; i < 4; ++i) {
    tc.heap.set_valid(base + i, true);
    tc.heap[base+i] = 0x10;
  }
  ASSERT_TRUE(tc.heap.is_valid(0xffffffffffffffff));

  sb.insert_input(tc);
  sb.run(cfg);
  EXPECT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);
  EXPECT_EQ(0x10101010ul, (*sb.result_begin())[x64asm::ecx]);
}

TEST(SandboxTest, Issue709_3) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "movl (%rdi), %ecx" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;
  auto cfg = Cfg(TUnit(c));

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);

  uint64_t base = 0x0;
  tc.gp[x64asm::rdi].get_fixed_quad(0) =  base;
  tc.heap.resize(base, 0x4);
  for (uint64_t i = base; i < base + 4; ++i) {
    tc.heap.set_valid(i, true);
    tc.heap[i] = 0x10;
  }

  sb.insert_input(tc);
  sb.run(cfg);
  EXPECT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);
  EXPECT_EQ(0x10101010ul, (*sb.result_begin())[x64asm::ecx]);
}

TEST(SandboxTest, Issue709_4) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "movq (%rdi), %rcx" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;
  auto cfg = Cfg(TUnit(c));

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);

  uint64_t base = 0x7ffffffffffffffc;
  tc.gp[x64asm::rdi].get_fixed_quad(0) =  base;
  tc.heap.resize(base, 0x8);
  for (uint64_t i = base; i < base + 8; ++i) {
    tc.heap.set_valid(i, true);
    tc.heap[i] = 0x10;
  }

  sb.insert_input(tc);
  sb.run(cfg);
  EXPECT_EQ(ErrorCode::NORMAL, sb.result_begin()->code);
  EXPECT_EQ(0x1010101010101010ul, (*sb.result_begin())[x64asm::rcx]);

}

TEST(SandboxTest, Issue709_5) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "movq (%rdi), %rcx" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;
  auto cfg = Cfg(TUnit(c));

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);

  uint64_t base = 0xfffffffffffffffc;
  tc.gp[x64asm::rdi].get_fixed_quad(0) =  base;
  tc.heap.resize(base, 0x4);
  for (uint64_t i = base; i < base + 4; ++i) {
    tc.heap.set_valid(i, true);
    tc.heap[i] = 0x10;
  }

  sb.insert_input(tc);
  sb.run(cfg);
  EXPECT_EQ(ErrorCode::SIGSEGV_, sb.result_begin()->code);

}

TEST(SandboxTest, LeaRip) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "leaq (%rip), %rax" << std::endl;
  ss << "retq" << std::endl;

  Code c;
  ss >> c;

  TUnit fxn(c, 0, 0x4004f6, 0);
  Cfg cfg(fxn, RegSet::empty(), RegSet::empty() + rax);

  CpuState tc;

  Sandbox sb;
  sb.insert_input(tc);
  sb.run(cfg);

  auto result = *sb.result_begin();

  /** 0x4004fd accounts for an instruction length of 7 */
  /** This has been compared with what the actual hardware does. */
  EXPECT_EQ(0x4004fdul, result[rax]);
}

TEST(SandboxTest, CannotReadInvalidAddress) {
  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "movq (%rdi), %rcx" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;
  auto cfg = Cfg(TUnit(c));

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);

  uint64_t base = 0x0;
  tc.gp[x64asm::rdi].get_fixed_quad(0) =  base;
  tc.heap.resize(base, 0x4);
  for (uint64_t i = base; i < base + 4; ++i) {
    tc.heap.set_valid(i, true);
    tc.heap[i] = 0x10;
  }
  tc.heap.set_valid(3, false);

  sb.insert_input(tc);
  sb.run(cfg);
  EXPECT_EQ(ErrorCode::SIGSEGV_, sb.result_begin()->code);

}

TEST(SandboxTest, DivideBySpl1) {

  std::stringstream st;
  st << ".foo:" << std::endl;
  st << "movl $0x0, %edx" << std::endl;
  st << "divb %dl" << std::endl;
  st << "retq" << std::endl;

  x64asm::Code d;
  st >> d;
  auto cfg_2 = Cfg(TUnit(d));


  std::stringstream ss;
  ss << ".foo:" << std::endl;
  ss << "movl $0x0, %esp" << std::endl;
  ss << "divb %spl" << std::endl;
  ss << "retq" << std::endl;

  x64asm::Code c;
  ss >> c;
  auto cfg = Cfg(TUnit(c));

  CpuState tc;

  Sandbox sb;
  sb.set_abi_check(false);

  uint64_t base = 0x10;
  tc.gp[x64asm::rax].get_fixed_quad(0) = base;
  tc.gp[x64asm::rdx].get_fixed_quad(0) = base;

  sb.insert_input(tc);
  sb.run(cfg);

  auto code_1 = sb.result_begin()->code;

  sb.run(cfg_2);

  auto code_2 = sb.result_begin()->code;

  EXPECT_NE(ErrorCode::NORMAL, code_1);
  EXPECT_NE(ErrorCode::NORMAL, code_2);
  EXPECT_EQ(code_1, code_2);

}

} //namespace
