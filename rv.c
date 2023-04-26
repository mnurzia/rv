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
  rv_u32 mhartid;
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

#define RV_ECALL 0x80000000
#define RV_EBREAK 0x80000000

#define RV_SBIT 0x80000000
#define rv_sgn(x) (!!((rv_u32)(x)&RV_SBIT))
#define rv_ovf(a, b, y) ((((a) ^ (b)) & RV_SBIT) && (((y) ^ (a)) & RV_SBIT))

#define rv_bf(i, h, l)                                                         \
  (((i) >> (l)) & ((1 << ((h) - (l) + 1)) - 1))       /* extract bit field */
#define rv_b(i, l) rv_bf(i, l, l)                     /* extract bit */
#define rv_tb(i, l, o) (rv_b(i, l) << (o))            /* translate bit */
#define rv_tbf(i, h, l, o) (rv_bf(i, h, l) << (o))    /* translate bit field */
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
  (rv_signext(rv_tbf(i, 31, 25, 5), 11) | rv_tbf(i, 30, 25, 5) |               \
   rv_bf(i, 11, 7))                        /* imm. for S-type */
#define rv_iimm_u(i) rv_tbf(i, 31, 12, 12) /* imm. for U-type */
#define rv_iimm_b(i)                                                           \
  (rv_signext(rv_tb(i, 31, 12), 12) | rv_tb(i, 7, 11) | rv_tbf(i, 30, 25, 5) | \
   rv_tbf(i, 11, 8, 1)) /* imm. for B-type */
#define rv_iimm_j(i)                                                           \
  (rv_signext(rv_tb(i, 31, 20), 20) | rv_tbf(i, 19, 12, 12) |                  \
   rv_tb(i, 20, 11) | rv_tbf(i, 30, 21, 1))     /* imm. for J-type */
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
    return cpu->csrs.mhartid;
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

#define rv_cop(c) rv_bf(c, 1, 0)           /* c. op */
#define rv_cf3(c) rv_bf(c, 15, 13)         /* c. funct3 */
#define rv_crp(r) (r + 8)                  /* c. register offsetter */
#define rv_cird(c) rv_bf(c, 11, 7)         /* c. ci-format rd/rs1  */
#define rv_cirpl(c) rv_crp(rv_bf(c, 4, 2)) /* c. rd'/rs2' (bits 4-2) */
#define rv_cirph(c) rv_crp(rv_bf(c, 9, 7)) /* c. rd'/rs1' (bits 9-7) */
#define rv_cimm_ciw(c)                                                         \
  (rv_tbf(c, 10, 7, 6) | rv_tbf(c, 12, 11, 4) | rv_tb(c, 6, 3) |               \
   rv_tb(c, 5, 2)) /* CIW imm. for c.addi4spn */
#define rv_cimm_cl(c)                                                          \
  (rv_tb(c, 5, 6) | rv_tbf(c, 12, 10, 3) |                                     \
   rv_tb(c, 6, 2)) /* CL imm. for c.lw/c.sw */
#define rv_cimm_ci(c)                                                          \
  (rv_signext(rv_tb(c, 12, 5), 5) |                                            \
   rv_bf(c, 6, 2)) /* CI imm. for c.addi/c.li/c.lui */
#define rv_cimm_ci_b(c)                                                        \
  (rv_signext(rv_tb(c, 12, 9), 9) | rv_tbf(c, 4, 3, 7) | rv_tb(c, 5, 6) |      \
   rv_tb(c, 2, 5) | rv_tb(c, 6, 4)) /* CI imm. for c.addi16sp */
#define rv_cimm_ci_c(c)                                                        \
  (rv_tbf(c, 3, 2, 6) | rv_tb(c, 12, 5) |                                      \
   rv_tbf(c, 6, 4, 2)) /* CI imm. for c.lwsp */
#define rv_cimm_cj(c)                                                          \
  (rv_signext(rv_tb(c, 12, 11), 11) | rv_tb(c, 8, 10) | rv_tbf(c, 10, 9, 8) |  \
   rv_tb(c, 6, 7) | rv_tb(c, 7, 6) | rv_tb(c, 2, 5) | rv_tb(c, 11, 4) |        \
   rv_tbf(c, 5, 3, 1)) /* CJ imm. for c.jalr/c.j */
#define rv_cimm_cb(c)                                                          \
  (rv_signext(rv_tb(c, 12, 8), 8) | rv_tbf(c, 6, 5, 6) | rv_tb(c, 2, 5) |      \
   rv_tbf(c, 11, 10, 3) | rv_tbf(c, 4, 3, 1)) /* CB imm. for c.beqz/c.bnez */
