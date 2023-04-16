#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef unsigned char rv_u8;
typedef unsigned int rv_u32;

typedef rv_u8 (*rv_load_cb)(void* user, rv_u32 addr);
typedef void (*rv_store_cb)(void* user, rv_u32 addr, rv_u8 data);

typedef struct rv {
  rv_load_cb load_cb;
  rv_store_cb store_cb;
  rv_u32 r[32];
  rv_u32 ip;
  void* user;
} rv;

void rv_init(rv* cpu, void* user, rv_load_cb load_cb, rv_store_cb store_cb)
{
  cpu->user = user;
  cpu->load_cb = load_cb;
  cpu->store_cb = store_cb;
  cpu->ip = 0x200;
  memset(cpu->r, 0, sizeof(cpu->r));
}

void rv_dump(rv* cpu)
{
  int i, y;
  printf("ip=%08X\n", cpu->ip);
  for (i = 0; i < 8; i++) {
    for (y = 0; y < 4; y++)
      printf("x%02d=%08X ", 8 * y + i, cpu->r[8 * y + i]);
    printf("\n");
  }
}

rv_u8 load_cb(void* user, rv_u32 addr)
{
  assert(addr >= 0x200 && addr < 0x10000);
  return ((rv_u8*)user)[addr];
}

void store_cb(void* user, rv_u32 addr, rv_u8 data)
{
  assert(addr >= 0x200 && addr < 0x10000);
  ((rv_u8*)user)[addr] = data;
}

rv_u8 rv_lb(rv* cpu, rv_u32 addr) { return cpu->load_cb(cpu->user, addr); }

rv_u32 rv_lh(rv* cpu, rv_u32 addr)
{
  return (rv_u32)rv_lb(cpu, addr) | ((rv_u32)rv_lb(cpu, addr + 1) << 8);
}

rv_u32 rv_lw(rv* cpu, rv_u32 addr)
{
  return rv_lh(cpu, addr) | (rv_lh(cpu, addr + 2) << 16);
}

void rv_sb(rv* cpu, rv_u32 addr, rv_u8 data)
{
  cpu->store_cb(cpu->user, addr, data);
}

void rv_sh(rv* cpu, rv_u32 addr, rv_u32 data)
{
  rv_sb(cpu, addr, (rv_u8)(data & 0xFF));
  rv_sb(cpu, addr + 1, (rv_u8)(data >> 8));
}

void rv_sw(rv* cpu, rv_u32 addr, rv_u32 data)
{
  rv_sh(cpu, addr, data & 0xFFFF);
  rv_sh(cpu, addr, data >> 16);
}

#define rv_signext(b, l) ((0 - (b)) << l)

#define rv_ibf(i, h, l) (((i) >> (l)) & ((1 << (h - l + 1)) - 1))
#define rv_ib(i, l)     rv_ibf(i, l, l)
#define rv_iop(i)       rv_ibf(i, 1, 0) /* op */
#define rv_if3(i)       rv_ibf(i, 15, 13) /* funct3 */
#define rv_ir1(i)       rv_ibf(i, 11, 7) /* rs1/rs1'/rd' */
#define rv_ir2(i)       rv_ibf(i, 5, 2) /* rs2 */
#define rv_inzimm5(i)                                                          \
  (rv_signext(rv_ib(i, 12), 5) |                                               \
   rv_ibf(i, 6, 2)) /* 6-bit nzimm, sign-extended */
#define rv_ijt(i) /* jump target as in c.jal, fuck your muxes */               \
  (rv_signext(rv_ib(i, 11), 10) | rv_ib(i, 4) << 9 | rv_ibf(i, 9, 8) << 7 |    \
   rv_ib(i, 10) << 6 | rv_ib(i, 6) << 5 | rv_ib(i, 7) << 4 |                   \
   rv_ibf(i, 3, 1) << 2 | rv_ib(i, 5) << 1)
#define unimp() (rv_dump(cpu), assert(0 == "unimplemented instruction"))

rv_u32 rv_lr(rv* cpu, rv_u8 i) { return cpu->r[i]; }

void rv_sr(rv* cpu, rv_u8 i, rv_u32 v)
{
  if (i)
    cpu->r[i] = v;
}

int rv_inst(rv* cpu)
{
  /* fetch instruction */
  rv_u32 i = rv_lh(cpu, cpu->ip);
  if (rv_iop(i) == 0) {
    unimp();
  } else if (rv_iop(i) == 1) {
    if (rv_if3(i) == 0) { /* c.nop, c.addi */
      rv_sr(cpu, rv_ir1(i), rv_lr(cpu, rv_ir1(i)) + rv_inzimm5(i));
    } else if (rv_if3(i) == 1) { /* c.jal */
      rv_sr(cpu, 1, cpu->ip + 2);
      printf("jt %08x\n", rv_ijt(i));
      cpu->ip += rv_ijt(i) - 2;
    } else if (rv_if3(i) == 2) { /* c.li */
      rv_sr(cpu, rv_ir1(i), rv_inzimm5(i));
    } else {
      unimp();
    }
  } else if (rv_iop(i) == 2) {
    if (rv_if3(i) == 4) {
      if (!rv_ib(i, 12)) {
        if (!rv_ir2(i)) { /* c.jr */
          cpu->ip = rv_lr(cpu, rv_ir1(i)) - 2;
        } else {
          unimp();
        }
      } else if (rv_if3(i) == 6) { /* c.swsp */
        rv_sw(
            cpu,
            rv_lr(cpu, 2) + (rv_ibf(i, 12, 10) << 2 | rv_ibf(i, 8, 7) << 5),
            rv_lr(cpu, rv_ir2(i)));
      } else {
        unimp();
      }
    } else {
      unimp();
    }
  } else if (rv_iop(i) == 3) {
    unimp();
  }
  cpu->ip += 2;
  return 0;
}

int main(int argc, const char** argv)
{
  const char* bn = "test_prog.bin";
  rv_u8* mem = malloc(sizeof(rv_u8) * 0x10000);
  int fd = open(bn, O_RDONLY);
  rv cpu;
  (void)(argc);
  (void)(argv);
  assert(mem);
  memset(mem, 0, 0x10000);
  read(fd, mem + 0x200, 0x10000 - 0x200);
  rv_init(&cpu, (void*)mem, &load_cb, &store_cb);
  while (1) {
    rv_inst(&cpu);
  }
}
