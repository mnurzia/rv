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

- [`add       `](rv.c#L568)[`addi      `](rv.c#L568)[`amoadd.w  `](rv.c#L527)[`amoand.w  `](rv.c#L535)[`amomax.w  `](rv.c#L539)[`amomaxu.w `](rv.c#L543)[`amomin.w  `](rv.c#L537)[`amominu.w `](rv.c#L541)
- [`amoor.w   `](rv.c#L533)[`amoswap.w `](rv.c#L529)[`amoxor.w  `](rv.c#L531)[`and       `](rv.c#L585)[`andi      `](rv.c#L585)[`auipc     `](rv.c#L677)[`beq       `](rv.c#L474)[`bge       `](rv.c#L477)
- [`bgtu      `](rv.c#L479)[`blt       `](rv.c#L476)[`bltu      `](rv.c#L478)[`bne       `](rv.c#L475)[`c.add     `](rv.c#L388)[`c.addi    `](rv.c#L326)[`c.addi16sp`](rv.c#L333)[`c.and     `](rv.c#L354)
- [`c.andi    `](rv.c#L345)[`c.beqz    `](rv.c#L364)[`c.bnez    `](rv.c#L366)[`c.ebreak  `](rv.c#L385)[`c.j       `](rv.c#L362)[`c.jal     `](rv.c#L328)[`c.jalr    `](rv.c#L382)[`c.jr      `](rv.c#L377)
- [`c.li      `](rv.c#L330)[`c.lui     `](rv.c#L335)[`c.lw      `](rv.c#L318)[`c.lwsp    `](rv.c#L374)[`c.mv      `](rv.c#L379)[`c.or      `](rv.c#L352)[`c.slli    `](rv.c#L372)[`c.srai    `](rv.c#L343)
- [`c.srli    `](rv.c#L341)[`c.sub     `](rv.c#L348)[`c.sw      `](rv.c#L320)[`c.swsp    `](rv.c#L390)[`c.xor     `](rv.c#L350)[`csrrc     `](rv.c#L633)[`csrrci    `](rv.c#L633)[`csrrs     `](rv.c#L627)
- [`csrrsi    `](rv.c#L627)[`csrrw     `](rv.c#L618)[`csrrwi    `](rv.c#L618)[`div       `](rv.c#L603)[`divu      `](rv.c#L605)[`ebreak    `](rv.c#L666)[`ecall     `](rv.c#L663)[`fence     `](rv.c#L496)
- [`fence.i   `](rv.c#L500)[`jal       `](rv.c#L554)[`jalr      `](rv.c#L489)[`lb        `](rv.c#L453)[`lbu       `](rv.c#L453)[`lh        `](rv.c#L453)[`lhu       `](rv.c#L453)[`lr.w      `](rv.c#L513)
- [`lui       `](rv.c#L679)[`lw        `](rv.c#L453)[`mret      `](rv.c#L642)[`mul       `](rv.c#L590)[`mulh      `](rv.c#L590)[`mulhsu    `](rv.c#L590)[`mulhu     `](rv.c#L590)[`or        `](rv.c#L583)
- [`ori       `](rv.c#L583)[`rem       `](rv.c#L607)[`remu      `](rv.c#L609)[`sc.w      `](rv.c#L517)[`sfence.vma`](rv.c#L659)[`sll       `](rv.c#L573)[`slli      `](rv.c#L573)[`slt       `](rv.c#L575)
- [`slti      `](rv.c#L575)[`sltiu     `](rv.c#L577)[`sltu      `](rv.c#L577)[`sra       `](rv.c#L581)[`srai      `](rv.c#L581)[`sret      `](rv.c#L642)[`srl       `](rv.c#L581)[`srli      `](rv.c#L581)
- [`sub       `](rv.c#L568)[`wfi       `](rv.c#L658)[`xor       `](rv.c#L579)[`xori      `](rv.c#L579)

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
