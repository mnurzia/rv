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
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct rv_gdb_hart rv_gdb_hart;

struct rv_gdb_hart {
  rv *cpu;
  rv_u32 stop_reason;
  const char *tid;
};

typedef struct rv_gdb {
  rv_gdb_hart *harts;
  rv_u32 harts_sz;
  rv_u32 hart_idx;
  rv_u32 stop_reason;
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
  int sock;
} rv_gdb;

#define RV_GDB_BUF_ALLOC 1024

int rv_gdb_init(rv_gdb *gdb, int sock) {
  assert(sock);
  gdb->harts = NULL;
  gdb->harts_sz = 0;
  gdb->hart_idx = 0;
  gdb->stop_reason = 0;
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
  if (!(gdb->rx_pre = malloc(RV_GDB_BUF_ALLOC)))
    return -1;
  memset(gdb->rx_pre, 0, RV_GDB_BUF_ALLOC);
  return 0;
}

int rv_gdb_addhart(rv_gdb *gdb, rv *cpu, const char *tid) {
  rv_gdb_hart *harts =
      realloc(gdb->harts, (gdb->harts_sz + 1) * sizeof(rv_gdb_hart));
  if (!harts)
    return -1;
  gdb->harts = harts;
  gdb->harts[gdb->harts_sz].cpu = cpu;
  gdb->harts[gdb->harts_sz].tid = tid;
  gdb->harts[gdb->harts_sz++].stop_reason = 0;
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
}

int rv_gdb_recv(rv_gdb *gdb) {
  ssize_t rv = read(gdb->sock, gdb->rx_pre, RV_GDB_BUF_ALLOC);
  assert(rv <= RV_GDB_BUF_ALLOC);
  if (rv <= 0)
    return -1;
  gdb->rx_pre_sz = (rv_u32)rv;
  gdb->rx_pre_ptr = 0;
  return 0;
}

int rv_gdb_in(rv_gdb *gdb, rv_u8 *out) {
  if (gdb->rx_pre_ptr == gdb->rx_pre_sz && rv_gdb_recv(gdb))
    return -1;
  *out = gdb->rx_pre[gdb->rx_pre_ptr++];
  return 0;
}