#define rv_cimm_css(c)                                                         \
  (rv_tbf(c, 8, 7, 6) | rv_tbf(c, 12, 9, 2)) /* CSS imm. for c.swsp */

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
      return rv_i_i(4, 0, rv_cirpl(c), 2, rv_cimm_ciw(c));
    } else if (c == 0) { /* illegal */
      return 0;
    } else if (rv_cf3(c) == 2) { /* c.lw -> lw rd', offset(rs1') */
      return rv_i_i(0, 2, rv_cirpl(c), rv_cirph(c), rv_cimm_cl(c));
    } else if (rv_cf3(c) == 6) { /* c.sw -> sw rs2', offset(rs1') */
      return rv_i_s(8, 2, rv_cirph(c), rv_cirpl(c), rv_cimm_cl(c));
    } else {
      unimp();
    }
  } else if (rv_cop(c) == 1) {
    if (rv_cf3(c) == 0) { /* c.addi -> addi rd, rd, nzimm */
      return rv_i_i(4, 0, rv_cird(c), rv_cird(c), rv_cimm_ci(c));
    } else if (rv_cf3(c) == 1) { /* c.jal -> jal x1, offset */
      return rv_i_j(27, 1, rv_cimm_cj(c));
    } else if (rv_cf3(c) == 2) { /* c.li -> addi rd, x0, imm */
      return rv_i_i(4, 0, rv_cird(c), 0, rv_cimm_ci(c));
    } else if (rv_cf3(c) == 3) { /* 01/011: LUI/ADDI16SP */
      if (rv_cird(c) == 2) {     /* c.addi16sp -> addi x2, x2, nzimm */
        return rv_i_i(4, 0, 2, 2, rv_cimm_ci_b(c));
      } else if (rv_cird(c) != 0) { /* c.lui -> lui rd, nzimm */
        return rv_i_u(13, rv_cird(c), rv_cimm_ci(c));
      } else {
        unimp();
      }
    } else if (rv_cf3(c) == 4) {   /* 01/100: MISC-ALU */
      if (rv_bf(c, 11, 10) == 0) { /* c.srli -> srli rd', rd', shamt */
        return rv_i_r(4, 5, rv_cirph(c), rv_cirph(c), rv_cimm_ci(c) & 0x1F, 0);
      } else if (rv_bf(c, 11, 10) == 1) { /* c.srai -> srai rd', rd', shamt */
        return rv_i_r(4, 5, rv_cirph(c), rv_cirph(c), rv_cimm_ci(c) & 0x1F, 32);
      } else if (rv_bf(c, 11, 10) == 2) { /* c.andi -> andi rd', rd', imm */
        return rv_i_i(4, 7, rv_cirph(c), rv_cirph(c), rv_cimm_ci(c));
      } else if (rv_bf(c, 11, 10) == 3) {
        if (rv_bf(c, 6, 5) == 0) { /* c.sub -> sub rd', rd', rs2' */
          return rv_i_r(12, 0, rv_cirph(c), rv_cirph(c), rv_cirpl(c), 32);
        } else if (rv_bf(c, 6, 5) == 1) { /* c.xor -> xor rd', rd', rs2' */
          return rv_i_r(12, 4, rv_cirph(c), rv_cirph(c), rv_cirpl(c), 0);
        } else if (rv_bf(c, 6, 5) == 2) { /* c.or -> or rd', rd', rs2' */
          return rv_i_r(12, 6, rv_cirph(c), rv_cirph(c), rv_cirpl(c), 0);
        } else if (rv_bf(c, 6, 5) == 3) { /* c.and -> and rd', rd', rs2' */
          return rv_i_r(12, 7, rv_cirph(c), rv_cirph(c), rv_cirpl(c), 0);
        } else {
          unimp();
        }
      } else {
        unimp();
      }
    } else if (rv_cf3(c) == 5) { /* c.j -> jal x0, offset */
      return rv_i_j(27, 0, rv_cimm_cj(c));
    } else if (rv_cf3(c) == 6) { /* c.beqz -> beq rs1' x0, offset */
      return rv_i_b(24, 0, rv_cirph(c), 0, rv_cimm_cb(c));
    } else if (rv_cf3(c) == 7) { /* c.bnez -> bne rs1' x0, offset */
      return rv_i_b(24, 1, rv_cirph(c), 0, rv_cimm_cb(c));
    } else {
      unimp();
    }
  } else if (rv_cop(c) == 2) {
    if (rv_cf3(c) == 0) { /* c.slli -> slli rd, rd, shamt */
      return rv_i_r(4, 1, rv_cird(c), rv_cird(c), rv_cimm_ci(c) & 0x1F, 0);
    } else if (rv_cf3(c) == 2) { /* c.lwsp -> lw rd, offset(x2) */
      return rv_i_i(0, 2, rv_cird(c), 2, rv_cimm_ci_c(c));
    } else if (rv_cf3(c) == 4 && !rv_b(c, 12) &&
               !rv_bf(c, 6, 2)) { /* c.jr -> jalr x0, 0(rs1) */
      return rv_i_i(25, 0, 0, rv_cird(c), 0);
    } else if (rv_cf3(c) == 4 && !rv_b(c, 12)) { /* c.mv -> add rd, x0, rs2 */
      return rv_i_r(12, 0, rv_cird(c), 0, rv_bf(c, 6, 2), 0);
    } else if (rv_cf3(c) == 4 && rv_b(c, 12) && rv_cird(c) &&
               !rv_bf(c, 6, 2)) { /* c.jalr -> jalr x1, 0(rs1) */
      return rv_i_i(25, 0, 1, rv_cird(c), 0);
    } else if (rv_cf3(c) == 4 && rv_b(c, 12) && rv_cird(c) &&
               rv_bf(c, 6, 2)) { /* c.add -> add rd, rd, rs2 */
      return rv_i_r(12, 0, rv_cird(c), rv_cird(c), rv_bf(c, 6, 2), 0);
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
  rv_u32 err = 0;
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
            err = RV_ECALL;
            /*
            printf("(ECALL) ");
            if (rv_lr(cpu, 17) == 93) {
              if (rv_lr(cpu, 3) == 1)
                printf("PASS!\n");
              else {
                printf("FAIL (%i)!\n", rv_lr(cpu, 10) >> 1);
                exit(EXIT_FAILURE);
              }
              return 0;
            }*/
          } else if (!rv_irs1(i) && rv_irs2(i) == 1 &&
                     !rv_if7(i)) { /* ebreak */
            err = RV_EBREAK;
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
  return err;
}

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define RV_GDB_ERR_NOMEM -1
#define RV_GDB_ERR_FMT -2
#define RV_GDB_ERR_CORRUPT -3
#define RV_GDB_ERR_EOF -4
#define RV_GDB_ERR_WRITE -5
#define RV_GDB_ERR_READ -6
#define RV_GDB_ERR_MTX -7

#define RV_GDB_STOP 0
#define RV_GDB_STEP 1
#define RV_GDB_CONTINUE 2

typedef struct rv_gdb_hart rv_gdb_hart;

struct rv_gdb_hart {
  rv *cpu;
  rv_u32 state;
  rv_u32 next_state;
  rv_u32 except;
  rv_u32 need_send;
  const char *tid;
};

typedef struct rv_gdb {
  rv_gdb_hart *harts;
  rv_u32 harts_sz;
  rv_u32 hart_idx;
  rv_u8 *rx_pre;
  rv_u32 rx_pre_ptr;
  rv_u32 rx_pre_sz;
  rv_u8 *rx;
  rv_u32 rx_ptr;
  rv_u32 rx_sz;
  rv_u32 rx_alloc;
  rv_u8 *tx;
  rv_u32 tx_sz;
  rv_u32 tx_alloc;
  rv_u32 *bp;
  rv_u32 bp_sz;
  rv_u32 tx_seq;
  rv_u32 tx_ack;
  rv_u32 rx_seq;
  rv_u32 rx_ack;
  int sock;
} rv_gdb;

#define RV_GDB_BUF_ALLOC 1024

int rv_gdb_init(rv_gdb *gdb, int sock) {
  assert(sock);
  gdb->harts = NULL;
  gdb->harts_sz = 0;
  gdb->hart_idx = 0;
  gdb->rx_pre = NULL;
  gdb->rx_pre_ptr = 0;
  gdb->rx_pre_sz = 0;
  gdb->rx = NULL;
  gdb->rx_ptr = 0;
  gdb->rx_sz = 0;
  gdb->rx_alloc = 0;
  gdb->tx = NULL;
  gdb->tx_sz = 0;
  gdb->tx_alloc = 0;
  gdb->sock = sock;
  gdb->bp = NULL;
  gdb->bp_sz = 0;
  gdb->tx_seq = 1; /* to handle first '+' */
  gdb->tx_ack = 0;
  gdb->rx_seq = 0;
  gdb->rx_ack = 0;
  if (!(gdb->rx_pre = malloc(RV_GDB_BUF_ALLOC)))
    return RV_GDB_ERR_NOMEM;
  memset(gdb->rx_pre, 0, RV_GDB_BUF_ALLOC);
  return 0;
}

int rv_gdb_addhart(rv_gdb *gdb, rv *cpu, const char *tid) {
  rv_gdb_hart *harts =
      realloc(gdb->harts, (gdb->harts_sz + 1) * sizeof(rv_gdb_hart));
  if (!harts)
    return RV_GDB_ERR_NOMEM;
  gdb->harts = harts;
  gdb->harts[gdb->harts_sz].cpu = cpu;
  gdb->harts[gdb->harts_sz].tid = tid;
  gdb->harts[gdb->harts_sz].next_state = 0;
  gdb->harts[gdb->harts_sz].except = 0;
  gdb->harts[gdb->harts_sz].need_send = 0;
  gdb->harts[gdb->harts_sz++].state = RV_GDB_CONTINUE;
  return 0;
}

void rv_gdb_destroy(rv_gdb *gdb) {
  if (gdb->rx_pre)
    free(gdb->rx_pre);
  if (gdb->rx)
    free(gdb->rx);
  if (gdb->tx)
    free(gdb->tx);
  if (gdb->harts)
    free(gdb->harts);
  if (gdb->bp)
    free(gdb->bp);
}

int rv_gdb_recv(rv_gdb *gdb) {
  ssize_t rv;
reread:
  rv = read(gdb->sock, gdb->rx_pre, RV_GDB_BUF_ALLOC);
  assert(rv <= RV_GDB_BUF_ALLOC);
  if (rv <= 0) {
    if (errno == EINTR)
      goto reread;
    return RV_GDB_ERR_READ;
  }
  gdb->rx_pre_sz = (rv_u32)rv;
  gdb->rx_pre_ptr = 0;
  return 0;
}

int rv_gdb_in(rv_gdb *gdb, rv_u8 *out) {
  int err = 0;
  if (gdb->rx_pre_ptr == gdb->rx_pre_sz && (err = rv_gdb_recv(gdb)))
    return err;
  *out = gdb->rx_pre[gdb->rx_pre_ptr++];
  return err;
}

int rv_gdb_rx(rv_gdb *gdb, rv_u8 *out) {
  if (gdb->rx_ptr == gdb->rx_sz)
    return RV_GDB_ERR_EOF;
  *out = gdb->rx[gdb->rx_ptr++];
  return 0;
}

int rv_gdb_hex2num(rv_u8 c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  else if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  else if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  else
    return RV_GDB_ERR_FMT;
}

rv_u8 rv_gdb_num2hex(rv_u8 c) {
  assert(c < 16);
  if (c < 10)
    return '0' + c;
  else
    return 'A' + c - 10;
}

int rv_gdb_next_hex(rv_gdb *gdb, rv_u8 *out) {
  int err;
  rv_u8 a, b;
  if ((err = rv_gdb_in(gdb, &a)))
    return err;
  if ((err = rv_gdb_in(gdb, &b)))
    return err;
  if ((err = rv_gdb_hex2num(a)) < 0)
    return err;
  a = (rv_u8)err;
  if ((err = rv_gdb_hex2num(b)) < 0)
    return err;
  b = (rv_u8)err;
  *out = (a * 16) | b;
  return 0;
}

int rv_gdb_tx(rv_gdb *gdb, rv_u8 c) {
  if (gdb->tx_sz + 1 >= gdb->tx_alloc) {
    rv_u32 new_alloc = gdb->tx_alloc ? gdb->tx_alloc << 1 : 16;
    rv_u8 *tmp = realloc(gdb->tx, new_alloc);
    if (!tmp)
      return RV_GDB_ERR_NOMEM;
    gdb->tx = tmp;
    gdb->tx_alloc = new_alloc;
  }
  gdb->tx[gdb->tx_sz++] = c;
  gdb->tx[gdb->tx_sz] = '\0';
  return 0;
}

int rv_gdb_tx_s(rv_gdb *gdb, const char *s) {
  int err = 0;
  while (*s) {
    if ((err = rv_gdb_tx(gdb, (rv_u8)*s)))
      return err;
    s++;
  }
  return err;
}

int rv_gdb_tx_begin(rv_gdb *gdb) {
  int err;
  gdb->tx_sz = 0;
  if (gdb->rx_ack < gdb->rx_seq) {
    if ((err = rv_gdb_tx(gdb, '+')))
      return err;
    gdb->rx_ack++;
  }
  if ((err = rv_gdb_tx(gdb, '$')))
    return err;
  return 0;
}

int rv_gdb_tx_end(rv_gdb *gdb) {
  rv_u8 cksum = 0;
  rv_u32 i;
  int err = 0;
  for (i = 2; i < gdb->tx_sz; i++) {
    cksum += gdb->tx[i];
  }
  if ((err = rv_gdb_tx(gdb, '#')))
    return err;
  if ((err = rv_gdb_tx(gdb, rv_gdb_num2hex(cksum >> 4))))
    return err;
  if ((err = rv_gdb_tx(gdb, rv_gdb_num2hex(cksum & 0xF))))
    return err;
  if (write(gdb->sock, gdb->tx, gdb->tx_sz) != gdb->tx_sz)
    return RV_GDB_ERR_WRITE;
  printf("[Tx] %s %u\n", gdb->tx, ++gdb->tx_seq);
  return 0;
}

int rv_gdb_rx_pre(rv_gdb *gdb, const char *s) {
  rv_u32 i = gdb->rx_ptr;
  while (1) {
    if (*s == '\0') {
      gdb->rx_ptr = i;
      return 1;
    }
    if (i == gdb->rx_sz)
      return 0;
    if (gdb->rx[i] != *s)
      return 0;
    s++;
    i++;
  }
}

int rv_gdb_rx_h(rv_gdb *gdb, rv_u32 *v, unsigned int ndig, int le) {
  int err = 0;
  unsigned int i, j;
  rv_u32 out = 0;
  rv_u8 c;
  rv_u32 saved = gdb->rx_ptr;
  assert(ndig <= 8);
  for (i = 0; i < ndig / 2; i++) {
    for (j = 0; j < 2; j++) {
      unsigned int dig_idx = (le ? i * 2 : (ndig - (i * 2) - 2)) + 1 - j;
      if ((err = rv_gdb_rx(gdb, &c)))
        goto error;
      if ((err = rv_gdb_hex2num(c)) < 0)
        goto error;
      out |= (rv_u32)err << (dig_idx * 4);
    }
  }
  err = 0;
  *v = out;
  return err;
error:
  gdb->rx_ptr = saved;
  return err;
}

int rv_gdb_rx_svwh(rv_gdb *gdb, rv_u32 *v) {
  int err = 0;
  rv_u32 out = 0;
  rv_u32 saved = gdb->rx_ptr;
  int i = 0;
  while (1) {
    rv_u8 c;
    if ((err = rv_gdb_rx(gdb, &c))) {
      goto done;
    }
    if ((err = rv_gdb_hex2num(c)) < 0) {
      gdb->rx_ptr--;
      goto done;
    }
    out <<= 4;
    out |= (rv_u32)err;
    i++;
  }
done:
  if (!i) {
    gdb->rx_ptr = saved;
    return RV_GDB_ERR_EOF;
  }
  *v = out;
  return 0;
}

int rv_gdb_tx_h(rv_gdb *gdb, rv_u32 v, unsigned int ndig, int le) {
  int err = 0;
  unsigned int i, j;
  assert(ndig <= 8);
  for (i = 0; i < ndig / 2; i++) {
    for (j = 0; j < 2; j++) {
      unsigned int dig_idx = (le ? i * 2 : (ndig - (i * 2) - 2)) + 1 - j;
      if ((err = rv_gdb_tx(gdb, rv_gdb_num2hex((v >> (dig_idx * 4)) & 0xF))))
        return err;
    }
  }
  return err;
}

int rv_gdb_tx_sh(rv_gdb *gdb, rv_u8 *buf) {
  int err = 0;
  while (*buf) {
    if ((err = rv_gdb_tx_h(gdb, *(buf++), 2, 0)))
      return err;
  }
  return err;
}

rv_u32 rv_gdb_cvt_sig(rv_u32 except) {
  if (!except)
    return 0; /* none */
  except -= 1;
  if (except == RV_EIFAULT || except == RV_ELFAULT || except == RV_ESFAULT)
    return 10; /* SIGBUS */
  else if (except == RV_EIALIGN || except == RV_ELALIGN || except == RV_ESALIGN)
    return 10; /* SIGBUS */
  else if (except == RV_EBREAK)
    return 5; /* SIGTRAP */
  else if (except == RV_EILL)
    return 4; /* SIGILL */
  else
    return 6; /* SIGABRT */
}

int rv_gdb_stop(rv_gdb *gdb) {
  int err = 0;
  rv_u32 i;
  for (i = 0; i < gdb->harts_sz; i++) {
    rv_gdb_hart *h = gdb->harts + i;
    if (!h->need_send) /* sent stop status */
      continue;
    if ((err = rv_gdb_tx_begin(gdb)))
      return err;
    if ((err = rv_gdb_tx(gdb, 'T')))
      return err;
    if ((err = rv_gdb_tx_h(gdb, rv_gdb_cvt_sig(h->except), 2, 0)))
      return err;
    if ((err = rv_gdb_tx_s(gdb, "thread:")))
      return err;
    if ((err = rv_gdb_tx(gdb, rv_gdb_num2hex((rv_u8)i + 1))))
      return err;
    if ((err = rv_gdb_tx(gdb, ';')))
      return err;
    if ((err = rv_gdb_tx_end(gdb)))
      return err;
    h->need_send = 0;
    break;
  }
  for (i = 0; i < gdb->harts_sz; i++) {
    if (gdb->harts[i].need_send)
      return 1;
  }
  return err;
}

rv *rv_gdb_cpu(rv_gdb *gdb) { return gdb->harts[gdb->hart_idx].cpu; }

int rv_gdb_packet(rv_gdb *gdb) {
  rv_u8 c;
  int err = 0;
  gdb->rx_ptr = 0;
  if ((err = rv_gdb_rx(gdb, &c)))
    return err;
  if (c == '?') { /* stop reason -> run until stop */
    return 1;
  } else if (c == 'H') { /* set current thread */
    rv_u32 tid = 0;
    if ((err = rv_gdb_rx(gdb, &c))) /* get command {c, g} */
      return err;
    if (c == 'c')
      (void)0;
    if (c == 'g') { /* set thrd for step/cont: deprecated */
      if ((err = rv_gdb_rx_svwh(gdb, &tid)))
        return err;
      if (!tid) /* 0: select any thread, we prefer cpu0 */
        tid += 1;
      if (tid > gdb->harts_sz)
        return RV_GDB_ERR_FMT;
      gdb->hart_idx = tid - 1;
    }
    if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_s(gdb, "OK")) ||
        (err = rv_gdb_tx_end(gdb)))
      return err;
  } else if (c == 'T') { /* is thread alive */
    if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_s(gdb, "OK")) ||
        (err = rv_gdb_tx_end(gdb)))
      return err;
  } else if (c == 'Z' || c == 'z') { /* breakpoint */
    if ((err = rv_gdb_rx(gdb, &c)))  /* kind */
      return err;
    if (c != '0')
      return RV_GDB_ERR_FMT;

  } else if (c == 'c' || c == 's') { /* continue/step */
    rv_u32 addr = 0;
    if ((err = rv_gdb_rx_svwh(gdb, &addr) >= 0)) {
      rv_gdb_cpu(gdb)->ip = addr;
    }
    gdb->harts[gdb->hart_idx].state = c == 'c' ? RV_GDB_CONTINUE : RV_GDB_STEP;
    return 1;
  } else if (c == 'g') { /* general registers */
    rv_u32 i;
    if ((err = rv_gdb_tx_begin(gdb)))
      return err;
    for (i = 0; i < 32; i++)
      if ((err = rv_gdb_tx_h(gdb, rv_gdb_cpu(gdb)->r[i], 8, 1)))
        return err;
    if ((err = rv_gdb_tx_h(gdb, rv_gdb_cpu(gdb)->ip, 8, 1)))
      return err;
    if ((err = rv_gdb_tx_end(gdb)))
      return err;
  } else if (c == 'm') { /* read memory */
    rv_u32 addr = 0, size = 0, i;
    rv_res res;
    if ((err = rv_gdb_rx_svwh(gdb, &addr)))
      return err;
    if ((err = rv_gdb_rx(gdb, &c)))
      return err;
    if (c != ',')
      return RV_GDB_ERR_FMT;
    if ((err = rv_gdb_rx_svwh(gdb, &size)))
      return err;
    if ((err = rv_gdb_tx_begin(gdb)))
      return err;
    for (i = 0; i < size; i++) {
      res = rv_gdb_cpu(gdb)->load_cb(rv_gdb_cpu(gdb)->user, addr + i);
      if (rv_isbad(res))
        break;
      else if ((err = rv_gdb_tx_h(gdb, (rv_u32)res, 2, 0)))
        return err;
    }
    if ((err = rv_gdb_tx_end(gdb)))
      return err;
  } else if (c == 'q') {                   /* general query */
    if (rv_gdb_rx_pre(gdb, "Supported")) { /* supported features */
      if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_end(gdb)))
        return err;
    } else if (rv_gdb_rx_pre(gdb, "TStatus")) { /* trace status */
      if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_end(gdb)))
        return err;
    } else if (rv_gdb_rx_pre(gdb, "fThreadInfo")) { /* thread list */
      rv_u8 i;
      if ((err = rv_gdb_tx_begin(gdb)))
        return err;
      if ((err = rv_gdb_tx(gdb, 'm')))
        return err;
      for (i = 0; i < gdb->harts_sz; i++) {
        if (i && (err = rv_gdb_tx(gdb, ',')))
          return err;
        if ((rv_gdb_tx(gdb, rv_gdb_num2hex(i + 1))))
          return err;
      }
      if ((err = rv_gdb_tx_end(gdb)))
        return err;
    } else if (rv_gdb_rx_pre(gdb, "sThreadInfo")) { /* thread list stop */
      if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_s(gdb, "l")) ||
          (err = rv_gdb_tx_end(gdb)))
        return err;
    } else if (rv_gdb_rx_pre(gdb, "Attached")) { /* process attach status */
      if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_s(gdb, "0")) ||
          (err = rv_gdb_tx_end(gdb)))
        return err;
    } else if (rv_gdb_rx_pre(gdb, "C")) { /* return thread id */
      if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_s(gdb, "QC")) ||
          (err = rv_gdb_tx(gdb, rv_gdb_num2hex((rv_u8)gdb->hart_idx + 1))) ||
          (err = rv_gdb_tx_end(gdb)))
        return err;
    } else if (rv_gdb_rx_pre(gdb, "Offsets")) { /* section offsets */
      if ((err = rv_gdb_tx_begin(gdb)) ||
          (err = rv_gdb_tx_s(gdb, "TextSeg=80000000")) ||
          (err = rv_gdb_tx_end(gdb)))
        return err;
    } else if (rv_gdb_rx_pre(gdb, "Symbol::")) { /* symbol lookup ready */
      if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_s(gdb, "OK")) ||
          (err = rv_gdb_tx_end(gdb)))
        return err;
    } else if (rv_gdb_rx_pre(gdb, "ThreadExtraInfo,")) {
      rv_u32 tid = 0;
      if ((err = rv_gdb_rx_svwh(gdb, &tid)))
        return err;
      if (!tid || tid > gdb->harts_sz)
        return RV_GDB_ERR_FMT;
      if ((err = rv_gdb_tx_begin(gdb)) ||
          (err = rv_gdb_tx_sh(gdb, (rv_u8 *)(gdb->harts[tid - 1].tid))) ||
          (err = rv_gdb_tx_end(gdb)))
        return err;
      return 0;
    } else {
      assert(0);
    }
  } else if (c == 'v') { /* variable...? Idk what the v stands for. */
    if (rv_gdb_rx_pre(gdb, "MustReplyEmpty")) { /* unknown packet test */
      if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_end(gdb)))
        return err;
    } else if (rv_gdb_rx_pre(gdb, "Cont?")) { /* vCont support query */
      if ((err = rv_gdb_tx_begin(gdb)) ||
          (err = rv_gdb_tx_s(gdb, "vCont;c;s;C;S;t")) ||
          (err = rv_gdb_tx_end(gdb)))
        return err;
    } else if (rv_gdb_rx_pre(gdb, "Cont")) { /* vCont */
      rv_u32 i;
      for (i = 0; i < gdb->harts_sz; i++) {
        gdb->harts[i].next_state = 0;
      }
      while (1) {
        rv_u32 tid = 0; /* all thrds */
        rv_u8 act;
        rv_u32 sig = 0;
        if ((err = rv_gdb_rx(gdb, &c)) && err != RV_GDB_ERR_EOF)
          return err;
        if (err) /* eof */
          break;
        if (c != ';')
          return RV_GDB_ERR_FMT;
        if ((err = rv_gdb_rx(gdb, &act))) /* parse action */
          return err;
        if ((act == 'C' || act == 'S') && (err = rv_gdb_rx_h(gdb, &sig, 2, 0)))
          return err; /* parse signal */
        if ((err = rv_gdb_rx(gdb, &c)) &&
            err != RV_GDB_ERR_EOF) /* parse colon (maybe) */
          return err;
        if (!err && c != ':')
          gdb->rx_ptr--;
        else if (!err && (err = rv_gdb_rx_svwh(
                              gdb, &tid))) { /* couldn't parse hex tid */
          rv_u8 a, b;
          if ((err = rv_gdb_rx(gdb, &a)) ||
              (err = rv_gdb_rx(gdb, &b))) /* parse -1 */
            return err;
          if (a != '-' || b != '1')
            return RV_GDB_ERR_FMT;
        }
        for (i = 0; i < gdb->harts_sz; i++) {
          rv_u32 state;
          if (act == 'c' || act == 'C') /* continue */
            state = RV_GDB_CONTINUE;
          else if (act == 's' || act == 'S') /* step */
            state = RV_GDB_STEP;
          else
            return RV_GDB_ERR_FMT;
          if ((!tid || (i == tid - 1)) &&
              !gdb->harts[i].next_state) { /* write state to hart */
            printf("[vCont] setting thread %u state to %u\n", i, state);
            gdb->harts[i].state = state;
            gdb->harts[i].next_state = 1;
          }
        }
      }
      return 1;
    } else if (rv_gdb_rx_pre(gdb, "Kill")) { /* kill, ignore pid for now */
      if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_end(gdb))) /* ack */
        return err;
      return 2;
    } else {
      assert(0);
    }
  } else {
    assert(0);
  }
  return err;
}

