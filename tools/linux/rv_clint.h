/* RISC-V Core-Local Interruptor implementation
 * see: https://github.com/riscv/riscv-aclint */

#ifndef RV_CLINT_H
#define RV_CLINT_H

#include "rv.h"

#define RV_CLINT_SIZE /* size of memory map */ 0x10000

typedef struct rv_clint {
  rv *cpu;
  rv_u32 mswi, mtimecmp, mtimecmph;
} rv_clint;

/* initialize the interruptor for a given cpu */
void rv_clint_init(rv_clint *clint, rv *cpu);

/* perform a bus access on the interruptor */
rv_res rv_clint_bus(rv_clint *clint, rv_u32 addr, rv_u8 *data, rv_u32 store,
                    rv_u32 width);

/* returns 1 if a machine software interrupt is occurring */
rv_u32 rv_clint_msi(rv_clint *clint, rv_u32 context);

/* returns 1 if a machine timer interrupt is occurring */
rv_u32 rv_clint_mti(rv_clint *clint, rv_u32 context);

#endif /* RV_CLINT_H */
