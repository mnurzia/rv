#include "machine.h"

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "rv_gdb.h"

rv_res load_cb(void *user, rv_u32 addr) {
  machine *m = (machine *)user;
  if (addr >= 0x80000000 && addr <= 0xBFFFFFFF) {
    return m->rom[(addr - 0x80000000) & ~(rv_u32)(0x10000)];
  } else if (addr >= 0x40000000 && addr <= 0x40FFFFFF) {
    return m->ram[addr - 0x40000000];
  }
  return RV_BAD;
}

rv_res store_cb(void *user, rv_u32 addr, rv_u8 data) {
  machine *m = (machine *)user;
  if (addr >= 0x80000000 && addr <= 0xBFFFFFFF) {
    return RV_BAD;
  } else if (addr >= 0x40000000 && addr <= 0x40FFFFFF) {
    m->ram[addr - 0x40000000] = data;
    return 0;
  }
  return RV_BAD;
}

int machine_init(machine *m, const char *rom) {
  int fd = open(rom, O_RDONLY);
  rv_u32 i;
  m->rom = NULL;
  m->ram = NULL;
  for (i = 0; i < 4; i++) {
    rv_init(&m->cpus[i], (void *)m, load_cb, store_cb);
    m->cpus[i].csrs.mhartid = i;
  }
  m->rom = malloc(sizeof(rv_u8) * 0x10000);
  if (!m->rom)
    return RV_GDB_ERR_NOMEM;
  m->ram = malloc(sizeof(rv_u8) * 0x1000000);
  if (!m->ram)
    return RV_GDB_ERR_NOMEM;
  memset(m->rom, 0, 0x10000);
  memset(m->ram, 0, 0x1000000);
  read(fd, m->rom, 0x10000);
  rv_gdb_init(&m->gdb);
  return 0;
}

void machine_destroy(machine *m) {
  if (m->rom)
    free(m->rom);
  if (m->ram)
    free(m->ram);
  rv_gdb_destroy(&m->gdb);
}
