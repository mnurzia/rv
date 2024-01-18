/* RISC-V Platform-Level Interrupt Controller implementation
 * see: https://github.com/riscv/riscv-plic-spec */

#ifndef RV_PLIC_H
#define RV_PLIC_H

#include "rv.h"

#define RV_PLIC_NSRC 256
#define RV_PLIC_NCTX 1

typedef struct rv_plic {
  rv_u32 priority[RV_PLIC_NSRC];
  rv_u32 pending[RV_PLIC_NSRC / 32];
  rv_u32 enable[RV_PLIC_NSRC / 32 * RV_PLIC_NCTX];
  rv_u32 thresh[RV_PLIC_NCTX];
  rv_u32 claim[RV_PLIC_NCTX];
  rv_u32 claiming[RV_PLIC_NSRC / 32]; /* interrupts with claiming in progress */
} rv_plic;

/* initialize the PLIC */
void rv_plic_init(rv_plic *plic);

#define RV_PLIC_SIZE /* size of memory map */ 0x4000000

/* perform a bus access on the plic */
rv_res rv_plic_bus(rv_plic *plic, rv_u32 addr, rv_u8 *data, rv_u32 is_store,
                   rv_u32 width);

/* request an interrupt with the given interrupt source */
rv_res rv_plic_irq(rv_plic *plic, rv_u32 source);

/* returns 1 if an external interrupt needs servicing by the given hart */
rv_u32 rv_plic_mei(rv_plic *plic, rv_u32 context);

#endif /* RV_PLIC_H */
