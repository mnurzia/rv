# rv

Single header RISC-V emulator written in C89.

Features:
- `RV32IMC` implementation
- Passes all supported tests in [`riscv-tests`](https://github.com/riscv/riscv-tests)
- ~600 lines of code
- Doesn't use any datatypes larger than 32 bits
- Simple API
- No memory allocations

## Usage

```c
#include "rv.h"

rv_res load_cb(void* user, rv_u32 addr, rv_u8* data) {
  if (addr - 0x80000000 > sizeof mem) /* Reset vector is 0x80000000 */
    return RV_BAD;
  *data = (rv_u8*)(user)[addr - 0x80000000];
  return RV_OK;
}

rv_res store_cb(void* user, rv_u32 addr, rv_u8 data) {
  if (addr - 0x80000000 > sizeof mem)
    return RV_BAD;
  (rv_u8*)(user)[addr - 0x80000000] = data;
  return RV_OK;
}

int main(void) {
  rv_u8 mem[0x10000];
  rv_cpu cpu;
  rv_init(&cpu, (void*)mem, &load_cb, &store_cb);
  while (rv_inst(&cpu) != RV_EECALL) {
    /* handle environment call */
    printf("Syscall number: %08X\n", cpu.r[17]);
  }
}
```

## Instructions

Click an instruction to see its implementation.

- [`add       `](rv.c#L426)[`addi      `](rv.c#L426)[`and       `](rv.c#L440)[`andi      `](rv.c#L440)
- [`auipc     `](rv.c#L512)[`beq       `](rv.c#L382)[`bge       `](rv.c#L385)[`bgtu      `](rv.c#L387)
- [`blt       `](rv.c#L384)[`bltu      `](rv.c#L386)[`bne       `](rv.c#L383)[`c.add     `](rv.c#L297)
- [`c.addi    `](rv.c#L238)[`c.addi16sp`](rv.c#L245)[`c.and     `](rv.c#L266)[`c.andi    `](rv.c#L257)
- [`c.beqz    `](rv.c#L276)[`c.bnez    `](rv.c#L278)[`c.j       `](rv.c#L274)[`c.jal     `](rv.c#L240)
- [`c.jalr    `](rv.c#L294)[`c.jr      `](rv.c#L289)[`c.li      `](rv.c#L242)[`c.lui     `](rv.c#L247)
- [`c.lw      `](rv.c#L230)[`c.lwsp    `](rv.c#L286)[`c.mv      `](rv.c#L291)[`c.or      `](rv.c#L264)
- [`c.slli    `](rv.c#L284)[`c.srai    `](rv.c#L255)[`c.srli    `](rv.c#L253)[`c.sub     `](rv.c#L260)
- [`c.sw      `](rv.c#L232)[`c.swsp    `](rv.c#L299)[`c.xor     `](rv.c#L262)[`csrrc     `](rv.c#L488)
- [`csrrci    `](rv.c#L488)[`csrrs     `](rv.c#L482)[`csrrsi    `](rv.c#L482)[`csrrw     `](rv.c#L473)
- [`csrrwi    `](rv.c#L473)[`div       `](rv.c#L458)[`divu      `](rv.c#L460)[`ebreak    `](rv.c#L501)
- [`ecall     `](rv.c#L498)[`fence     `](rv.c#L404)[`fence.i   `](rv.c#L408)[`jal       `](rv.c#L412)
- [`jalr      `](rv.c#L397)[`lb        `](rv.c#L332)[`lbu       `](rv.c#L340)[`lh        `](rv.c#L335)
- [`lhu       `](rv.c#L343)[`lui       `](rv.c#L514)[`lw        `](rv.c#L338)[`mret      `](rv.c#L496)
- [`mul       `](rv.c#L445)[`mulh      `](rv.c#L445)[`mulhsu    `](rv.c#L445)[`mulhu     `](rv.c#L445)
- [`or        `](rv.c#L438)[`ori       `](rv.c#L438)[`rem       `](rv.c#L462)[`remu      `](rv.c#L464)
- [`sb        `](rv.c#L363)[`sh        `](rv.c#L365)[`sll       `](rv.c#L428)[`slli      `](rv.c#L428)
- [`slt       `](rv.c#L430)[`slti      `](rv.c#L430)[`sltiu     `](rv.c#L432)[`sltu      `](rv.c#L432)
- [`sra       `](rv.c#L436)[`srai      `](rv.c#L436)[`srl       `](rv.c#L436)[`srli      `](rv.c#L436)
- [`sub       `](rv.c#L426)[`sw        `](rv.c#L367)[`xor       `](rv.c#L434)[`xori      `](rv.c#L434)

## CSRs

- [`mcause  `](rv.c#L112)[`mepc    `](rv.c#L110)[`mhartid `](rv.c#L100)[`mie     `](rv.c#L104)
- [`mstatus `](rv.c#L106)[`mstatush`](rv.c#L108)[`mtvec   `](rv.c#L102)

## Instruction quadrants
- [`00/000: LOAD    `](rv.c#L324)[`00/011: MISC-MEM`](rv.c#L403)[`00/100: OP-IMM  `](rv.c#L417)[`00/101: AUIPC   `](rv.c#L511)
- [`01/000: STORE   `](rv.c#L356)[`01/100: OP      `](rv.c#L418)[`01/101: LUI     `](rv.c#L513)[`11/000: BRANCH  `](rv.c#L377)
- [`11/001: JALR    `](rv.c#L396)[`11/011: JAL     `](rv.c#L411)[`11/100: SYSTEM  `](rv.c#L470)