int rv_gdb_proc(rv_gdb *gdb) {
  int err = 0;
  rv_u8 c;
  rv_u8 cksum = 0;
  gdb->rx_sz = 0;
  gdb->rx_ptr = 0;
ack:
  if ((err = rv_gdb_in(gdb, &c)))
    return err;
  if (c == '+') {
    assert(gdb->tx_ack != gdb->tx_seq);
    gdb->tx_ack++;
    goto ack;
  }
  if (c != '$')
    return RV_GDB_ERR_FMT;
  while (1) {
    if ((err = rv_gdb_in(gdb, &c)))
      return err;
    if (c == '#') { /* packet end */
      gdb->rx_seq++;
      if ((err = rv_gdb_next_hex(gdb, &c)))
        return err;
      if (c != cksum)
        return RV_GDB_ERR_CORRUPT;
      /* process packet */
      printf("[Rx] %s\n", gdb->rx);
      if (gdb->rx_sz && (err = rv_gdb_packet(gdb)))
        return err;
      break;
    } else {
      if (gdb->rx_sz + 1 >= gdb->rx_alloc) {
        rv_u32 new_alloc = gdb->rx_alloc ? gdb->rx_alloc << 1 : 16;
        rv_u8 *tmp = realloc(gdb->rx, new_alloc);
        if (!tmp)
          return RV_GDB_ERR_NOMEM;
        gdb->rx = tmp;
        gdb->rx_alloc = new_alloc;
      }
      cksum += c;
      gdb->rx[gdb->rx_sz++] = c;
      gdb->rx[gdb->rx_sz] = '\0';
    }
  }
  return 0;
}

