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
  cpu->ip = 0x80000000;
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
  printf("(L) %08X ", addr);
  assert(addr >= 0x80000000 && addr < 0x80010000);
  printf("-> %02X\n", ((rv_u8 *)user)[addr - 0x80000000]);
  return ((rv_u8 *)user)[addr - 0x80000000];
}

void store_cb(void *user, rv_u32 addr, rv_u8 data) {
  printf("(S) %08X <- %02X\n", addr, data);
  assert(addr >= 0x200 && addr < 0x10000);
  ((rv_u8 *)user)[addr - 0x80000000] = data;
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
  rv_sh(cpu, addr + 2, data >> 16);
}

rv_u32 rv_signext(rv_u32 x, rv_u32 h) { return (0 - (x >> h)) << h | x; }

#define RV_SBIT 0x80000000
#define rv_sgn(x) (!!((rv_u32)(x)&RV_SBIT))
#define rv_ovf(a, b, y)                                                        \
  ((!((a)&RV_SBIT) && !((b)&RV_SBIT) && !!((y)&RV_SBIT)) ||                    \
   (!!((a)&RV_SBIT) && !!((b)&RV_SBIT) && !!((y)&RV_SBIT)))

#define rv_ibf(i, h, l) (((i) >> (l)) & ((1 << (h - l + 1)) - 1))
#define rv_ib(i, l) rv_ibf(i, l, l)
#define rv_ioph(i) rv_ibf(i, 6, 5)
#define rv_iopl(i) rv_ibf(i, 4, 2)
#define rv_if3(i) rv_ibf(i, 14, 12)
#define rv_ird(i) rv_ibf(i, 11, 7)
#define rv_irs1(i) rv_ibf(i, 19, 15)
#define rv_irs2(i) rv_ibf(i, 24, 20)
#define rv_iimm_i(i) rv_signext(rv_ibf(i, 31, 20), 11)
#define rv_iimm_iu(i) rv_ibf(i, 31, 20)
#define rv_iimm_s(i)                                                           \
  (rv_signext(rv_ibf(i, 31, 25), 6) << 5 | rv_ibf(i, 30, 25) << 5 |            \
   rv_ibf(i, 11, 7))
#define rv_iimm_u(i) (rv_ibf(i, 31, 12) << 12)
#define rv_iimm_b(i)                                                           \
  (rv_signext(rv_ib(i, 31), 0) << 12 | rv_ib(i, 7) << 11 |                     \
   rv_ibf(i, 30, 25) << 5 | rv_ibf(i, 11, 8) << 1)
#define rv_iimm_j(i)                                                           \
  (rv_signext(rv_ib(i, 31), 0) << 20 | rv_ibf(i, 19, 12) << 12 |               \
   rv_ib(i, 20) << 11 | rv_ibf(i, 30, 21) << 1)
#define rv_isz(i) (rv_ibf(i, 1, 0) + 1)

#define unimp() (rv_dump(cpu), assert(0 == "unimplemented instruction"))

rv_u32 rv_lr(rv *cpu, rv_u8 i) { return cpu->r[i]; }

void rv_sr(rv *cpu, rv_u8 i, rv_u32 v) {
  if (i)
    cpu->r[i] = v;
}

rv_u32 rv_lcsr(rv *cpu, rv_u32 csr) {
  printf("(LCSR) %04X\n", csr);
  if (csr == 0xF14) { /* mhartid */
    return 0;
  } else if (csr == 0x305) { /*mtvec */
    return 0;
  } else {
    unimp();
  }
}

void rv_scsr(rv *cpu, rv_u32 csr, rv_u32 v) {
  printf("(SCSR) %04X <- %08X\n", csr, v);
  if (csr == 0xF14) { /* mhartid */
    unimp();
  } else if (csr == 0x305) { /* mtvec */
  } else {
    unimp();
  }
  (void)v;
}

