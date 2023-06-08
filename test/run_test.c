#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../rv.h"

void die(const char *msg) {
  printf("%s\n", msg);
  exit(1);
}

rv_u8 mem[0x10000];

rv_res load_cb(void *user, rv_u32 addr, rv_u8 *data) {
  if (addr < 0x80000000 || addr >= 0x80000000 + sizeof(mem))
    return RV_BAD;
  (void)user;
  *data = mem[addr - 0x80000000];
  return 0;
}

rv_res store_cb(void *user, rv_u32 addr, rv_u8 val) {
  if (addr < 0x80000000 || addr >= 0x80000000 + sizeof(mem))
    return RV_BAD;
  (void)user;
  mem[addr - 0x80000000] = val;
  return 0;
}

int main(int argc, const char **argv) {
  FILE *f;
  rv cpu;
  if (argc < 2)
    die("expected test name");
  f = fopen(argv[1], "r");
  (void)argc;
  memset(mem, 0, sizeof(mem));
  fread(mem, 1, sizeof(mem), f);
  rv_init(&cpu, NULL, &load_cb, &store_cb);
  while (1) {
    rv_u32 v = rv_inst(&cpu);
    if (v == RV_EECALL) {
      return (cpu.r[17] == 93 && !cpu.r[10]) ? EXIT_SUCCESS : EXIT_FAILURE;
    } else if (v && v != RV_EILL) {
      printf("%08X\n", v);
      return EXIT_FAILURE;
    }
  }
  return 0;
}