#include <pthread.h>

typedef pthread_mutex_t game_mutex;

int game_mutex_init(game_mutex *mtx) {
  if (pthread_mutex_init(mtx, NULL))
    return RV_GDB_ERR_MTX;
  return 0;
}

void game_mutex_destroy(game_mutex *mtx) { pthread_mutex_destroy(mtx); }

void game_mutex_lock(game_mutex *mtx) { assert(!pthread_mutex_lock(mtx)); }

void game_mutex_unlock(game_mutex *mtx) { assert(!pthread_mutex_unlock(mtx)); }

typedef pthread_t game_thrd;

int game_thrd_init(game_thrd *thrd, void *(*start_func)(void *arg), void *arg) {
  if (pthread_create(thrd, NULL, start_func, arg))
    return RV_GDB_ERR_MTX;
  return 0;
}

void game_thrd_join(game_thrd *thrd) { pthread_join(*thrd, NULL); }

typedef struct game_regs {
  rv_u32 vbase;
  rv_u32 vmode;
} game_regs;

typedef struct game {
  rv cpus[4];
  game_mutex cpu_lock[4];
  game_mutex bus_lock;
  rv_u8 *rom;
  rv_u8 *ram;
  game_regs regs;
  game_mutex video_lock;
  game_thrd video_thrd;
  rv_gdb gdb;
} game;

