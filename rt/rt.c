#include "rt.h"

void rt_sw(u32 *addr, u32 v) { *addr = v; }

u32 rt_lw(u32 *addr) { return *addr; }
