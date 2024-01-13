#include "rv_clint.h"

#include <string.h>

void rv_clint_init(rv_clint *clint, rv *cpu) {
  memset(clint, 0, sizeof(*clint));
  clint->cpu = cpu;
}

rv_res rv_clint_bus(rv_clint *clint, rv_u32 addr, rv_u32 *data, rv_u32 store) {
  rv_u32 *reg = NULL;
  if (addr == 0x0)
    reg = &clint->mswi;
  else if (addr == 0x4000 + 0x7FF8)
    reg = &clint->cpu->csrs.mtime;
  else if (addr == 0x4000 + 0x7FF8 + 4)
    reg = &clint->cpu->csrs.mtimeh;
  else if (addr == 0x4000 + 0x0000)
    reg = &clint->mtimecmp;
  else if (addr == 0x4000 + 0x0000 + 4)
    reg = &clint->mtimecmph;
  else
    return RV_BAD;
  if (reg) {
    if (store) {
      *reg = *data;
    } else
      *data = *reg;
  }
  return RV_OK;
}

rv_u32 rv_clint_msi(rv_clint *clint, rv_u32 context) {
  (void)context; /* unused for now, perhaps add multicore support later */
  return clint->mswi & 1;
}

rv_u32 rv_clint_mti(rv_clint *clint, rv_u32 context) {
  (void)context;
  return (clint->cpu->csrs.mtimeh > clint->mtimecmph) ||
         ((clint->cpu->csrs.mtimeh == clint->mtimecmph) &&
          (clint->cpu->csrs.mtime >= clint->mtimecmp));
}