#include <SDL2/SDL.h>

#define W 854
#define H 480

void *game_video_thrd(void *arg) {
  game *gm = (game *)arg;
  if (SDL_Init(SDL_INIT_VIDEO) < 0)
    return 0;
  else {
    SDL_Window *win =
        SDL_CreateWindow("SDL2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         854, 480, SDL_WINDOW_SHOWN);
    SDL_Renderer *r = SDL_CreateRenderer(win, -1, 0);
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888,
                                       SDL_TEXTUREACCESS_STREAMING, W, H);
    rv_u32 *pix;
    int run = 1;
    while (run) {
      void *locked_pix;
      int pitch = 0;
      SDL_Event ev;
      while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
          run = 0;
          break;
        }
      }
      pix = NULL;
      game_mutex_lock(&gm->video_lock);
      if (gm->regs.vbase >= 0x40000000 &&
          gm->regs.vbase + W * H * 4 <= 0x41000000)
        pix = (rv_u32 *)(gm->ram + (gm->regs.vbase - 0x40000000));
      game_mutex_unlock(&gm->video_lock);
      SDL_LockTexture(t, NULL, &locked_pix, &pitch);
      if (pix)
        memcpy(locked_pix, pix, W * H * 4);
      SDL_UnlockTexture(t);
      SDL_RenderCopy(r, t, NULL, NULL);
      SDL_RenderPresent(r);
    }
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
  }
  return 0;
}

