  .text
  .globl main
  .type main, @function

#! file-offset 0x490
#! rip-offset  0x400490
#! capacity    124 bytes

# Text                         #  Line  RIP       Bytes  Opcode              
.main:                         #        0x400490  0      OPC=<label>         
  movq 0x8(%rsi), %rdi         #  1     0x400490  4      OPC=movq_r64_m64    
  pushq %r14                   #  2     0x400494  2      OPC=pushq_r64_1     
  pushq %r13                   #  3     0x400496  2      OPC=pushq_r64_1     
  pushq %r12                   #  4     0x400498  2      OPC=pushq_r64_1     
  pushq %rbp                   #  5     0x40049a  1      OPC=pushq_r64_1     
  pushq %rbx                   #  6     0x40049b  1      OPC=pushq_r64_1     
  callq .atoi_plt              #  7     0x40049c  5      OPC=callq_label     
  testl %eax, %eax             #  8     0x4004a1  2      OPC=testl_r32_r32   
  movl %eax, %ebp              #  9     0x4004a3  2      OPC=movl_r32_r32    
  jle .L_400508                #  10    0x4004a5  2      OPC=jle_label       
  xorl %r14d, %r14d            #  11    0x4004a7  3      OPC=xorl_r32_r32    
  movl $0x1, %r13d             #  12    0x4004aa  6      OPC=movl_r32_imm32  
  xorl %ebx, %ebx              #  13    0x4004b0  2      OPC=xorl_r32_r32    
  movl $0x66666667, %r12d      #  14    0x4004b2  6      OPC=movl_r32_imm32  
  nop                          #  15    0x4004b8  1      OPC=nop             
  nop                          #  16    0x4004b9  1      OPC=nop             
  nop                          #  17    0x4004ba  1      OPC=nop             
  nop                          #  18    0x4004bb  1      OPC=nop             
  nop                          #  19    0x4004bc  1      OPC=nop             
  nop                          #  20    0x4004bd  1      OPC=nop             
  nop                          #  21    0x4004be  1      OPC=nop             
  nop                          #  22    0x4004bf  1      OPC=nop             
.L_4004c0:                     #        0x4004c0  0      OPC=<label>         
  callq .rand_plt              #  23    0x4004c0  5      OPC=callq_label     
  movl %eax, %ecx              #  24    0x4004c5  2      OPC=movl_r32_r32    
  leaq (%r13,%r13,8), %rax     #  25    0x4004c7  5      OPC=leaq_r64_m16    
  addl $0x1, %r14d             #  26    0x4004cc  4      OPC=addl_r32_imm8   
  leaq 0x7(%r13,%rax,2), %r13  #  27    0x4004d0  5      OPC=leaq_r64_m16    
  movl %ecx, %eax              #  28    0x4004d5  2      OPC=movl_r32_r32    
  imull %r12d                  #  29    0x4004d7  3      OPC=imull_r32       
  movl %ecx, %eax              #  30    0x4004da  2      OPC=movl_r32_r32    
  sarl $0x1f, %eax             #  31    0x4004dc  3      OPC=sarl_r32_imm8   
  sarl $0x1, %edx              #  32    0x4004df  2      OPC=sarl_r32_one    
  subl %eax, %edx              #  33    0x4004e1  2      OPC=subl_r32_r32    
  leal (%rdx,%rdx,4), %eax     #  34    0x4004e3  3      OPC=leal_r32_m16    
  subl %eax, %ecx              #  35    0x4004e6  2      OPC=subl_r32_r32    
  movslq %ecx, %rcx            #  36    0x4004e8  3      OPC=movslq_r64_r32  
  addq %rcx, %r13              #  37    0x4004eb  3      OPC=addq_r64_r64    
  movq %r13, %rdi              #  38    0x4004ee  3      OPC=movq_r64_r64    
  callq ._Z6popcntm            #  39    0x4004f1  5      OPC=callq_label     
  addl %eax, %ebx              #  40    0x4004f6  2      OPC=addl_r32_r32    
  cmpl %ebp, %r14d             #  41    0x4004f8  3      OPC=cmpl_r32_r32    
  jne .L_4004c0                #  42    0x4004fb  2      OPC=jne_label       
.L_4004fd:                     #        0x4004fd  0      OPC=<label>         
  movl %ebx, %eax              #  43    0x4004fd  2      OPC=movl_r32_r32    
  popq %rbx                    #  44    0x4004ff  1      OPC=popq_r64_1      
  popq %rbp                    #  45    0x400500  1      OPC=popq_r64_1      
  popq %r12                    #  46    0x400501  2      OPC=popq_r64_1      
  popq %r13                    #  47    0x400503  2      OPC=popq_r64_1      
  popq %r14                    #  48    0x400505  2      OPC=popq_r64_1      
  retq                         #  49    0x400507  1      OPC=retq            
.L_400508:                     #        0x400508  0      OPC=<label>         
  xorl %ebx, %ebx              #  50    0x400508  2      OPC=xorl_r32_r32    
  jmpq .L_4004fd               #  51    0x40050a  2      OPC=jmpq_label      
                                                                             
.size main, .-main

