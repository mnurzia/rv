#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../rv.h"

void die(const char *msg) {
  printf("%s\n", msg);
  exit(1);
}

rv_u8 mem[0x10000];

rv_res bus_cb(void *user, rv_u32 addr, rv_u8 *data, rv_u32 store,
              rv_u32 width) {
  rv_u8 *ptr = mem + (addr - 0x80000000);
  (void)(user);
  if (addr < 0x80000000 || (addr + width) >= 0x80000000 + sizeof(mem))
    return RV_BAD;
  memcpy(store ? ptr : data, store ? data : ptr, width);
  return 0;
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
    if (!end)
      die("invalid number of instructions");
  }
  (void)argc;
  memset(mem, 0, sizeof(mem));
  fread(mem, 1, sizeof(mem), f);
  rv_init(&cpu, NULL, &bus_cb);
  while (1 && (!limit || ninstr++ < limit)) {
    rv_u32 v = rv_step(&cpu);
    if (v == RV_EMECALL || v == RV_EUECALL || v == RV_ESECALL) {
      if (cpu.r[17] == 93 && !cpu.r[10]) {
        return EXIT_SUCCESS;
      }
    }
  }
  return EXIT_FAILURE;
}
