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

- [`add       `](rv.c#L566)[`addi      `](rv.c#L566)[`amoadd.w  `](rv.c#L525)[`amoand.w  `](rv.c#L537)[`amomax.w  `](rv.c#L541)[`amomaxu.w `](rv.c#L545)[`amomin.w  `](rv.c#L539)[`amominu.w `](rv.c#L543)
- [`amoor.w   `](rv.c#L535)[`amoswap.w `](rv.c#L527)[`amoxor.w  `](rv.c#L533)[`and       `](rv.c#L583)[`andi      `](rv.c#L583)[`auipc     `](rv.c#L673)[`beq       `](rv.c#L486)[`bge       `](rv.c#L489)
- [`bgtu      `](rv.c#L491)[`blt       `](rv.c#L488)[`bltu      `](rv.c#L490)[`bne       `](rv.c#L487)[`c.add     `](rv.c#L385)[`c.addi    `](rv.c#L323)[`c.addi16sp`](rv.c#L330)[`c.and     `](rv.c#L351)
- [`c.andi    `](rv.c#L342)[`c.beqz    `](rv.c#L361)[`c.bnez    `](rv.c#L363)[`c.ebreak  `](rv.c#L382)[`c.j       `](rv.c#L359)[`c.jal     `](rv.c#L325)[`c.jalr    `](rv.c#L379)[`c.jr      `](rv.c#L374)
- [`c.li      `](rv.c#L327)[`c.lui     `](rv.c#L332)[`c.lw      `](rv.c#L315)[`c.lwsp    `](rv.c#L371)[`c.mv      `](rv.c#L376)[`c.or      `](rv.c#L349)[`c.slli    `](rv.c#L369)[`c.srai    `](rv.c#L340)
- [`c.srli    `](rv.c#L338)[`c.sub     `](rv.c#L345)[`c.sw      `](rv.c#L317)[`c.swsp    `](rv.c#L387)[`c.xor     `](rv.c#L347)[`csrrc     `](rv.c#L626)[`csrrci    `](rv.c#L626)[`csrrs     `](rv.c#L620)
- [`csrrsi    `](rv.c#L620)[`csrrw     `](rv.c#L611)[`csrrwi    `](rv.c#L611)[`div       `](rv.c#L597)[`divu      `](rv.c#L599)[`ebreak    `](rv.c#L662)[`ecall     `](rv.c#L659)[`fence     `](rv.c#L508)
- [`fence.i   `](rv.c#L512)[`jal       `](rv.c#L554)[`jalr      `](rv.c#L501)[`lb        `](rv.c#L465)[`lbu       `](rv.c#L465)[`lh        `](rv.c#L465)[`lhu       `](rv.c#L465)[`lr.w      `](rv.c#L529)
- [`lui       `](rv.c#L675)[`lw        `](rv.c#L465)[`mret      `](rv.c#L635)[`mul       `](rv.c#L587)[`mulh      `](rv.c#L587)[`mulhsu    `](rv.c#L587)[`mulhu     `](rv.c#L587)[`or        `](rv.c#L581)
- [`ori       `](rv.c#L581)[`rem       `](rv.c#L601)[`remu      `](rv.c#L603)[`sb        `](rv.c#L477)[`sc.w      `](rv.c#L531)[`sfence.vma`](rv.c#L655)[`sh        `](rv.c#L477)[`sll       `](rv.c#L571)
- [`slli      `](rv.c#L571)[`slt       `](rv.c#L573)[`slti      `](rv.c#L573)[`sltiu     `](rv.c#L575)[`sltu      `](rv.c#L575)[`sra       `](rv.c#L579)[`srai      `](rv.c#L579)[`sret      `](rv.c#L635)
- [`srl       `](rv.c#L579)[`srli      `](rv.c#L579)[`sub       `](rv.c#L566)[`sw        `](rv.c#L477)[`wfi       `](rv.c#L652)[`xor       `](rv.c#L577)[`xori      `](rv.c#L577)

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
