  .text
  .globl frame_dummy
  .type frame_dummy, @function

#! file-offset 0x5e0
#! rip-offset  0x4005e0
#! capacity    48 bytes

# Text                      #  Line  RIP       Bytes  Opcode              
.frame_dummy:               #        0x4005e0  0      OPC=<label>         
  movl $0x600e20, %edi      #  1     0x4005e0  5      OPC=movl_r32_imm32  
  cmpq $0x0, (%rdi)         #  2     0x4005e5  4      OPC=cmpq_m64_imm8   
  jne .L_4005f0             #  3     0x4005e9  2      OPC=jne_label       
.L_4005eb:                  #        0x4005eb  0      OPC=<label>         
  jmpq .register_tm_clones  #  4     0x4005eb  2      OPC=jmpq_label      
  nop                       #  5     0x4005ed  1      OPC=nop             
  nop                       #  6     0x4005ee  1      OPC=nop             
  nop                       #  7     0x4005ef  1      OPC=nop             
.L_4005f0:                  #        0x4005f0  0      OPC=<label>         
  movl $0x0, %eax           #  8     0x4005f0  5      OPC=movl_r32_imm32  
  testq %rax, %rax          #  9     0x4005f5  3      OPC=testq_r64_r64   
  je .L_4005eb              #  10    0x4005f8  2      OPC=je_label        
  pushq %rbp                #  11    0x4005fa  1      OPC=pushq_r64_1     
  movq %rsp, %rbp           #  12    0x4005fb  3      OPC=movq_r64_r64    
  callq %rax                #  13    0x4005fe  2      OPC=callq_r64       
  popq %rbp                 #  14    0x400600  1      OPC=popq_r64_1      
  jmpq .register_tm_clones  #  15    0x400601  5      OPC=jmpq_label_1    
  nop                       #  16    0x400606  1      OPC=nop             
  nop                       #  17    0x400607  1      OPC=nop             
  nop                       #  18    0x400608  1      OPC=nop             
  nop                       #  19    0x400609  1      OPC=nop             
  nop                       #  20    0x40060a  1      OPC=nop             
  nop                       #  21    0x40060b  1      OPC=nop             
  nop                       #  22    0x40060c  1      OPC=nop             
  nop                       #  23    0x40060d  1      OPC=nop             
  nop                       #  24    0x40060e  1      OPC=nop             
  nop                       #  25    0x40060f  1      OPC=nop             
                                                                          
.size frame_dummy, .-frame_dummy

