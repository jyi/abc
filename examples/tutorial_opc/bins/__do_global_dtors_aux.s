  .text
  .globl __do_global_dtors_aux
  .type __do_global_dtors_aux, @function

#! file-offset 0x5c0
#! rip-offset  0x4005c0
#! capacity    32 bytes

# Text                         #  Line  RIP       Bytes  Opcode            
.__do_global_dtors_aux:        #        0x4005c0  0      OPC=<label>       
  cmpb $0x0, 0x200a81(%rip)    #  1     0x4005c0  7      OPC=cmpb_m8_imm8  
  jne .L_4005da                #  2     0x4005c7  2      OPC=jne_label     
  pushq %rbp                   #  3     0x4005c9  1      OPC=pushq_r64_1   
  movq %rsp, %rbp              #  4     0x4005ca  3      OPC=movq_r64_r64  
  callq .deregister_tm_clones  #  5     0x4005cd  5      OPC=callq_label   
  popq %rbp                    #  6     0x4005d2  1      OPC=popq_r64_1    
  movb $0x1, 0x200a6e(%rip)    #  7     0x4005d3  7      OPC=movb_m8_imm8  
.L_4005da:                     #        0x4005da  0      OPC=<label>       
  retq                         #  8     0x4005da  1      OPC=retq          
  nop                          #  9     0x4005db  1      OPC=nop           
  nop                          #  10    0x4005dc  1      OPC=nop           
  nop                          #  11    0x4005dd  1      OPC=nop           
  nop                          #  12    0x4005de  1      OPC=nop           
  nop                          #  13    0x4005df  1      OPC=nop           
                                                                           
.size __do_global_dtors_aux, .-__do_global_dtors_aux

