#include "rv_plic.h"

#include <stdio.h>
#include <string.h>

void rv_plic_init(rv_plic *plic) { memset(plic, 0, sizeof(*plic)); }

rv_res rv_plic_bus(rv_plic *plic, rv_u32 addr, rv_u8 *d, rv_u32 store,
                   rv_u32 width) {
  rv_u32 *reg = NULL, wmask = 0 - 1U, *data = (rv_u32 *)d;
  if (addr >= 0x4000000 || width != 4)
    return RV_BAD;
  else if (addr >= 0 &&
           addr < RV_PLIC_NSRC * 4) /*R Interrupt Source Priority */
    reg = plic->priority + (addr >> 2), wmask *= !!addr;
  else if (addr >= 0x1000 &&
           addr < 0x1000 + RV_PLIC_NSRC / 8) /*R Interrupt Pending Bits */
    reg = plic->pending + ((addr - 0x1000) >> 2), wmask ^= addr == 0x1000;
  else if (addr >= 0x2000 &&
           addr < 0x2000 + RV_PLIC_NSRC / 8) /*R Interrupt Enable Bits */
    reg = plic->enable + ((addr - 0x2000) >> 2), wmask ^= addr == 0x2000;
  else if (addr >> 12 >= 0x200 && (addr >> 12) < 0x200 + RV_PLIC_NCTX &&
           !(addr & 0xFFF)) /*R Priority Threshold */
    reg = plic->thresh + ((addr >> 12) - 0x200);
  else if (addr >> 12 >= 0x200 && (addr >> 12) < 0x200 + RV_PLIC_NCTX &&
           (addr & 0xFFF) == 4) /*R Interrupt Claim Register */ {
    rv_u32 context = (addr >> 12) - 0x200, en_off = context * RV_PLIC_NSRC / 32;
    reg = plic->claim + context;
    if (!store && *reg < RV_PLIC_NSRC) {
      if (plic->pending[*reg / 32] & (1U << *reg % 32))
        plic->claiming[*reg / 32 + en_off] |=
            1U << *reg % 32; /* set claiming bit */
    } else if (store && *data < RV_PLIC_NSRC) {
      plic->claiming[*data / 32 + en_off] &=
          ~(1U << *data % 32); /* unset claiming bit */
    }
  }
  if (reg) {
    if (!store)
      *data = *reg;
    else
      *reg = (*reg & ~wmask) | (*data & wmask);
  }
  return RV_OK;
}

rv_res rv_plic_irq(rv_plic *plic, rv_u32 source) {
  if (source > RV_PLIC_NSRC || !source ||
      ((plic->claiming[source / 32] >> (source % 32)) & 1U) ||
      ((plic->pending[source / 32] >> (source % 32)) & 1U))
    return RV_BAD;
  plic->pending[source / 32] |= 1U << source % 32;
  return RV_OK;
}

rv_u32 rv_plic_mei(rv_plic *plic, rv_u32 context) {
  rv_u32 i, j, o = 0, h = 0;
  for (i = 0; i < RV_PLIC_NSRC / 32; i++) {
    rv_u32 en_off = i + context * RV_PLIC_NSRC / 32;
    if (!((plic->enable[en_off] & plic->pending[i]) | plic->claiming[i]))
      continue;
    for (j = 0; j < 32; j++) {
      if ((plic->claiming[en_off] >> j) & 1U)
        plic->pending[i] &= ~(1U << j);
      else if (((plic->enable[i] >> j) & 1U) &&
               ((plic->pending[i] >> j) & 1U) &&
               plic->priority[i * 32 + j] >= h &&
               plic->priority[i * 32 + j] >= plic->thresh[context])
        o = i * 32 + j, h = plic->priority[i * 32 + j];
    }
  }
  plic->claim[context] = o;
  return !!o;
}
