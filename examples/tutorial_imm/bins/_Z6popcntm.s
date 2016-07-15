  .text
  .globl _Z6popcntm
  .type _Z6popcntm, @function

#! file-offset 0x610
#! rip-offset  0x400610
#! capacity    48 bytes

# Text              #  Line  RIP       Bytes  Opcode             
._Z6popcntm:        #        0x400610  0      OPC=<label>        
  testq %rdi, %rdi  #  1     0x400610  3      OPC=testq_r64_r64  
  je .L_40062f      #  2     0x400613  2      OPC=je_label       
  xorl %eax, %eax   #  3     0x400615  2      OPC=xorl_r32_r32   
  nop               #  4     0x400617  1      OPC=nop            
  nop               #  5     0x400618  1      OPC=nop            
  nop               #  6     0x400619  1      OPC=nop            
  nop               #  7     0x40061a  1      OPC=nop            
  nop               #  8     0x40061b  1      OPC=nop            
  nop               #  9     0x40061c  1      OPC=nop            
  nop               #  10    0x40061d  1      OPC=nop            
  nop               #  11    0x40061e  1      OPC=nop            
  nop               #  12    0x40061f  1      OPC=nop            
.L_400620:          #        0x400620  0      OPC=<label>        
  movl %edi, %edx   #  13    0x400620  2      OPC=movl_r32_r32   
  andl $0x1, %edx   #  14    0x400622  3      OPC=andl_r32_imm8  
  addl %edx, %eax   #  15    0x400625  2      OPC=addl_r32_r32   
  shrq $0x1, %rdi   #  16    0x400627  3      OPC=shrq_r64_one   
  jne .L_400620     #  17    0x40062a  2      OPC=jne_label      
  cltq              #  18    0x40062c  2      OPC=cltq           
  retq              #  19    0x40062e  1      OPC=retq           
.L_40062f:          #        0x40062f  0      OPC=<label>        
  xorl %eax, %eax   #  20    0x40062f  2      OPC=xorl_r32_r32   
  retq              #  21    0x400631  1      OPC=retq           
  nop               #  22    0x400632  1      OPC=nop            
  nop               #  23    0x400633  1      OPC=nop            
  nop               #  24    0x400634  1      OPC=nop            
  nop               #  25    0x400635  1      OPC=nop            
  nop               #  26    0x400636  1      OPC=nop            
  nop               #  27    0x400637  1      OPC=nop            
  nop               #  28    0x400638  1      OPC=nop            
  nop               #  29    0x400639  1      OPC=nop            
  nop               #  30    0x40063a  1      OPC=nop            
  nop               #  31    0x40063b  1      OPC=nop            
  nop               #  32    0x40063c  1      OPC=nop            
  nop               #  33    0x40063d  1      OPC=nop            
  nop               #  34    0x40063e  1      OPC=nop            
  nop               #  35    0x40063f  1      OPC=nop            
                                                                 
.size _Z6popcntm, .-_Z6popcntm