void *game_debug_thrd(void *arg) {
  game *gm = (game *)arg;
  (void)gm;
  return NULL;
}

rv_res load_cb(void *user, rv_u32 addr) {
  game *m = (game *)user;
  rv_u32 out;
  if (addr >= 0x80000000 && addr <= 0xBFFFFFFF) {
    return m->rom[(addr - 0x80000000) & ~(rv_u32)(0x10000)];
  } else if (addr >= 0x40000000 && addr <= 0x40FFFFFF) {
    return m->ram[addr - 0x40000000];
  } else if (addr >= 0xC0000000 && addr < 0xC0000000 + sizeof(game_regs)) {
    game_mutex_lock(&m->video_lock);
    out = ((rv_u8 *)(&m->regs))[addr - 0xC0000000];
    game_mutex_unlock(&m->video_lock);
    return out;
  }
  return RV_BAD;
}

rv_res store_cb(void *user, rv_u32 addr, rv_u8 data) {
  game *m = (game *)user;
  if (addr >= 0x80000000 && addr <= 0xBFFFFFFF) {
    return RV_BAD;
  } else if (addr >= 0x40000000 && addr <= 0x40FFFFFF) {
    m->ram[addr - 0x40000000] = data;
    return 0;
  } else if (addr >= 0xC0000000 && addr < 0xC0000000 + sizeof(game_regs)) {
    game_mutex_lock(&m->video_lock);
    ((rv_u8 *)(&m->regs))[addr - 0xC0000000] = data;
    game_mutex_unlock(&m->video_lock);
    return 0;
  }
  return RV_BAD;
}