int rv_inst(rv *cpu) {
  /* fetch instruction */
  rv_u32 i = rv_lw(cpu, cpu->ip);
  rv_u32 next_ip = cpu->ip + rv_isz(i);
  if (rv_iopl(i) == 0) {
    if (rv_ioph(i) == 0) { /* 00/000: LOAD */
      rv_u32 addr = rv_lr(cpu, rv_irs1(i)) + rv_iimm_i(i);
      if (rv_if3(i) == 0) { /* lb */
        rv_sr(cpu, rv_ird(i), rv_signext(rv_lb(cpu, addr), 7));
      } else if (rv_if3(i) == 1) { /* lh */
        rv_sr(cpu, rv_ird(i), rv_signext(rv_lh(cpu, addr), 15));
      } else if (rv_if3(i) == 2) { /* lw */
        rv_sr(cpu, rv_ird(i), rv_lw(cpu, addr));
      } else if (rv_if3(i) == 4) { /* lbu */
        rv_sr(cpu, rv_ird(i), rv_lb(cpu, addr));
      } else if (rv_if3(i) == 5) { /* lhu */
        rv_sr(cpu, rv_ird(i), rv_lh(cpu, addr));
      } else {
        unimp();
      }
    } else if (rv_ioph(i) == 1) { /* 01/000: STORE */
      rv_u32 addr = rv_lr(cpu, rv_irs1(i)) + rv_iimm_s(i);
      if (rv_if3(i) == 0) { /* sb */
        rv_sb(cpu, addr, rv_lr(cpu, rv_irs2(i)) & 0xFF);
      } else if (rv_if3(i) == 1) { /* sh */
        rv_sh(cpu, addr, rv_lr(cpu, rv_irs2(i)) & 0xFFFF);
      } else if (rv_if3(i) == 2) { /* sw */
        rv_sw(cpu, addr, rv_lr(cpu, rv_irs2(i)));
      } else {
        unimp();
      }
    } else if (rv_ioph(i) == 3) { /* 11/000: BRANCH */
      rv_u32 a = rv_lr(cpu, rv_irs1(i)), b = rv_lr(cpu, rv_irs2(i));
      rv_u32 y = a - b;
      rv_u32 zero = !y, sgn = rv_sgn(y), ovf = rv_ovf(a, b, y), carry = y > a;
      rv_u32 add = rv_iimm_b(i);
      rv_u32 targ = cpu->ip + add;
      if ((rv_if3(i) == 0 && zero) ||         /* beq */
          (rv_if3(i) == 1 && !zero) ||        /* bne */
          (rv_if3(i) == 4 && (sgn != ovf)) || /* blt */
          (rv_if3(i) == 5 && (sgn == ovf)) || /* bge */
          (rv_if3(i) == 6 && carry) ||        /* bltu */
          (rv_if3(i) == 7 && !carry)          /* bgtu */
      ) {
        next_ip = targ;
      } else if (rv_if3(i) == 2 || rv_if3(i) == 3) {
        unimp();
      }
    } else {
      unimp();
    }
  } else if (rv_iopl(i) == 1) {
    if (rv_ioph(i) == 3 && rv_if3(i) == 0) { /* 11/001: JALR */
      rv_sr(cpu, rv_ird(i), next_ip);        /* jalr */
      next_ip = rv_lr(cpu, rv_irs1(i)) + rv_iimm_i(i);
    } else {
      unimp();
    }
  } else if (rv_iopl(i) == 3) {
    if (rv_ioph(i) == 3) {            /* 11/011: JAL */
      rv_sr(cpu, rv_ird(i), next_ip); /* jal */
      next_ip = cpu->ip + rv_iimm_j(i);
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
    } else if (rv_ioph(i) == 1) { /* 01/100: OP */
      if (rv_if3(i) == 0) {       /* add */
        rv_sr(cpu, rv_ird(i), rv_lr(cpu, rv_irs1(i)) + rv_lr(cpu, rv_irs2(i)));
      } else {
        unimp();
      }
    } else if (rv_ioph(i) == 3) { /* 11/100: SYSTEM */
      rv_u32 csr = rv_iimm_iu(i);
      rv_u32 s = rv_if3(i) & 4 ? rv_irs1(i) : rv_lr(cpu, rv_irs1(i)); /* uimm */
      if ((rv_if3(i) & 3) == 1) { /* csrrw / csrrwi */
        if (rv_ird(i))
          rv_sr(cpu, rv_ird(i), rv_lcsr(cpu, csr));
        rv_scsr(cpu, csr, s);
      } else if ((rv_if3(i) & 3) == 2) { /* csrrs / csrrsi */
        rv_u32 p = rv_lcsr(cpu, csr);
        rv_sr(cpu, rv_ird(i), p);
        if (rv_irs1(i))
          rv_scsr(cpu, csr, p | s);
      } else if ((rv_if3(i) & 3) == 3) { /* csrrc / csrrci */
        rv_u32 p = rv_lcsr(cpu, csr);
        rv_sr(cpu, rv_ird(i), p);
        if (rv_irs1(i))
          rv_scsr(cpu, csr, p & ~s);
      } else {
        unimp();
      }
    } else {
      unimp();
    }
  } else if (rv_iopl(i) == 5) {
    if (rv_ioph(i) == 0) {                           /* 00/101: AUIPC */
      rv_sr(cpu, rv_ird(i), rv_iimm_u(i) + cpu->ip); /* auipc */
    } else if (rv_ioph(i) == 1) {                    /* 01/101: LUI */
      rv_sr(cpu, rv_ird(i), rv_iimm_u(i));           /* lui */
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
  const char *bn = argv[1];
  rv_u8 *mem = malloc(sizeof(rv_u8) * 0x10000);
  int fd = open(bn, O_RDONLY);
  rv cpu;
  (void)(argc);
  (void)(argv);
  assert(mem);
  memset(mem, 0, 0x10000);
  read(fd, mem, 0x10000);
  rv_init(&cpu, (void *)mem, &load_cb, &store_cb);
  while (1) {
    rv_inst(&cpu);
  }
}
