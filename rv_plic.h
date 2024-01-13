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
  rv_u32 claiming[RV_PLIC_NSRC / 32];
} rv_plic;

void rv_plic_init(rv_plic *plic);

rv_res rv_plic_bus(rv_plic *plic, rv_u32 addr, rv_u32 *data, rv_u32 store);
rv_res rv_plic_irq(rv_plic *plic, rv_u32 source);
rv_u32 rv_plic_mei(rv_plic *plic, rv_u32 context);

#endif /* RV_PLIC_H */