int rv_gdb_rx(rv_gdb *gdb, rv_u8 *out) {
  if (gdb->rx_ptr == gdb->rx_sz) {
    return -1;
  }
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
    return -1;
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

#define RV_GDB_ERR_FMT -2
#define RV_GDB_ERR_CORRUPT -3

int rv_gdb_tx(rv_gdb *gdb, rv_u8 c) {
  if (gdb->tx_sz + 1 >= gdb->tx_alloc) {
    rv_u32 new_alloc = gdb->tx_alloc ? gdb->tx_alloc << 1 : 16;
    rv_u8 *tmp = realloc(gdb->tx, new_alloc);
    if (!tmp)
      return -1;
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
  if ((err = rv_gdb_tx(gdb, '+')))
    return err;
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
    return -1;
  printf("[Tx] %s\n", gdb->tx);
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
  assert(ndig <= 8);
  for (i = 0; i < ndig / 2; i++) {
    for (j = 0; j < 2; j++) {
      unsigned int dig_idx = (le ? i * 2 : (ndig - (i * 2) - 2)) + 1 - j;
      if ((err = rv_gdb_rx(gdb, &c)))
        return err;
      if ((err = rv_gdb_hex2num(c)) < 0)
        return err;
      out |= (rv_u32)err << (dig_idx * 4);
    }
  }
  *v = out;
  return err;
}

int rv_gdb_rx_svwh(rv_gdb *gdb, rv_u32 *v) {
  int err = 0;
  rv_u32 out = 0;
  int i = 0;
  while (1) {
    rv_u8 c;
    if ((err = rv_gdb_rx(gdb, &c)) || (err = rv_gdb_hex2num(c)) < 0) {
      if (!i)
        return -1; /* no data */
      *v = out;
      return 0; /* hit separator */
    }
    out <<= 4;
    out |= (rv_u32)err;
    i++;
  }
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

#define RV_GDB_MORE 0
#define RV_GDB_STEP 1
#define RV_GDB_CONTINUE 2

int rv_gdb_stop(rv_gdb *gdb) {
  int err = 0;
  rv_u32 i;
  if ((err = rv_gdb_tx_begin(gdb)))
    return err;
  if ((err = rv_gdb_tx(gdb, 'T')))
    return err;
  for (i = 0; i < gdb->harts_sz; i++) {
    if (!gdb->harts[i].stop_reason)
      continue;
    if ((err = rv_gdb_tx_h(gdb, gdb->harts[i].stop_reason, 2, 0)))
      return err;
    if ((err = rv_gdb_tx_s(gdb, "thread:")))
      return err;
    if ((err = rv_gdb_tx(gdb, rv_gdb_num2hex((rv_u8)i + 1))))
      return err;
    if ((err = rv_gdb_tx(gdb, ';')))
      return err;
  }
  if ((err = rv_gdb_tx_end(gdb)))
    return err;
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
    return RV_GDB_CONTINUE;
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
        return -1;
      gdb->hart_idx = tid - 1;
    }
    if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_s(gdb, "OK")) ||
        (err = rv_gdb_tx_end(gdb)))
      return err;
  } else if (c == 'c' || c == 's') { /* continue/step */
    rv_u32 addr = 0;
    if ((err = rv_gdb_rx_svwh(gdb, &addr) >= 0)) {
      rv_gdb_cpu(gdb)->ip = addr;
    }
    return c == 'c' ? RV_GDB_CONTINUE : RV_GDB_STEP;
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
        return -1;
      if ((err = rv_gdb_tx_begin(gdb)) ||
          (err = rv_gdb_tx_sh(gdb, (rv_u8 *)(gdb->harts[tid - 1].tid))) ||
          (err = rv_gdb_tx_end(gdb)))
        return err;
      return 0;
      return -1;
    } else {
      assert(0);
    }
  } else if (c == 'v') { /* variable...? Idk what the v stands for. */
    if (rv_gdb_rx_pre(gdb, "MustReplyEmpty")) { /* unknown packet test */
      if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_end(gdb)))
        return err;
    } else if (rv_gdb_rx_pre(gdb, "Cont")) { /* vCont packet unsupported */
      if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_end(gdb)))
        return err;
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
  if ((err = rv_gdb_in(gdb, &c)))
    return err;
  if (c != '+')
    return RV_GDB_ERR_FMT;
  if ((err = rv_gdb_in(gdb, &c)))
    return err;
  if (c != '$')
    return RV_GDB_ERR_FMT;
  while (1) {
    if ((err = rv_gdb_in(gdb, &c)))
      return err;
    if (c == '#') { /* packet end */
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
          return -1;
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

typedef struct mapper {
  rv_u8 *rom;
  rv_u8 *ram;
} mapper;

rv_res load_cb(void *user, rv_u32 addr) {
  mapper *m = (mapper *)user;
  if (addr >= 0x80000000 && addr <= 0xBFFFFFFF) {
    return m->rom[(addr - 0x80000000) & ~(rv_u32)(0x10000)];
  } else if (addr >= 0x40000000 && addr <= 0x40FFFFFF) {
    return m->ram[addr - 0x40000000];
  }
  return RV_BAD;
}

rv_res store_cb(void *user, rv_u32 addr, rv_u8 data) {
  mapper *m = (mapper *)user;
  if (addr >= 0x80000000 && addr <= 0xBFFFFFFF) {
    return RV_BAD;
  } else if (addr >= 0x40000000 && addr <= 0x40FFFFFF) {
    m->ram[addr - 0x40000000] = data;
    return 0;
  }
  return RV_BAD;
}

int main(int argc, const char **argv) {
  int sock, new;
  struct sockaddr_in addr;
  int addrlen = sizeof(addr);
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("\n Socket creation error \n");
    return -1;
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
    mapper map;
    int fd = open(bn, O_RDONLY);
    int i = 0;
    rv cpus[4];
    rv_gdb gdb;
    map.rom = malloc(sizeof(rv_u8) * 0x10000);
    map.ram = malloc(sizeof(rv_u8) * 0x1000000);
    (void)(argc);
    (void)(argv);
    assert(map.rom);
    assert(map.ram);
    memset(map.rom, 0, 0x10000);
    memset(map.ram, 0, 0x1000000);
    read(fd, map.rom, 0x10000);
    for (i = 0; i < 4; i++) {
      rv_init(cpus + i, (void *)&map, &load_cb, &store_cb);
    }
    assert(!rv_gdb_init(&gdb, new));
    assert(!rv_gdb_addhart(&gdb, cpus + 0, "cpu0"));
    assert(!rv_gdb_addhart(&gdb, cpus + 1, "cpu1"));
    assert(!rv_gdb_addhart(&gdb, cpus + 2, "gpu"));
    assert(!rv_gdb_addhart(&gdb, cpus + 3, "spu"));
    while (1) {
      int gdb_rv = rv_gdb_proc(&gdb);
      assert(gdb_rv >= 0);
      if (gdb_rv == RV_GDB_MORE)
        continue;
      else if (gdb_rv == RV_GDB_CONTINUE) {
        rv_u32 inst_out = 0;
        while (!(inst_out = rv_inst(gdb.harts[gdb.hart_idx].cpu))) {
        }
        gdb.harts[gdb.hart_idx].stop_reason = 5; /* trap */
        assert(!rv_gdb_stop(&gdb));
      } else if (gdb_rv == RV_GDB_STEP) {
        rv_inst(gdb.harts[gdb.hart_idx].cpu);
        gdb.harts[gdb.hart_idx].stop_reason = 0; /* none - single stepped */
        assert(!rv_gdb_stop(&gdb));
      }
    }
    rv_gdb_destroy(&gdb);
  }

  close(new);
  shutdown(sock, SHUT_RDWR);
  return 0;
}
