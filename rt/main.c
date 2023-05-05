#include "rt.h"

void rt_main(void) {
  if (rt_cpu() == 0) {
    u32 i, j = 0;
    rt_sw(R_VBASE, 0x40000000);
    while (1) {
      for (i = 0; i < 640 * 480; i++) {
        rt_sw((u32 *)0x40000000 + i, j++);
      }
    }
  } else {
    while (1) {
      rt_spin();
    }
  }
  rt_trap();
}
