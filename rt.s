.section .init, "ax"
.global _start
_start:
  addi sp, sp, -16
  call rv_main
