#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef unsigned char rv_u8;
typedef unsigned int rv_u32;

typedef rv_u8 (*rv_load_cb)(void *user, rv_u32 addr);
typedef void (*rv_store_cb)(void *user, rv_u32 addr, rv_u8 data);

typedef struct rv {
  rv_load_cb load_cb;
  rv_store_cb store_cb;
  rv_u32 r[32];
  rv_u32 ip;
  void *user;
} rv;

void rv_init(rv *cpu, void *user, rv_load_cb load_cb, rv_store_cb store_cb) {
  cpu->user = user;
  cpu->load_cb = load_cb;
  cpu->store_cb = store_cb;
  cpu->ip = 0x200;
  memset(cpu->r, 0, sizeof(cpu->r));
}

void rv_dump(rv *cpu) {
  int i, y;
  printf("ip=%08X\n", cpu->ip);
  for (i = 0; i < 8; i++) {
    for (y = 0; y < 4; y++)
      printf("x%02d=%08X ", 8 * y + i, cpu->r[8 * y + i]);
    printf("\n");
  }
}

rv_u8 load_cb(void *user, rv_u32 addr) {
  assert(addr >= 0x200 && addr < 0x10000);
  return ((rv_u8 *)user)[addr];
}

void store_cb(void *user, rv_u32 addr, rv_u8 data) {
  assert(addr >= 0x200 && addr < 0x10000);
  ((rv_u8 *)user)[addr] = data;
}

rv_u8 rv_lb(rv *cpu, rv_u32 addr) { return cpu->load_cb(cpu->user, addr); }

rv_u32 rv_lh(rv *cpu, rv_u32 addr) {
  return (rv_u32)rv_lb(cpu, addr) | ((rv_u32)rv_lb(cpu, addr + 1) << 8);
}

rv_u32 rv_lw(rv *cpu, rv_u32 addr) {
  return rv_lh(cpu, addr) | (rv_lh(cpu, addr + 2) << 16);
}

void rv_sb(rv *cpu, rv_u32 addr, rv_u8 data) {
  cpu->store_cb(cpu->user, addr, data);
}

void rv_sh(rv *cpu, rv_u32 addr, rv_u32 data) {
  rv_sb(cpu, addr, (rv_u8)(data & 0xFF));
  rv_sb(cpu, addr + 1, (rv_u8)(data >> 8));
}

void rv_sw(rv *cpu, rv_u32 addr, rv_u32 data) {
  rv_sh(cpu, addr, data & 0xFFFF);
  rv_sh(cpu, addr, data >> 16);
}

#define rv_signext(b, l) ((0 - (b)) << l)

#define rv_ibf(i, h, l) (((i) >> (l)) & ((1 << (h - l + 1)) - 1))
#define rv_ib(i, l) rv_ibf(i, l, l)
#define rv_ioph(i) rv_ibf(i, 6, 5)
#define rv_iopl(i) rv_ibf(i, 4, 2)
#define rv_if3(i) rv_ibf(i, 14, 12)
#define rv_ird(i) rv_ibf(i, 11, 7)
#define rv_irs1(i) rv_ibf(i, 19, 15)
#define rv_irs2(i) rv_ibf(i, 24, 20)
#define rv_iimm_i(i) (rv_signext(rv_ib(i, 31), 11) | rv_ibf(i, 30, 20))
#define rv_iimm_s(i)                                                           \
  (rv_signext(rv_ib(i, 31), 11) | rv_ibf(i, 30, 25) << 5 | rv_ibf(i, 11, 7))
#define rv_iimm_u(i) (rv_ibf(i, 31, 12) << 12)
#define rv_isz(i) (rv_ibf(i, 1, 0) + 1)
#define rv_ij(i)                                                               \
  (rv_signext(rv_ib(i, 31), 20) | rv_ibf(i, 19, 12) << 12 |                    \
   rv_ib(i, 20) << 11 | rv_ibf(i, 30, 21) << 1)

#define unimp() (rv_dump(cpu), assert(0 == "unimplemented instruction"))

rv_u32 rv_lr(rv *cpu, rv_u8 i) { return cpu->r[i]; }

void rv_sr(rv *cpu, rv_u8 i, rv_u32 v) {
  if (i)
    cpu->r[i] = v;
}

int rv_inst(rv *cpu) {
  /* fetch instruction */
  rv_u32 i = rv_lw(cpu, cpu->ip);
  rv_u32 next_ip = cpu->ip + rv_isz(i);
  if (rv_iopl(i) == 0) {
    if (rv_ioph(i) == 1) { /* 01/000: STORE */
      rv_u32 addr = rv_lr(cpu, rv_irs1(i)) + rv_iimm_s(i);
      if (rv_if3(i) == 0) { /* sw */
        rv_sw(cpu, addr, rv_irs2(i));
      } else if (rv_if3(i) == 1) { /* sh */
        rv_sh(cpu, addr, rv_irs2(i) & 0xFFFF);
      } else if (rv_if3(i) == 2) { /* sb */
        rv_sb(cpu, addr, rv_irs2(i) & 0xFF);
      } else {
        unimp();
      }
    } else {
      unimp();
    }
  } else if (rv_iopl(i) == 3) {
    if (rv_ioph(i) == 3) { /* 11/011: JAL */
      printf("ij %08X %08X\n", rv_ij(i), cpu->ip + rv_ij(i));
      rv_sr(cpu, rv_ird(i), next_ip); /* jal */
      next_ip = cpu->ip + rv_ij(i);
    } else {
      unimp();
    }
  } else if (rv_iopl(i) == 4) {
    if (rv_ioph(i) == 0) {  /* 00/100: OP-IMM */
      if (rv_if3(i) == 0) { /* addi */
        rv_sr(cpu, rv_ird(i), rv_lr(cpu, rv_irs1(i)) + rv_iimm_i(i));
      } else {
        unimp();
      }
    } else {
      unimp();
    }
  } else if (rv_iopl(i) == 5) {
    if (rv_ioph(i) == 1) {                 /* 01/101: LUI */
      rv_sr(cpu, rv_ird(i), rv_iimm_u(i)); /* lui */
    } else {
      unimp();
    }
  } else {
    unimp();
  }
  cpu->ip = next_ip;
  return 0;
}

int main(int argc, const char **argv) {
  const char *bn = "bin/test_prog.bin";
  rv_u8 *mem = malloc(sizeof(rv_u8) * 0x10000);
  int fd = open(bn, O_RDONLY);
  rv cpu;
  (void)(argc);
  (void)(argv);
  assert(mem);
  memset(mem, 0, 0x10000);
  read(fd, mem + 0x200, 0x10000 - 0x200);
  rv_init(&cpu, (void *)mem, &load_cb, &store_cb);
  while (1) {
    rv_inst(&cpu);
  }
}
