  .text
  .globl deregister_tm_clones
  .type deregister_tm_clones, @function

#! file-offset 0x540
#! rip-offset  0x400540
#! capacity    64 bytes

# Text                  #  Line  RIP       Bytes  Opcode              
.deregister_tm_clones:  #        0x400540  0      OPC=<label>         
  movl $0x60104f, %eax  #  1     0x400540  5      OPC=movl_r32_imm32  
  pushq %rbp            #  2     0x400545  1      OPC=pushq_r64_1     
  subq $0x601048, %rax  #  3     0x400546  6      OPC=subq_rax_imm32  
  cmpq $0xe, %rax       #  4     0x40054c  4      OPC=cmpq_r64_imm8   
  movq %rsp, %rbp       #  5     0x400550  3      OPC=movq_r64_r64    
  jbe .L_400570         #  6     0x400553  2      OPC=jbe_label       
  movl $0x0, %eax       #  7     0x400555  5      OPC=movl_r32_imm32  
  testq %rax, %rax      #  8     0x40055a  3      OPC=testq_r64_r64   
  je .L_400570          #  9     0x40055d  2      OPC=je_label        
  popq %rbp             #  10    0x40055f  1      OPC=popq_r64_1      
  movl $0x601048, %edi  #  11    0x400560  5      OPC=movl_r32_imm32  
  jmpq %rax             #  12    0x400565  2      OPC=jmpq_r64        
  nop                   #  13    0x400567  1      OPC=nop             
  nop                   #  14    0x400568  1      OPC=nop             
  nop                   #  15    0x400569  1      OPC=nop             
  nop                   #  16    0x40056a  1      OPC=nop             
  nop                   #  17    0x40056b  1      OPC=nop             
  nop                   #  18    0x40056c  1      OPC=nop             
  nop                   #  19    0x40056d  1      OPC=nop             
  nop                   #  20    0x40056e  1      OPC=nop             
  nop                   #  21    0x40056f  1      OPC=nop             
.L_400570:              #        0x400570  0      OPC=<label>         
  popq %rbp             #  22    0x400570  1      OPC=popq_r64_1      
  retq                  #  23    0x400571  1      OPC=retq            
  nop                   #  24    0x400572  1      OPC=nop             
  nop                   #  25    0x400573  1      OPC=nop             
  nop                   #  26    0x400574  1      OPC=nop             
  nop                   #  27    0x400575  1      OPC=nop             
  nop                   #  28    0x400576  1      OPC=nop             
  nop                   #  29    0x400577  1      OPC=nop             
  nop                   #  30    0x400578  1      OPC=nop             
  nop                   #  31    0x400579  1      OPC=nop             
  nop                   #  32    0x40057a  1      OPC=nop             
  nop                   #  33    0x40057b  1      OPC=nop             
  nop                   #  34    0x40057c  1      OPC=nop             
  nop                   #  35    0x40057d  1      OPC=nop             
  nop                   #  36    0x40057e  1      OPC=nop             
  nop                   #  37    0x40057f  1      OPC=nop             
                                                                      
.size deregister_tm_clones, .-deregister_tm_clones

