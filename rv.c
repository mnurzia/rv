/* rv32uic */
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef unsigned char rv_u8;
typedef unsigned int rv_u32;
typedef unsigned long rv_res;

typedef rv_res (*rv_load_cb)(void *user, rv_u32 addr);
typedef rv_res (*rv_store_cb)(void *user, rv_u32 addr, rv_u8 data);

typedef struct rv_csrs {
  rv_u32 mstatus;
  rv_u32 mstatush;
  rv_u32 mscratch;
  rv_u32 mepc;
  rv_u32 mcause;
  rv_u32 mtval;
  rv_u32 mip;
  rv_u32 mtinst;
  rv_u32 mtval2;
  rv_u32 mtvec;
  rv_u32 mie;
} rv_csrs;

typedef struct rv {
  rv_load_cb load_cb;
  rv_store_cb store_cb;
  rv_u32 r[32];
  rv_u32 ip;
  void *user;
  rv_csrs csrs;
} rv;

void rv_init(rv *cpu, void *user, rv_load_cb load_cb, rv_store_cb store_cb) {
  cpu->user = user;
  cpu->load_cb = load_cb;
  cpu->store_cb = store_cb;
  cpu->ip = 0x80000000;
  memset(cpu->r, 0, sizeof(cpu->r));
  memset(&cpu->csrs, 0, sizeof(cpu->csrs));
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

#define RV_BAD ((rv_res)1 << 32);
#define rv_isbad(x) (x >> 32)

rv_res load_cb(void *user, rv_u32 addr) {
  if (addr >= 0x80000000 && addr < 0x80010000) {
    return ((rv_u8 *)user)[addr - 0x80000000];
  } else {
    return RV_BAD;
  }
}

rv_res store_cb(void *user, rv_u32 addr, rv_u8 data) {
  if (addr >= 0x80000000 && addr < 0x80010000) {
    ((rv_u8 *)user)[addr - 0x80000000] = data;
    return 0;
  } else {
    return RV_BAD;
  }
}

rv_res rv_lb(rv *cpu, rv_u32 addr) { return cpu->load_cb(cpu->user, addr); }

rv_res rv_lh(rv *cpu, rv_u32 addr) {
  return (rv_u32)rv_lb(cpu, addr) | ((rv_u32)rv_lb(cpu, addr + 1) << 8);
}

rv_res rv_lw(rv *cpu, rv_u32 addr) {
  return rv_lh(cpu, addr) | (rv_lh(cpu, addr + 2) << 16);
}

rv_res rv_sb(rv *cpu, rv_u32 addr, rv_u8 data) {
  return cpu->store_cb(cpu->user, addr, data);
}

rv_res rv_sh(rv *cpu, rv_u32 addr, rv_u32 data) {
  return rv_sb(cpu, addr, (rv_u8)(data & 0xFF)) |
         rv_sb(cpu, addr + 1, (rv_u8)(data >> 8));
}

rv_res rv_sw(rv *cpu, rv_u32 addr, rv_u32 data) {
  return rv_sh(cpu, addr, data & 0xFFFF) | rv_sh(cpu, addr + 2, data >> 16);
}

rv_u32 rv_signext(rv_u32 x, rv_u32 h) { return (0 - (x >> h)) << h | x; }

#define RV_EIALIGN 0
#define RV_EIFAULT 1
#define RV_EILL 2
#define RV_EBP 3
#define RV_ELALIGN 4
#define RV_ELFAULT 5
#define RV_ESALIGN 6
#define RV_ESFAULT 7

#define RV_SBIT 0x80000000
#define rv_sgn(x) (!!((rv_u32)(x)&RV_SBIT))
#define rv_ovf(a, b, y) ((((a) ^ (b)) & RV_SBIT) && (((y) ^ (a)) & RV_SBIT))

#define rv_bf(i, h, l)                                                         \
  (((i) >> (l)) & ((1 << (h - l + 1)) - 1))           /* bit field             \
                                                       */
#define rv_b(i, l) rv_bf(i, l, l)                     /* bit */
#define rv_ioph(i) rv_bf(i, 6, 5)                     /* opcode[6:5] */
#define rv_iopl(i) rv_bf(i, 4, 2)                     /* opcode[4:2] */
#define rv_if3(i) rv_bf(i, 14, 12)                    /* funct3 */
#define rv_if7(i) rv_bf(i, 31, 25)                    /* funct7 */
#define rv_ird(i) rv_bf(i, 11, 7)                     /* rd */
#define rv_irs1(i) rv_bf(i, 19, 15)                   /* rs1 */
#define rv_irs2(i) rv_bf(i, 24, 20)                   /* rs2 */
#define rv_iimm_i(i) rv_signext(rv_bf(i, 31, 20), 11) /* imm. for I-type */
#define rv_iimm_iu(i) rv_bf(i, 31, 20) /* z-ext'd. imm. for I-type */
#define rv_iimm_s(i)                                                           \
  (rv_signext(rv_bf(i, 31, 25), 6) << 5 | rv_bf(i, 30, 25) << 5 |              \
   rv_bf(i, 11, 7))                           /* imm. for S-type */
#define rv_iimm_u(i) (rv_bf(i, 31, 12) << 12) /* imm. for U-type */
#define rv_iimm_b(i)                                                           \
  (rv_signext(rv_b(i, 31), 0) << 12 | rv_b(i, 7) << 11 |                       \
   rv_bf(i, 30, 25) << 5 | rv_bf(i, 11, 8) << 1) /* imm. for B-type */
#define rv_iimm_j(i)                                                           \
  (rv_signext(rv_b(i, 31), 0) << 20 | rv_bf(i, 19, 12) << 12 |                 \
   rv_b(i, 20) << 11 | rv_bf(i, 30, 21) << 1)   /* imm. for J-type */
#define rv_isz(i) (rv_bf(i, 1, 0) == 3 ? 4 : 2) /* instruction size */

#define unimp() (rv_dump(cpu), assert(0 == "unimplemented instruction"))

rv_u32 rv_lr(rv *cpu, rv_u8 i) { return cpu->r[i]; } /* load register */

void rv_sr(rv *cpu, rv_u8 i, rv_u32 v) { /* store register */
  if (i)
    cpu->r[i] = v;
}

rv_res rv_lcsr(rv *cpu, rv_u32 csr) { /* load csr */
  printf("(LCSR) %04X\n", csr);
  if (csr == 0xF14) { /* mhartid */
    return 0;
  } else if (csr == 0x305) { /* mtvec */
    return cpu->csrs.mtvec;
  } else if (csr == 0x740) { /* mnscratch */
    return RV_BAD;
  } else if (csr == 0x741) { /* mnepc */
    return RV_BAD;
  } else if (csr == 0x742) { /* mncause */
    return RV_BAD;
  } else if (csr == 0x744) { /* mnstatus */
    return RV_BAD;
  } else if (csr == 0x180) { /* satp */
    return RV_BAD;
  } else if (csr >= 0x3A0 && csr <= 0x3EF) { /* pmp* */
    return RV_BAD;
  } else if (csr == 0x304) { /* mie */
    return cpu->csrs.mie;
  } else if (csr == 0x302) { /* medeleg */
    return RV_BAD;
  } else if (csr == 0x300) { /* mstatus */
    return cpu->csrs.mstatus;
  } else if (csr == 0x310) { /* mstatush */
    return cpu->csrs.mstatush;
  } else if (csr == 0x341) { /* mepc */
    return cpu->csrs.mepc;
  } else if (csr == 0x342) { /* mcause */
    return cpu->csrs.mcause;
  } else {
    unimp();
  }
}

rv_res rv_scsr(rv *cpu, rv_u32 csr, rv_u32 v) { /* store csr */
  printf("(SCSR) %04X <- %08X\n", csr, v);
  if (csr == 0xF14) { /* mhartid */
    unimp();
  } else if (csr == 0x305) { /* mtvec */
    cpu->csrs.mtvec = v;
  } else if (csr == 0x740) { /* mnscratch */
    return RV_BAD;
  } else if (csr == 0x741) { /* mnepc */
    return RV_BAD;
  } else if (csr == 0x742) { /* mncause */
    return RV_BAD;
  } else if (csr == 0x744) { /* mnstatus */
    return RV_BAD;
  } else if (csr == 0x180) { /* satp */
    return RV_BAD;
  } else if (csr >= 0x3A0 && csr <= 0x3EF) { /* pmp* */
    return RV_BAD;
  } else if (csr == 0x304) { /* mie */
    cpu->csrs.mie = v;
  } else if (csr == 0x302) { /* medeleg */
    return RV_BAD;
  } else if (csr == 0x300) { /* mstatus */
    cpu->csrs.mstatus = v & 0x807FF615;
  } else if (csr == 0x310) { /* mstatush */
    cpu->csrs.mstatush = v & 0x00000030;
  } else if (csr == 0x341) { /* mepc */
    cpu->csrs.mepc = v;
  } else if (csr == 0x342) { /* mcause */
    cpu->csrs.mcause = v;
  } else {
    unimp();
  }
  return 0;
}

rv_u32 rv_except(rv *cpu, rv_u32 cause) { /* set exception state */
  (void)cpu;
  printf("(E) %04X\n", cause);
  cpu->ip = (cpu->csrs.mtvec & (rv_u32)~1) + 4 * cause * (cpu->csrs.mtvec & 1);
  return cause + 1;
}

#define rv_cop(c) rv_bf(c, 1, 0)   /* c-inst. op */
#define rv_cf3(c) rv_bf(c, 15, 13) /* c-inst. funct3 */
#define rv_crp(r) (r + 8)          /* c-inst. register offsetter */
#define rv_cimm_ciw(c)                                                         \
  (rv_bf(c, 10, 7) << 6 | rv_bf(c, 12, 11) << 4 | rv_b(c, 6) << 3 |            \
   rv_b(c, 5) << 2) /* c-inst. CIW immediate for c.addi4spn */
#define rv_cimm_cl(c)                                                          \
  (rv_b(c, 5) << 6 | rv_bf(c, 12, 10) << 3 |                                   \
   rv_b(c, 6) << 2) /* c-inst. CL immediate for c.lw/c.sw */
#define rv_cimm_ci(c)                                                          \
  (rv_signext(rv_b(c, 12), 0) << 5 |                                           \
   rv_bf(c, 6, 2)) /* c-inst. CI immediate for c.addi/c.li/c.lui */
#define rv_cimm_ci_b(c)                                                        \
  (rv_signext(rv_b(c, 12), 0) << 9 | rv_bf(c, 4, 3) << 7 | rv_b(c, 5) << 6 |   \
   rv_b(c, 2) << 5 |                                                           \
   rv_b(c, 6) << 4) /* c-inst. CI immediate for c.addi16sp */
#define rv_cimm_ci_c(c)                                                        \
  (rv_bf(c, 3, 2) << 6 | rv_b(c, 12) << 5 |                                    \
   rv_bf(c, 6, 4) << 2) /* c-inst. CI immediate for c.lwsp */
#define rv_cimm_cj(c)                                                          \
  (rv_signext(rv_b(c, 12), 0) << 11 | rv_b(c, 8) << 10 |                       \
   rv_bf(c, 10, 9) << 8 | rv_b(c, 6) << 7 | rv_b(c, 7) << 6 |                  \
   rv_b(c, 2) << 5 | rv_b(c, 11) << 4 |                                        \
   rv_bf(c, 5, 3) << 1) /* c-inst. CJ immediate for c.jalr/c.j */
#define rv_cimm_cb(c)                                                          \
  (rv_signext(rv_b(c, 12), 0) << 8 | rv_bf(c, 6, 5) << 6 | rv_b(c, 2) << 5 |   \
   rv_bf(c, 11, 10) << 3 |                                                     \
   rv_bf(c, 4, 3) << 1) /* c-inst. CB immediate for c.beqz/c.bnez */
#define rv_cimm_css(c)                                                         \
  (rv_bf(c, 8, 7) << 6 | rv_bf(c, 12, 9)                                       \
                             << 2) /* c-inst CSS immediate for c.swsp*/

/* macros to make all instruction types */
#define rv_i_i(op, f3, rd, rs1, imm)                                           \
  ((imm) << 20 | (rs1) << 15 | (f3) << 12 | (rd) << 7 | (op) << 2 | 3)
#define rv_i_s(op, f3, rs1, rs2, imm)                                          \
  (rv_bf(imm, 11, 5) << 25 | (rs2) << 20 | (rs1) << 15 | (f3) << 12 |          \
   rv_bf(imm, 4, 0) << 7 | (op) << 2 | 3)
#define rv_i_u(op, rd, imm) ((imm) << 12 | (rd) << 7 | (op) << 2 | 3)
#define rv_i_r(op, f3, rd, rs1, rs2, f7)                                       \
  ((f7) << 25 | (rs2) << 20 | (rs1) << 15 | (f3) << 12 | (rd) << 7 |           \
   (op) << 2 | 3)
#define rv_i_j(op, rd, imm)                                                    \
  (rv_b(imm, 20) << 31 | rv_bf(imm, 10, 1) << 21 | rv_b(imm, 11) << 20 |       \
   rv_bf(imm, 19, 12) << 12 | (rd) << 7 | (op) << 2 | 3)
#define rv_i_b(op, f3, rs1, rs2, imm)                                          \
  (rv_b(imm, 12) << 31 | rv_bf(imm, 10, 5) << 25 | (rs2) << 20 | (rs1) << 15 | \
   (f3) << 12 | rv_bf(imm, 4, 1) << 8 | rv_b(imm, 11) << 7 | (op) << 2 | 3)

rv_u32 rv_cvtinst(rv *cpu, rv_u32 c) { /* convert compressed to regular inst. */
  (void)c;
  if (rv_cop(c) == 0) {
    if (rv_cf3(c) == 0 && c != 0) { /* c.addi4spn -> addi rd', x2, nzuimm */
      return rv_i_i(4, 0, rv_crp(rv_bf(c, 4, 2)), 2, rv_cimm_ciw(c));
    } else if (c == 0) { /* illegal */
      return 0;
    } else if (rv_cf3(c) == 2) { /* c.lw -> lw rd', offset(rs1') */
      return rv_i_i(0, 2, rv_crp(rv_bf(c, 4, 2)), rv_crp(rv_bf(c, 9, 7)),
                    rv_cimm_cl(c));
    } else if (rv_cf3(c) == 6) { /* c.sw -> sw rs2', offset(rs1') */
      return rv_i_s(8, 2, rv_crp(rv_bf(c, 9, 7)), rv_crp(rv_bf(c, 4, 2)),
                    rv_cimm_cl(c));
    } else {
      unimp();
    }
  } else if (rv_cop(c) == 1) {
    if (rv_cf3(c) == 0) { /* c.addi -> addi rd, rd, nzimm */
      return rv_i_i(4, 0, rv_bf(c, 11, 7), rv_bf(c, 11, 7), rv_cimm_ci(c));
    } else if (rv_cf3(c) == 1) { /* c.jal -> jal x1, offset */
      return rv_i_j(27, 1, rv_cimm_cj(c));
    } else if (rv_cf3(c) == 2) { /* c.li -> addi rd, x0, imm */
      return rv_i_i(4, 0, rv_bf(c, 11, 7), 0, rv_cimm_ci(c));
    } else if (rv_cf3(c) == 3) {  /* 01/011: LUI/ADDI16SP */
      if (rv_bf(c, 11, 7) == 2) { /* c.addi16sp -> addi x2, x2, nzimm */
        return rv_i_i(4, 0, 2, 2, rv_cimm_ci_b(c));
      } else if (rv_bf(c, 11, 7) != 0) { /* c.lui -> lui rd, nzimm */
        return rv_i_u(13, rv_bf(c, 11, 7), rv_cimm_ci(c));
      } else {
        unimp();
      }
    } else if (rv_cf3(c) == 4) {   /* 01/100: MISC-ALU */
      if (rv_bf(c, 11, 10) == 0) { /* c.srli -> srli rd', rd', shamt */
        return rv_i_r(4, 5, rv_crp(rv_bf(c, 9, 7)), rv_crp(rv_bf(c, 9, 7)),
                      rv_cimm_ci(c) & 0x1F, 0);
      } else if (rv_bf(c, 11, 10) == 1) { /* c.srai -> srai rd', rd', shamt */
        return rv_i_r(4, 5, rv_crp(rv_bf(c, 9, 7)), rv_crp(rv_bf(c, 9, 7)),
                      rv_cimm_ci(c) & 0x1F, 32);
      } else if (rv_bf(c, 11, 10) == 2) { /* c.andi -> andi rd', rd', imm */
        return rv_i_i(4, 7, rv_crp(rv_bf(c, 9, 7)), rv_crp(rv_bf(c, 9, 7)),
                      rv_cimm_ci(c));
      } else if (rv_bf(c, 11, 10) == 3) {
        if (rv_bf(c, 6, 5) == 0) { /* c.sub -> sub rd', rd', rs2' */
          return rv_i_r(12, 0, rv_crp(rv_bf(c, 9, 7)), rv_crp(rv_bf(c, 9, 7)),
                        rv_crp(rv_bf(c, 4, 2)), 32);
        } else if (rv_bf(c, 6, 5) == 1) { /* c.xor -> xor rd', rd', rs2' */
          return rv_i_r(12, 4, rv_crp(rv_bf(c, 9, 7)), rv_crp(rv_bf(c, 9, 7)),
                        rv_crp(rv_bf(c, 4, 2)), 0);
        } else if (rv_bf(c, 6, 5) == 2) { /* c.or -> or rd', rd', rs2' */
          return rv_i_r(12, 6, rv_crp(rv_bf(c, 9, 7)), rv_crp(rv_bf(c, 9, 7)),
                        rv_crp(rv_bf(c, 4, 2)), 0);
        } else if (rv_bf(c, 6, 5) == 3) { /* c.and -> and rd', rd', rs2' */
          return rv_i_r(12, 7, rv_crp(rv_bf(c, 9, 7)), rv_crp(rv_bf(c, 9, 7)),
                        rv_crp(rv_bf(c, 4, 2)), 0);
        } else {
          unimp();
        }
      } else {
        unimp();
      }
    } else if (rv_cf3(c) == 5) { /* c.j -> jal x0, offset */
      return rv_i_j(27, 0, rv_cimm_cj(c));
    } else if (rv_cf3(c) == 6) { /* c.beqz -> beq rs1' x0, offset */
      return rv_i_b(24, 0, rv_crp(rv_bf(c, 9, 7)), 0, rv_cimm_cb(c));
    } else if (rv_cf3(c) == 7) { /* c.bnez -> bne rs1' x0, offset */
      return rv_i_b(24, 1, rv_crp(rv_bf(c, 9, 7)), 0, rv_cimm_cb(c));
    } else {
      unimp();
    }
  } else if (rv_cop(c) == 2) {
    if (rv_cf3(c) == 0) { /* c.slli -> slli rd, rd, shamt */
      return rv_i_r(4, 1, rv_bf(c, 11, 7), rv_bf(c, 11, 7),
                    rv_cimm_ci(c) & 0x1F, 0);
    } else if (rv_cf3(c) == 2) { /* c.lwsp -> lw rd, offset(x2) */
      return rv_i_i(0, 2, rv_bf(c, 11, 7), 2, rv_cimm_ci_c(c));
    } else if (rv_cf3(c) == 4 && !rv_b(c, 12) &&
               !rv_bf(c, 6, 2)) { /* c.jr -> jalr x0, 0(rs1) */
      return rv_i_i(25, 0, 0, rv_bf(c, 11, 7), 0);
    } else if (rv_cf3(c) == 4 && !rv_b(c, 12)) { /* c.mv -> add rd, x0, rs2 */
      return rv_i_r(12, 0, rv_bf(c, 11, 7), 0, rv_bf(c, 6, 2), 0);
    } else if (rv_cf3(c) == 4 && rv_b(c, 12) && rv_bf(c, 11, 7) &&
               !rv_bf(c, 6, 2)) { /* c.jalr -> jalr x1, 0(rs1) */
      return rv_i_i(25, 0, 1, rv_bf(c, 11, 7), 0);
    } else if (rv_cf3(c) == 4 && rv_b(c, 12) && rv_bf(c, 11, 7) &&
               rv_bf(c, 6, 2)) { /* c.add -> add rd, rd, rs2 */
      return rv_i_r(12, 0, rv_bf(c, 11, 7), rv_bf(c, 11, 7), rv_bf(c, 6, 2), 0);
    } else if (rv_cf3(c) == 6) { /* c.swsp -> sw rs2, offset(x2) */
      return rv_i_s(8, 2, 2, rv_bf(c, 6, 2), rv_cimm_css(c));
    } else {
      unimp();
    }
  } else {
    unimp();
  }
  return 0;
}

rv_u32 rv_inst(rv *cpu) {
  /* fetch instruction */
  rv_res ires = rv_lw(cpu, cpu->ip);
  rv_u32 i = (rv_u32)ires;
  rv_u32 next_ip;
  if (rv_isbad(ires))
    printf("(IF) %08X -> fault\n", cpu->ip);
  else
    printf("(IF) %08X -> %08X\n", cpu->ip, i);
  if (rv_isbad(ires))
    return rv_except(cpu, RV_EIFAULT);
  if (cpu->ip == 0x80002230)
    printf("hit\n");
  next_ip = cpu->ip + rv_isz(i);
  if (rv_isz(i) != 4)
    i = rv_cvtinst(cpu, i & 0xFFFF);
  if (rv_iopl(i) == 0) {
    if (rv_ioph(i) == 0) { /* 00/000: LOAD */
      rv_u32 addr = rv_lr(cpu, rv_irs1(i)) + rv_iimm_i(i);
      rv_res res;
      rv_u32 v;
      static const char *load_types[] = {"b",  "h",  "w",  "XX",
                                         "bu", "hu", "XX", "XX"};
      printf("(L%s) %08X -> ", load_types[rv_if3(i)], addr);
      if (rv_if3(i) == 0) { /* lb */
        v = rv_signext((rv_u32)(res = rv_lb(cpu, addr)), 7);
      } else if (rv_if3(i) == 1) { /* lh */
        v = rv_signext((rv_u32)(res = rv_lh(cpu, addr)), 15);
      } else if (rv_if3(i) == 2) { /* lw */
        v = (rv_u32)(res = rv_lw(cpu, addr));
      } else if (rv_if3(i) == 4) { /* lbu */
        v = (rv_u32)(res = rv_lb(cpu, addr));
      } else if (rv_if3(i) == 5) { /* lhu */
        v = (rv_u32)(res = rv_lh(cpu, addr));
      } else {
        unimp();
      }
      if (rv_isbad(res))
        printf("fault\n");
      else
        printf("%08X\n", v);
      if (rv_isbad(res))
        return rv_except(cpu, RV_ELFAULT);
      else
        rv_sr(cpu, rv_ird(i), v);
    } else if (rv_ioph(i) == 1) { /* 01/000: STORE */
      rv_u32 addr = rv_lr(cpu, rv_irs1(i)) + rv_iimm_s(i);
      rv_res res;
      static const char *store_types[] = {"b",  "h",  "w",  "XX",
                                          "XX", "XX", "XX", "XX"};
      printf("(S%s) %08X <- %08X", store_types[rv_if3(i)], addr,
             rv_lr(cpu, rv_irs2(i)));
      if (rv_if3(i) == 0) { /* sb */
        res = rv_sb(cpu, addr, rv_lr(cpu, rv_irs2(i)) & 0xFF);
      } else if (rv_if3(i) == 1) { /* sh */
        res = rv_sh(cpu, addr, rv_lr(cpu, rv_irs2(i)) & 0xFFFF);
      } else if (rv_if3(i) == 2) { /* sw */
        res = rv_sw(cpu, addr, rv_lr(cpu, rv_irs2(i)));
      } else {
        unimp();
      }
      if (rv_isbad(res))
        printf("-> fault\n");
      else
        printf("\n");
      if (rv_isbad(res))
        return rv_except(cpu, RV_ESFAULT);
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
      rv_u32 target = (rv_lr(cpu, rv_irs1(i)) + rv_iimm_i(i)) & (~(rv_u32)1);
      rv_sr(cpu, rv_ird(i), next_ip); /* jalr */
      next_ip = target;
    } else {
      unimp();
    }
  } else if (rv_iopl(i) == 3) {
    if (rv_ioph(i) == 0) {  /* 00/011: MISC-MEM */
      if (rv_if3(i) == 0) { /* fence */
        rv_u32 fm = rv_bf(i, 31, 28);
        if (fm && fm != 16) /* fm != 0000/1000 */
          return rv_except(cpu, RV_EILL);
      } else if (rv_if3(i) == 1) { /* fence.i */
      } else {
        unimp();
      }
    } else if (rv_ioph(i) == 3) {     /* 11/011: JAL */
      rv_sr(cpu, rv_ird(i), next_ip); /* jal */
      next_ip = cpu->ip + rv_iimm_j(i);
    } else {
      unimp();
    }
  } else if (rv_iopl(i) == 4) {
    if (rv_ioph(i) == 0 || /* 00/100: OP-IMM */
        rv_ioph(i) == 1) { /* 01/100: OP */
      rv_u32 a = rv_lr(cpu, rv_irs1(i));
      rv_u32 b = rv_ioph(i) ? rv_lr(cpu, rv_irs2(i)) : rv_iimm_i(i);
      rv_u32 s = (rv_ioph(i) || rv_if3(i)) ? rv_b(i, 30) : 0, sh = b & 0x1F;
      rv_u32 y;
      if (rv_if3(i) == 0) { /* add, addi, sub */
        y = s ? a - b : a + b;
      } else if (rv_if3(i) == 1) { /* sll, slli */
        y = a << sh;
      } else if (rv_if3(i) == 2) { /* slt, slti */
        y = rv_ovf(a, b, a - b) != rv_sgn(a - b);
      } else if (rv_if3(i) == 3) { /* sltu, sltiu */
        y = (a - b) > a;
      } else if (rv_if3(i) == 4) { /* xor, xori */
        y = a ^ b;
      } else if (rv_if3(i) == 5) { /* srl, srli, sra, srai */
        y = (a >> sh) | (((rv_u32)0 - (s && (a & RV_SBIT))) << (0x1F - sh));
      } else if (rv_if3(i) == 6) { /* or, ori */
        y = a | b;
      } else { /* and, andi */
        y = a & b;
      }
      rv_sr(cpu, rv_ird(i), y);
    } else if (rv_ioph(i) == 3) { /* 11/100: SYSTEM */
      rv_u32 csr = rv_iimm_iu(i);
      rv_u32 s = rv_if3(i) & 4 ? rv_irs1(i) : rv_lr(cpu, rv_irs1(i)); /* uimm */
      rv_res res;
      if ((rv_if3(i) & 3) == 1) { /* csrrw / csrrwi */
        if (rv_irs1(i)) {
          res = rv_lcsr(cpu, csr);
          if (rv_isbad(res))
            return rv_except(cpu, RV_EILL);
          if (rv_ird(i))
            rv_sr(cpu, rv_ird(i), (rv_u32)res);
        }
        if (rv_isbad(rv_scsr(cpu, csr, s)))
          return rv_except(cpu, RV_EILL);
      } else if ((rv_if3(i) & 3) == 2) { /* csrrs / csrrsi */
        res = rv_lcsr(cpu, csr);
        if (rv_isbad(res))
          return rv_except(cpu, RV_EILL);
        rv_sr(cpu, rv_ird(i), (rv_u32)res);
        if (rv_irs1(i) && rv_isbad(rv_scsr(cpu, csr, (rv_u32)res | s)))
          return rv_except(cpu, RV_EILL);
      } else if ((rv_if3(i) & 3) == 3) { /* csrrc / csrrci */
        res = rv_lcsr(cpu, csr);
        if (rv_isbad(res))
          return rv_except(cpu, RV_EILL);
        rv_sr(cpu, rv_ird(i), (rv_u32)res);
        if (rv_irs1(i) && rv_isbad(rv_scsr(cpu, csr, (rv_u32)res & ~s)))
          return rv_except(cpu, RV_EILL);
      } else if (!rv_if3(i)) {
        if (!rv_ird(i)) {
          if (!rv_irs1(i) && rv_irs2(i) == 2 && rv_if7(i) == 24) { /* mret */
            next_ip = cpu->csrs.mepc;
          } else if (!rv_irs1(i) && !rv_irs2(i) && !rv_if7(i)) { /* ecall */
            printf("(ECALL) ");
            if (rv_lr(cpu, 17) == 93) {
              if (rv_lr(cpu, 3) == 1)
                printf("PASS!\n");
              else {
                printf("FAIL (%i)!\n", rv_lr(cpu, 10) >> 1);
                exit(EXIT_FAILURE);
              }
              return 0;
            }
          } else {
            unimp();
          }
        } else {
          unimp();
        }
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
  return 1;
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
  while (rv_inst(&cpu)) {
  }
}
