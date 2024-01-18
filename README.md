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
typedef rv_res (*rv_bus_cb)(void *user, rv_u32 addr, rv_u8 *data, rv_u32 store, rv_u32 width);

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

rv_res bus_cb(void *user, rv_u32 addr, rv_u8 *data, rv_u32 is_store,
              rv_u32 width) {
  rv_u8 *mem = (rv_u8 *)user + addr - 0x80000000;
  if (addr < 0x80000000 || addr + width >= 0x80000000 + 0x10000)
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
  rv_u8 mem[0x10000];
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

- [`add       `](rv.c#L575)[`addi      `](rv.c#L575)[`amoadd.w  `](rv.c#L538)[`amoand.w  `](rv.c#L546)[`amomax.w  `](rv.c#L550)[`amomaxu.w `](rv.c#L554)[`amomin.w  `](rv.c#L548)[`amominu.w `](rv.c#L552)
- [`amoor.w   `](rv.c#L544)[`amoswap.w `](rv.c#L540)[`amoxor.w  `](rv.c#L542)[`and       `](rv.c#L592)[`andi      `](rv.c#L592)[`auipc     `](rv.c#L685)[`beq       `](rv.c#L487)[`bge       `](rv.c#L490)
- [`bgtu      `](rv.c#L492)[`blt       `](rv.c#L489)[`bltu      `](rv.c#L491)[`bne       `](rv.c#L488)[`c.add     `](rv.c#L387)[`c.addi    `](rv.c#L325)[`c.addi16sp`](rv.c#L332)[`c.and     `](rv.c#L353)
- [`c.andi    `](rv.c#L344)[`c.beqz    `](rv.c#L363)[`c.bnez    `](rv.c#L365)[`c.ebreak  `](rv.c#L384)[`c.j       `](rv.c#L361)[`c.jal     `](rv.c#L327)[`c.jalr    `](rv.c#L381)[`c.jr      `](rv.c#L376)
- [`c.li      `](rv.c#L329)[`c.lui     `](rv.c#L334)[`c.lw      `](rv.c#L317)[`c.lwsp    `](rv.c#L373)[`c.mv      `](rv.c#L378)[`c.or      `](rv.c#L351)[`c.slli    `](rv.c#L371)[`c.srai    `](rv.c#L342)
- [`c.srli    `](rv.c#L340)[`c.sub     `](rv.c#L347)[`c.sw      `](rv.c#L319)[`c.swsp    `](rv.c#L389)[`c.xor     `](rv.c#L349)[`csrrc     `](rv.c#L638)[`csrrci    `](rv.c#L638)[`csrrs     `](rv.c#L632)
- [`csrrsi    `](rv.c#L632)[`csrrw     `](rv.c#L623)[`csrrwi    `](rv.c#L623)[`div       `](rv.c#L609)[`divu      `](rv.c#L611)[`ebreak    `](rv.c#L674)[`ecall     `](rv.c#L671)[`fence     `](rv.c#L509)
- [`fence.i   `](rv.c#L513)[`jal       `](rv.c#L563)[`jalr      `](rv.c#L502)[`lb        `](rv.c#L466)[`lbu       `](rv.c#L466)[`lh        `](rv.c#L466)[`lhu       `](rv.c#L466)[`lr.w      `](rv.c#L524)
- [`lui       `](rv.c#L687)[`lw        `](rv.c#L466)[`mret      `](rv.c#L647)[`mul       `](rv.c#L596)[`mulh      `](rv.c#L596)[`mulhsu    `](rv.c#L596)[`mulhu     `](rv.c#L596)[`or        `](rv.c#L590)
- [`ori       `](rv.c#L590)[`rem       `](rv.c#L613)[`remu      `](rv.c#L615)[`sb        `](rv.c#L478)[`sc.w      `](rv.c#L528)[`sfence.vma`](rv.c#L667)[`sh        `](rv.c#L478)[`sll       `](rv.c#L580)
- [`slli      `](rv.c#L580)[`slt       `](rv.c#L582)[`slti      `](rv.c#L582)[`sltiu     `](rv.c#L584)[`sltu      `](rv.c#L584)[`sra       `](rv.c#L588)[`srai      `](rv.c#L588)[`sret      `](rv.c#L647)
- [`srl       `](rv.c#L588)[`srli      `](rv.c#L588)[`sub       `](rv.c#L575)[`sw        `](rv.c#L478)[`wfi       `](rv.c#L664)[`xor       `](rv.c#L586)[`xori      `](rv.c#L586)

## FAQ

### Spaghetti code!

- `rv` tries to strike a good balance between conciseness and readability. Of course, being able to read this code at all requires intimate prior knowledge of the ISA encoding.

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
