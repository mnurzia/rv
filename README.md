# rv

RISC-V CPU core written in ANSI C.

Features:
- `RV32IMC` user-level implementation
- Passes all supported tests in [`riscv-tests`](https://github.com/riscv/riscv-tests)
- ~600 lines of code
- Doesn't use any integer types larger than 32 bits, even for multiplication
- Simple API (two functions, plus two memory callback functions that you provide)
- No memory allocations

## API

```c
/* Memory access callbacks: data is input/output, return RV_BAD on fault, 0 otherwise */
typedef rv_res (*rv_store_cb)(void *user, rv_u32 addr, rv_u8 data);
typedef rv_res (*rv_load_cb)(void *user, rv_u32 addr, rv_u8 *data);

/* Initialize CPU. */
void rv_init(rv *cpu, void *user, rv_load_cb load_cb, rv_store_cb store_cb);

/* Single-step CPU. Returns 0 on success, one of RV_E* on exception. */
rv_u32 rv_step(rv *cpu);
```

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

rv_u32 program[2] = {
    /* _start: */
    0x02A88893, /* add a7, a7, 42 */
    0x00000073  /* ecall */
};

int main(void) {
  rv_u8 mem[0x10000];
  rv cpu;
  rv_init(&cpu, (void *)mem, &load_cb, &store_cb);
  memcpy((void *)mem, (void *)program, sizeof(program));
  while (rv_step(&cpu) != RV_EECALL) {
  }
  printf("Environment call @ %08X: %u\n", cpu.pc, cpu.r[17]);
  return 0;
}
```

## Targeting `rv`

Use [riscv-gnu-toolchain](https://github.com/riscv-collab/riscv-gnu-toolchain) with [tools/link.ld](tools/link.ld).

Suggested GCC commandline:

`riscv64-unknown-elf-gcc example.S -nostdlib -nostartfiles -Tlink.ld -march=rv32imc -mabi=ilp32 -o example.o -e _start -g -no-pie`

To dump a binary starting at `0x80000000` that can be directly loaded by `rv` as in the above example:

`riscv64-unknown-elf-objcopy -g -O binary example.o example.bin`

## Instruction List

Click an instruction to see its implementation in `rv.c`.

- [`add       `](rv.c#L419)[`addi      `](rv.c#L419)[`and       `](rv.c#L433)[`andi      `](rv.c#L433)[`auipc     `](rv.c#L505)[`beq       `](rv.c#L375)[`bge       `](rv.c#L378)[`bgtu      `](rv.c#L380)
- [`blt       `](rv.c#L377)[`bltu      `](rv.c#L379)[`bne       `](rv.c#L376)[`c.add     `](rv.c#L296)[`c.addi    `](rv.c#L237)[`c.addi16sp`](rv.c#L244)[`c.and     `](rv.c#L265)[`c.andi    `](rv.c#L256)
- [`c.beqz    `](rv.c#L275)[`c.bnez    `](rv.c#L277)[`c.j       `](rv.c#L273)[`c.jal     `](rv.c#L239)[`c.jalr    `](rv.c#L293)[`c.jr      `](rv.c#L288)[`c.li      `](rv.c#L241)[`c.lui     `](rv.c#L246)
- [`c.lw      `](rv.c#L229)[`c.lwsp    `](rv.c#L285)[`c.mv      `](rv.c#L290)[`c.or      `](rv.c#L263)[`c.slli    `](rv.c#L283)[`c.srai    `](rv.c#L254)[`c.srli    `](rv.c#L252)[`c.sub     `](rv.c#L259)
- [`c.sw      `](rv.c#L231)[`c.swsp    `](rv.c#L298)[`c.xor     `](rv.c#L261)[`csrrc     `](rv.c#L481)[`csrrci    `](rv.c#L481)[`csrrs     `](rv.c#L475)[`csrrsi    `](rv.c#L475)[`csrrw     `](rv.c#L466)
- [`csrrwi    `](rv.c#L466)[`div       `](rv.c#L451)[`divu      `](rv.c#L453)[`ebreak    `](rv.c#L494)[`ecall     `](rv.c#L491)[`fence     `](rv.c#L397)[`fence.i   `](rv.c#L401)[`jal       `](rv.c#L405)
- [`jalr      `](rv.c#L390)[`lb        `](rv.c#L330)[`lbu       `](rv.c#L338)[`lh        `](rv.c#L333)[`lhu       `](rv.c#L341)[`lui       `](rv.c#L507)[`lw        `](rv.c#L336)[`mret      `](rv.c#L489)
- [`mul       `](rv.c#L438)[`mulh      `](rv.c#L438)[`mulhsu    `](rv.c#L438)[`mulhu     `](rv.c#L438)[`or        `](rv.c#L431)[`ori       `](rv.c#L431)[`rem       `](rv.c#L455)[`remu      `](rv.c#L457)
- [`sb        `](rv.c#L359)[`sh        `](rv.c#L361)[`sll       `](rv.c#L421)[`slli      `](rv.c#L421)[`slt       `](rv.c#L423)[`slti      `](rv.c#L423)[`sltiu     `](rv.c#L425)[`sltu      `](rv.c#L425)
- [`sra       `](rv.c#L429)[`srai      `](rv.c#L429)[`srl       `](rv.c#L429)[`srli      `](rv.c#L429)[`sub       `](rv.c#L419)[`sw        `](rv.c#L363)[`xor       `](rv.c#L427)[`xori      `](rv.c#L427)

## FAQ

### Spaghetti code!

- `rv` tries to strike a good balance between conciseness and readability. Of course, being able to read this code at all requires intimate prior knowledge of the ISA encoding.

### No switch statements!

- C only allows constant expressions in switch statements. In addition to an abundance of `break` statements using these would result in more bloated code in the author's opinion. You are free to reimplement this code with switch statements. See [LICENSE.txt](LICENSE.txt).

### Not useful!
- [Ok](https://www.google.com/search?q=happy+smiley+thumbs+up+happy+cool+funny+ok&tbm=isch)

## Caveats

- Written in C89.
- Not actually written in C89, since it uses external names longer than 6 characters.
- Doesn't use any integer types larger than 32 bits, even for multiplication, because it's written in C89.
- Assumes width of integer types in a way that's not completely compliant with C89/99. Fix for this is coming soon, I'm working on a watertight `<stdint.h>` for C89.
- Written in C89.