int game_init(game *gm, const char *rom) {
  int fd = open(rom, O_RDONLY);
  rv_u32 i;
  gm->rom = NULL;
  gm->ram = NULL;
  for (i = 0; i < 4; i++) {
    rv_init(&gm->cpus[i], (void *)gm, load_cb, store_cb);
    gm->cpus[i].csrs.mhartid = i;
    assert(!game_mutex_init(&gm->cpu_lock[i]));
  }
  assert(!game_mutex_init(&gm->bus_lock));
  assert(!game_mutex_init(&gm->video_lock));
  memset(&gm->regs, 0, sizeof(game_regs));
  gm->rom = malloc(sizeof(rv_u8) * 0x10000);
  if (!gm->rom)
    return RV_GDB_ERR_NOMEM;
  gm->ram = malloc(sizeof(rv_u8) * 0x1000000);
  if (!gm->ram)
    return RV_GDB_ERR_NOMEM;
  memset(gm->rom, 0, 0x10000);
  memset(gm->ram, 0, 0x1000000);
  read(fd, gm->rom, 0x10000);
  assert(!game_thrd_init(&gm->video_thrd, game_video_thrd, gm));
  return 0;
}

void game_destroy(game *gm) {
  if (gm->rom)
    free(gm->rom);
  if (gm->ram)
    free(gm->ram);
  game_mutex_destroy(&gm->video_lock);
  game_mutex_destroy(&gm->bus_lock);
  game_thrd_join(&gm->video_thrd);
}

