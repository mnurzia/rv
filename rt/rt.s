.section .text.init, "ax"
.global _start
_start:
  li sp, 0x80010000
  call rv_main

.section .text
.global rv_trap
rv_trap:
  ebreak
  ret
