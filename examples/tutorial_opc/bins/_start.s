  .text
  .globl _start
  .type _start, @function

#! file-offset 0x50c
#! rip-offset  0x40050c
#! capacity    52 bytes

# Text                          #  Line  RIP       Bytes  Opcode              
._start:                        #        0x40050c  0      OPC=<label>         
  xorl %ebp, %ebp               #  1     0x40050c  2      OPC=xorl_r32_r32    
  movq %rdx, %r9                #  2     0x40050e  3      OPC=movq_r64_r64    
  popq %rsi                     #  3     0x400511  1      OPC=popq_r64_1      
  movq %rsp, %rdx               #  4     0x400512  3      OPC=movq_r64_r64    
  andq $0xf0, %rsp              #  5     0x400515  4      OPC=andq_r64_imm8   
  pushq %rax                    #  6     0x400519  1      OPC=pushq_r64_1     
  pushq %rsp                    #  7     0x40051a  1      OPC=pushq_r64_1     
  movq $0x4006b0, %r8           #  8     0x40051b  7      OPC=movq_r64_imm32  
  movq $0x400640, %rcx          #  9     0x400522  7      OPC=movq_r64_imm32  
  movq $0x400490, %rdi          #  10    0x400529  7      OPC=movq_r64_imm32  
  callq .__libc_start_main_plt  #  11    0x400530  5      OPC=callq_label     
  retq                          #  12    0x400535  1      OPC=retq            
  nop                           #  13    0x400536  1      OPC=nop             
  nop                           #  14    0x400537  1      OPC=nop             
  nop                           #  15    0x400538  1      OPC=nop             
  nop                           #  16    0x400539  1      OPC=nop             
  nop                           #  17    0x40053a  1      OPC=nop             
  nop                           #  18    0x40053b  1      OPC=nop             
  nop                           #  19    0x40053c  1      OPC=nop             
  nop                           #  20    0x40053d  1      OPC=nop             
  nop                           #  21    0x40053e  1      OPC=nop             
  nop                           #  22    0x40053f  1      OPC=nop             
                                                                              
.size _start, .-_start

