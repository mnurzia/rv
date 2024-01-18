#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rv.h"

void die(const char *msg) {
  printf("%s\n", msg);
  exit(1);
}

rv_u8 mem[0x10000];

int done = 0;

rv_res bus_cb(void *user, rv_u32 addr, rv_u8 *data, rv_u32 store,
              rv_u32 width) {
  rv_u8 *ptr = mem + (addr - 0x80000000);
  (void)(user);
  if (addr < 0x80000000 || (addr + width) >= 0x80000000 + sizeof(mem)) {
    return RV_BAD;
  } else
    memcpy(store ? ptr : data, store ? data : ptr, width);
  return RV_OK;
}

void dump_cpu(rv *r) {
  rv_u32 i, j;
  printf("PC %08X\n", r->pc);
  for (i = 0; i < 8; i++)
    for (j = 0; j < 4; j++)
      printf("x%02d: %08X%s", i + j * 8, r->r[i + j * 8], j == 3 ? "\n" : "  ");
  printf("mstatus: %08X  mcause:  %08X  mtvec:   %08X\n", r->csr.mstatus,
         r->csr.mcause, r->csr.mtvec);
  printf("mip:     %08X  mie:     %08X  mtval:   %08X\n", r->csr.mip,
         r->csr.mie, r->csr.mtval);
  printf("priv:    %8X\n", r->priv);
}

int main(int argc, const char **argv) {
  FILE *f;
  rv cpu;
  unsigned long limit = 0, ninstr = 0;
  if (argc < 2)
    die("expected test name");
  f = fopen(argv[1], "r");
  if (!f)
    die("couldn't open test");
  if (argc == 3) {
    char *end;
    limit = strtoul(argv[2], &end, 10);
    if (!limit)
      die("invalid number of instructions");
  }
  (void)argc;
  memset(mem, 0, sizeof(mem));
  fread(mem, 1, sizeof(mem), f);
  rv_init(&cpu, NULL, &bus_cb);
  while (1 && (!limit || ninstr++ < limit)) {
    rv_u32 v = rv_step(&cpu);
    if ((v == RV_EUECALL || v == RV_ESECALL || v == RV_EMECALL) &&
        (cpu.r[3] == 1 && cpu.r[10] == 0))
      return EXIT_SUCCESS;
  }
  dump_cpu(&cpu);
  return EXIT_FAILURE;
}
