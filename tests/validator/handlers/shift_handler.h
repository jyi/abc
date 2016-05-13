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

namespace stoke {

#include "src/symstate/bitvector.h"

class ValidatorShiftTest : public StraightLineValidatorTest {};


TEST_F(ValidatorShiftTest, IdentitySalb) {
  //note: the 'and' is needed to make sure 'cf' is defined

  target_ << ".foo:" << std::endl;
  target_ << "andb $0x7, %cl" << std::endl;
  target_ << "salb %cl, %sil" << std::endl;
  target_ << "retq" << std::endl;

  rewrite_ << ".foo:" << std::endl;
  rewrite_ << "andb $0x7, %cl" << std::endl;
  rewrite_ << "salb %cl, %sil" << std::endl;
  rewrite_ << "retq" << std::endl;

  set_live_outs(x64asm::RegSet::empty() + x64asm::sil + x64asm::cl);
  assert_equiv();

}

TEST_F(ValidatorShiftTest, OutputsEqualInBigShift) {
  // If you shift by 0xf0 (i.e. 15*16 = 225 + 15 = 240) bits
  // that's the same as shifting by 0x10 (i.e. 16 bits) on the value
  // of the register (but not on the CF)

  target_ << ".foo:" << std::endl;
  target_ << "sall $0x10, %eax" << std::endl;
  target_ << "retq" << std::endl;

  rewrite_ << ".foo:" << std::endl;
  rewrite_ << "sall $0xf0, %eax" << std::endl;
  rewrite_ << "retq" << std::endl;

  set_live_outs(x64asm::RegSet::empty() + x64asm::eax);

  assert_equiv();

}

TEST_F(ValidatorShiftTest, CfUndefinedInBigShift) {

  target_ << ".foo:" << std::endl;
  target_ << "salw $0x11, %ax" << std::endl;
  target_ << "retq" << std::endl;

  target_ << ".foo:" << std::endl;
  rewrite_ << "salw $0x11, %ax" << std::endl;
  rewrite_ << "retq" << std::endl;

  set_live_outs(x64asm::RegSet::empty() + x64asm::eflags_cf);

  assert_ceg_nocheck();

}

TEST_F(ValidatorShiftTest, CfUndefinedInBigShift2) {

  target_ << ".foo:" << std::endl;
  target_ << "salw $0x10, %ax" << std::endl;
  target_ << "retq" << std::endl;

  target_ << ".foo:" << std::endl;
  rewrite_ << "salw $0x10, %ax" << std::endl;
  rewrite_ << "retq" << std::endl;

  set_live_outs(x64asm::RegSet::empty() + x64asm::eflags_cf);

  assert_ceg_nocheck();

}

// Right now we don't have enough precision in the circuit
TEST_F(ValidatorShiftTest, DISABLED_CfDefinedInSmallShift) {

  target_ << ".foo:" << std::endl;
  target_ << "sall $0x10, %eax" << std::endl;
  target_ << "retq" << std::endl;

  rewrite_ << ".foo:" << std::endl;
  rewrite_ << "sall $0xf0, %eax" << std::endl;
  rewrite_ << "retq" << std::endl;

  set_live_outs(x64asm::RegSet::empty() + x64asm::eflags_cf);

  assert_equiv();

}

// Right now we don't have enough precision in the circuit
TEST_F(ValidatorShiftTest, DISABLED_CfDdefinedInSmallShift2) {

  target_ << ".foo:" << std::endl;
  target_ << "salw $0x0f, %ax" << std::endl;
  target_ << "retq" << std::endl;

  target_ << ".foo:" << std::endl;
  rewrite_ << "salw $0x0f, %ax" << std::endl;
  rewrite_ << "retq" << std::endl;

  set_live_outs(x64asm::RegSet::empty() + x64asm::eflags_cf);

  assert_equiv();

}

TEST_F(ValidatorShiftTest, RotateWorks) {

  target_ << ".foo:" << std::endl;
  target_ << "rolb $0xab, %r14b" << std::endl;
  target_ << "retq" << std::endl;

  rewrite_ << ".foo:" << std::endl;
  rewrite_ << "rolb $0xab, %r14b" << std::endl;
  rewrite_ << "retq" << std::endl;

  set_live_outs(x64asm::RegSet::empty() + x64asm::r14);

  assert_equiv();

}
} //namespace stoke
