# rv

RISC-V CPU core written in C89.

Features:
- `RV32IMC` implementation
- Passes all supported tests in [`riscv-tests`](https://github.com/riscv/riscv-tests)
- ~600 lines of code
- Doesn't use any datatypes larger than 32 bits
- Simple API
- No memory allocations

## Usage

```c
#include <stdio.h>
#include <string.h>

#include "rv.h"

rv_res load_cb(void *user, rv_u32 addr, rv_u8 *data) {
  if (addr - 0x80000000 > 0x10000) /* Reset vector is 0x80000000 */
    return RV_BAD;
  *data = ((rv_u8 *)(user))[addr - 0x80000000];
  return RV_OK;
}

rv_res store_cb(void *user, rv_u32 addr, rv_u8 data) {
  if (addr - 0x80000000 > 0x10000)
    return RV_BAD;
  ((rv_u8 *)(user))[addr - 0x80000000] = data;
  return RV_OK;
}

rv_u32 program[4] = {
    /* _start: */
    0x02A88893, /* add a7, a7, 42 */
    0x00000073  /* ecall */
};

int main(void) {
  rv_u8 mem[0x10000];
  rv cpu;
  rv_init(&cpu, (void *)mem, &load_cb, &store_cb);
  memcpy((void *)mem, (void *)program, sizeof(program));
  while (rv_inst(&cpu) != RV_EECALL) {
  }
  printf("Environment call @ %08X: %u\n", cpu.pc, cpu.r[17]);
  return 0;
}
```

## Instructions

Click an instruction to see its implementation in `rv.c`.

- [`add       `](rv.c#L417)[`addi      `](rv.c#L417)[`and       `](rv.c#L431)[`andi      `](rv.c#L431)[`auipc     `](rv.c#L503)[`beq       `](rv.c#L373)[`bge       `](rv.c#L376)[`bgtu      `](rv.c#L378)
- [`blt       `](rv.c#L375)[`bltu      `](rv.c#L377)[`bne       `](rv.c#L374)[`c.add     `](rv.c#L294)[`c.addi    `](rv.c#L235)[`c.addi16sp`](rv.c#L242)[`c.and     `](rv.c#L263)[`c.andi    `](rv.c#L254)
- [`c.beqz    `](rv.c#L273)[`c.bnez    `](rv.c#L275)[`c.j       `](rv.c#L271)[`c.jal     `](rv.c#L237)[`c.jalr    `](rv.c#L291)[`c.jr      `](rv.c#L286)[`c.li      `](rv.c#L239)[`c.lui     `](rv.c#L244)
- [`c.lw      `](rv.c#L227)[`c.lwsp    `](rv.c#L283)[`c.mv      `](rv.c#L288)[`c.or      `](rv.c#L261)[`c.slli    `](rv.c#L281)[`c.srai    `](rv.c#L252)[`c.srli    `](rv.c#L250)[`c.sub     `](rv.c#L257)
- [`c.sw      `](rv.c#L229)[`c.swsp    `](rv.c#L296)[`c.xor     `](rv.c#L259)[`csrrc     `](rv.c#L479)[`csrrci    `](rv.c#L479)[`csrrs     `](rv.c#L473)[`csrrsi    `](rv.c#L473)[`csrrw     `](rv.c#L464)
- [`csrrwi    `](rv.c#L464)[`div       `](rv.c#L449)[`divu      `](rv.c#L451)[`ebreak    `](rv.c#L492)[`ecall     `](rv.c#L489)[`fence     `](rv.c#L395)[`fence.i   `](rv.c#L399)[`jal       `](rv.c#L403)
- [`jalr      `](rv.c#L388)[`lb        `](rv.c#L328)[`lbu       `](rv.c#L336)[`lh        `](rv.c#L331)[`lhu       `](rv.c#L339)[`lui       `](rv.c#L505)[`lw        `](rv.c#L334)[`mret      `](rv.c#L487)
- [`mul       `](rv.c#L436)[`mulh      `](rv.c#L436)[`mulhsu    `](rv.c#L436)[`mulhu     `](rv.c#L436)[`or        `](rv.c#L429)[`ori       `](rv.c#L429)[`rem       `](rv.c#L453)[`remu      `](rv.c#L455)
- [`sb        `](rv.c#L357)[`sh        `](rv.c#L359)[`sll       `](rv.c#L419)[`slli      `](rv.c#L419)[`slt       `](rv.c#L421)[`slti      `](rv.c#L421)[`sltiu     `](rv.c#L423)[`sltu      `](rv.c#L423)
- [`sra       `](rv.c#L427)[`srai      `](rv.c#L427)[`srl       `](rv.c#L427)[`srli      `](rv.c#L427)[`sub       `](rv.c#L417)[`sw        `](rv.c#L361)[`xor       `](rv.c#L425)[`xori      `](rv.c#L425)

## Caveats

- Assumes width of integer types in a way that's not completely compliant with C89/99. Fix for this is coming soon, I'm working on a watertight `<stdint.h>` for C89.