int main(int argc, const char **argv) {
  int sock, new;
  struct sockaddr_in addr;
  int addrlen = sizeof(addr);
  game gm;
  (void)argc;
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("\n Socket creation error \n");
    return 1;
  }

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(61444);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind failed");
    shutdown(sock, SHUT_RDWR);
    exit(EXIT_FAILURE);
  }
  if (listen(sock, 3) < 0) {
    perror("listen");
    shutdown(sock, SHUT_RDWR);
    exit(EXIT_FAILURE);
  }
  if ((new = accept(sock, (struct sockaddr *)&addr, (socklen_t *)&addrlen)) <
      0) {
    perror("accept");
    shutdown(sock, SHUT_RDWR);
    exit(EXIT_FAILURE);
  }
  {
    const char *bn = argv[1];
    rv_u32 i = 0;
    int err = 0;
    rv_gdb gdb;
    assert(!game_init(&gm, bn));
    assert(!rv_gdb_init(&gdb, new));
    assert(!rv_gdb_addhart(&gdb, gm.cpus + 0, "cpu0"));
    gdb.harts[0].need_send = 1;
    gdb.harts[0].state = RV_GDB_STOP;
    assert(!rv_gdb_addhart(&gdb, gm.cpus + 1, "cpu1"));
    assert(!rv_gdb_addhart(&gdb, gm.cpus + 2, "gpu"));
    assert(!rv_gdb_addhart(&gdb, gm.cpus + 3, "spu"));
    while (1) {
      while (!(err = rv_gdb_proc(&gdb))) {
      }
      if (err == 2) /* vKill */
        break;
      assert(err > 0);
      /* send pending stop events */
      if ((err = rv_gdb_stop(&gdb)) == 1) {
        continue; /* get acks for those */
      } else if (err) {
        exit(1);
      }
    cont:
      for (i = 0; i < gdb.harts_sz; i++) {
        rv_gdb_hart *h = gdb.harts + i;
        rv_u32 inst_out;
        if (h->state == RV_GDB_CONTINUE) {
          if ((inst_out = rv_inst(h->cpu))) {
            h->except = inst_out;
            h->state = RV_GDB_STOP;
            h->need_send = 1;
          }
        } else if (h->state == RV_GDB_STEP) {
          if ((inst_out = rv_inst(h->cpu))) {
            h->except = inst_out;
          } else {
            h->except = 0;
          }
          h->state = RV_GDB_STOP;
          h->need_send = 1;
        } else {
          h->except = rv_inst(h->cpu);
          h->need_send = 1;
        }
      }
      for (i = 0; i < gdb.harts_sz; i++) {
        if (gdb.harts[i].state == RV_GDB_STOP)
          goto stop;
      }
      goto cont;
    stop:
      rv_gdb_stop(&gdb);
    }
    rv_gdb_destroy(&gdb);
  }

  close(new);
  shutdown(sock, SHUT_RDWR);
  game_destroy(&gm);
  return 0;
}

/* threads:
 * video
 * audio
 * cpu[0-3]
 * debug */
