# rv

RISC-V CPU core written in ANSI C.

Features:
- `RV32IMAC_Zicsr` implementation with M-mode and S-mode
- Boots RISCV32 Linux
- Passes all supported tests in [`riscv-tests`](https://github.com/riscv/riscv-tests)
- ~800 lines of code
- Doesn't use any integer types larger than 32 bits, even for multiplication
- Simple API (two required functions, plus one memory callback function that you provide)
- No memory allocations

## API

```c
/* Memory access callback: data is input/output, return RV_BAD on fault. */
typedef rv_res (*rv_bus_cb)(void *user, rv_u32 addr, rv_u8 *data, rv_u32 is_store, rv_u32 width);

/* Initialize CPU. You can call this again on `cpu` to reset it. */
void rv_init(rv *cpu, void *user, rv_bus_cb bus_cb);

/* Single-step CPU. Returns RV_E* on exception. */
rv_u32 rv_step(rv *cpu);
```

## Usage

```c
#include <stdio.h>
#include <string.h>

#include "rv.h"

#define RAM_BASE 0x80000000
#define RAM_SIZE 0x10000

rv_res bus_cb(void *user, rv_u32 addr, rv_u8 *data, rv_u32 is_store,
              rv_u32 width) {
  rv_u8 *mem = (rv_u8 *)user + addr - RAM_BASE;
  if (addr < RAM_BASE || addr + width >= RAM_BASE + RAM_SIZE)
    return RV_BAD;
  memcpy(is_store ? mem : data, is_store ? data : mem, width);
  return RV_OK;
}

rv_u32 program[2] = {
    /*            */             /* _start: */
    /* 0x80000000 */ 0x02A88893, /* add a7, a7, 42 */
    /* 0x80000004 */ 0x00000073  /* ecall */
};

int main(void) {
  rv_u8 mem[RAM_SIZE];
  rv cpu;
  rv_init(&cpu, (void *)mem, &bus_cb);
  memcpy((void *)mem, (void *)program, sizeof(program));
  while (rv_step(&cpu) != RV_EMECALL) {
  }
  printf("Environment call @ %08X: %u\n", cpu.csr.mepc, cpu.r[17]);
  return 0;
}
```

See [`tools/example/example.c`](tools/example/example.c).

## Running Linux

This repository contains a machine emulator that can use `rv` to boot Linux.
See [`tools/linux/README.md`](tools/linux/README.md).

## Targeting `rv`

Use [riscv-gnu-toolchain](https://github.com/riscv-collab/riscv-gnu-toolchain) with [tools/link.ld](tools/link.ld).

Suggested GCC commandline:

`riscv64-unknown-elf-gcc example.S -nostdlib -nostartfiles -Tlink.ld -march=rv32imac -mabi=ilp32 -o example.o -e _start -g -no-pie`

To dump a binary starting at `0x80000000` that can be directly loaded by `rv` as in the above example:

`riscv64-unknown-elf-objcopy -g -O binary example.o example.bin`

## Instruction List

Click an instruction to see its implementation in `rv.c`.

- [`add       `](rv.c#L589)[`addi      `](rv.c#L589)[`amoadd.w  `](rv.c#L548)[`amoand.w  `](rv.c#L560)[`amomax.w  `](rv.c#L564)[`amomaxu.w `](rv.c#L568)[`amomin.w  `](rv.c#L562)[`amominu.w `](rv.c#L566)
- [`amoor.w   `](rv.c#L558)[`amoswap.w `](rv.c#L550)[`amoxor.w  `](rv.c#L556)[`and       `](rv.c#L606)[`andi      `](rv.c#L606)[`auipc     `](rv.c#L696)[`beq       `](rv.c#L509)[`bge       `](rv.c#L512)
- [`bgtu      `](rv.c#L514)[`blt       `](rv.c#L511)[`bltu      `](rv.c#L513)[`bne       `](rv.c#L510)[`c.add     `](rv.c#L386)[`c.addi    `](rv.c#L324)[`c.addi16sp`](rv.c#L331)[`c.and     `](rv.c#L352)
- [`c.andi    `](rv.c#L343)[`c.beqz    `](rv.c#L362)[`c.bnez    `](rv.c#L364)[`c.ebreak  `](rv.c#L383)[`c.j       `](rv.c#L360)[`c.jal     `](rv.c#L326)[`c.jalr    `](rv.c#L380)[`c.jr      `](rv.c#L375)
- [`c.li      `](rv.c#L328)[`c.lui     `](rv.c#L333)[`c.lw      `](rv.c#L316)[`c.lwsp    `](rv.c#L372)[`c.mv      `](rv.c#L377)[`c.or      `](rv.c#L350)[`c.slli    `](rv.c#L370)[`c.srai    `](rv.c#L341)
- [`c.srli    `](rv.c#L339)[`c.sub     `](rv.c#L346)[`c.sw      `](rv.c#L318)[`c.swsp    `](rv.c#L388)[`c.xor     `](rv.c#L348)[`csrrc     `](rv.c#L649)[`csrrci    `](rv.c#L649)[`csrrs     `](rv.c#L643)
- [`csrrsi    `](rv.c#L643)[`csrrw     `](rv.c#L634)[`csrrwi    `](rv.c#L634)[`div       `](rv.c#L620)[`divu      `](rv.c#L622)[`ebreak    `](rv.c#L685)[`ecall     `](rv.c#L682)[`fence     `](rv.c#L531)
- [`fence.i   `](rv.c#L535)[`jal       `](rv.c#L577)[`jalr      `](rv.c#L524)[`lb        `](rv.c#L488)[`lbu       `](rv.c#L488)[`lh        `](rv.c#L488)[`lhu       `](rv.c#L488)[`lr.w      `](rv.c#L552)
- [`lui       `](rv.c#L698)[`lw        `](rv.c#L488)[`mret      `](rv.c#L658)[`mul       `](rv.c#L610)[`mulh      `](rv.c#L610)[`mulhsu    `](rv.c#L610)[`mulhu     `](rv.c#L610)[`or        `](rv.c#L604)
- [`ori       `](rv.c#L604)[`rem       `](rv.c#L624)[`remu      `](rv.c#L626)[`sb        `](rv.c#L500)[`sc.w      `](rv.c#L554)[`sfence.vma`](rv.c#L678)[`sh        `](rv.c#L500)[`sll       `](rv.c#L594)
- [`slli      `](rv.c#L594)[`slt       `](rv.c#L596)[`slti      `](rv.c#L596)[`sltiu     `](rv.c#L598)[`sltu      `](rv.c#L598)[`sra       `](rv.c#L602)[`srai      `](rv.c#L602)[`sret      `](rv.c#L658)
- [`srl       `](rv.c#L602)[`srli      `](rv.c#L602)[`sub       `](rv.c#L589)[`sw        `](rv.c#L500)[`wfi       `](rv.c#L675)[`xor       `](rv.c#L600)[`xori      `](rv.c#L600)

## FAQ

### Spaghetti code!

- `rv` was written in a way that takes maximal advantage of RISCV's instruction orthogonality.
- `rv` also tries to strike a good balance between conciseness and readability.
- Of course, being able to read this code at all requires intimate prior knowledge of the ISA encoding.

### No switch statements!

- C only allows constant expressions in switch statements. In addition to an abundance of `break` statements using these would result in more bloated code in the author's opinion. As it turns out, you are actually free to reimplement this code with switch statements. See [LICENSE.txt](LICENSE.txt).

### Not useful!
- [Ok](https://www.google.com/search?q=happy+smiley+thumbs+up+happy+cool+funny+ok&tbm=isch)

### Slow!
- [Ok](https://www.google.com/search?q=happy+smiley+thumbs+up+happy+cool+funny+ok&tbm=isch)

## Caveats

- Written in C89.
- Not actually written in C89, since it uses external names longer than 6 characters.
- Doesn't use any integer types larger than 32 bits, even for multiplication, because it's written in C89.
- Assumes width of integer types in a way that's not completely compliant with C89/99. Fix for this is coming soon, I'm working on a watertight `<stdint.h>` for C89.
- Written in C89.
