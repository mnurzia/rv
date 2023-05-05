#ifndef MACHINE_H
#define MACHINE_H

#include "ext/tinycthread.h"

#include "rv.h"
#include "rv_gdb.h"

typedef struct machine_regs {
  rv_u32 vbase;
} machine_regs;

typedef struct machine {
  rv cpus[4];
  rv_gdb gdb;
  rv_u8 *rom;
  rv_u8 *ram;
  mtx_t mtx_video;
  machine_regs regs;
} machine;

int machine_init(machine *m, const char *rom);
void machine_destroy(machine *m);

#endif /* MACHINE_H */
