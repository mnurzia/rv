#include "rt.h"

void rt_main(void) {
  if (rt_cpu() == 0) {
    rt_sw(R_DFBB, 0x40000000);
  } else {
    while (1) {
      rt_spin();
    }
  }
  rt_trap();
}
