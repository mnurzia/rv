.section .text.init, "ax"
.global _start
_start:
  csrrs sp, mhartid, x0 # setup stack based on hartid
  addi sp, sp, 1        # stack  = 0x40000000 + 0x00400000 * (mhartid + 1)
  slli sp, sp, 22
  li t0, 0x40000000
  add sp, sp, t0
  call rt_main

.section .text
.global rt_trap
rt_trap:
  ebreak
  ret

.global rt_spin
rt_spin:
  ret

.global rt_cpu
rt_cpu:
  csrrs a0, mhartid, x0
  ret
