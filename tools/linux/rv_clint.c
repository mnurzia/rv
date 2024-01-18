#include "rv_clint.h"

#include <string.h>

void rv_clint_init(rv_clint *clint, rv *cpu) {
  memset(clint, 0, sizeof(*clint));
  clint->cpu = cpu;
}

rv_res rv_clint_bus(rv_clint *clint, rv_u32 addr, rv_u8 *d, rv_u32 is_store,
                    rv_u32 width) {
  rv_u32 *reg, *data = (rv_u32 *)d;
  if (width != 4)
    return RV_BAD;
  if (addr == 0x0) /*R mswi */
    reg = &clint->mswi;
  else if (addr == 0x4000 + 0x0000) /*R mtimecmp */
    reg = &clint->mtimecmp;
  else if (addr == 0x4000 + 0x0000 + 4) /*R mtimecmph */
    reg = &clint->mtimecmph;
  else if (addr == 0x4000 + 0x7FF8) /*R mtime */
    reg = &clint->cpu->csr.mtime;
  else if (addr == 0x4000 + 0x7FF8 + 4) /*R mtimeh */
    reg = &clint->cpu->csr.mtimeh;
  else
    return RV_BAD;
  memcpy(is_store ? reg : data, is_store ? data : reg, 4);
  return RV_OK;
}

rv_u32 rv_clint_msi(rv_clint *clint, rv_u32 context) {
  (void)context; /* unused for now, perhaps add multicore support later */
  return clint->mswi & 1;
}

rv_u32 rv_clint_mti(rv_clint *clint, rv_u32 context) {
  (void)context;
  return (clint->cpu->csr.mtimeh > clint->mtimecmph) ||
         ((clint->cpu->csr.mtimeh == clint->mtimecmph) &&
          (clint->cpu->csr.mtime >= clint->mtimecmp));
}
