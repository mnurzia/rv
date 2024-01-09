#ifndef RV_CLINT_H
#define RV_CLINT_H

#include "rv.h"

typedef struct rv_clint {
  rv *cpu;
  rv_u32 mswi, mtimecmp, mtimecmph;
  rv_u32 substep;
} rv_clint;

void rv_clint_init(rv_clint *clint, rv *cpu);

rv_res rv_clint_bus(rv_clint *clint, rv_u32 addr, rv_u32 *data, rv_u32 store);
rv_u32 rv_clint_msi(rv_clint *clint, rv_u32 context);
rv_u32 rv_clint_mti(rv_clint *clint, rv_u32 context);

#endif /* RV_CLINT_H */
