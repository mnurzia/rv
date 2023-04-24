.section .text.init, "ax"
.global _start
_start:
  csrrs sp, mhartid, x0 # setup stack based on hartid
  addi sp, sp, 1
  slli sp, sp, 22
  li t0, 0x40000000
  add sp, sp, t0
  call rv_main

.section .text
.global rv_trap
rv_trap:
  ebreak
  ret
