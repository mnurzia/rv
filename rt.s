.section .init, "ax"
.global _start
_start:
  li sp, 0xFFFF
  call rv_main
