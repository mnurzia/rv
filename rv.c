#include "rv.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define RV_RESET_VEC 0x80000000

void rv_init(rv *cpu, void *user, rv_load_cb load_cb, rv_store_cb store_cb) {
  cpu->user = user;
  cpu->load_cb = load_cb;
  cpu->store_cb = store_cb;
  cpu->ip = RV_RESET_VEC;
  memset(cpu->r, 0, sizeof(cpu->r));
  memset(&cpu->csrs, 0, sizeof(cpu->csrs));
#if RVF
  memset(cpu->f, 0, sizeof(cpu->f));
  cpu->fcsr = 0;
#endif
}

void rv_destroy(rv *cpu) { (void)(cpu); }

void rv_dump(rv *cpu) {
  int i, y;
  printf("ip=%08X\n", cpu->ip);
  for (i = 0; i < 8; i++) {
    for (y = 0; y < 4; y++)
      printf("x%02d=%08X ", 8 * y + i, cpu->r[8 * y + i]);
    printf("\n");
  }
}

#define rv_isbad(x) ((x) >> 32)

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
    return RV_BAD;
  }
}

rv_res rv_scsr(rv *cpu, rv_u32 csr, rv_u32 v) { /* store csr */
  printf("(SCSR) %04X <- %08X\n", csr, v);
  if (csr == 0xF14) { /* mhartid */
    return RV_BAD;
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
    return RV_BAD;
  }
  return 0;
}

rv_u32 rv_except(rv *cpu, rv_u32 cause) { /* set exception state */
  assert(cause);
  printf("(E) %04X\n", cause);
  cpu->ip =
      (cpu->csrs.mtvec & (rv_u32)~1) + 4 * (cause - 1) * (cpu->csrs.mtvec & 1);
  return cause;
}

#ifdef RVC
#define rvc_op(c) rv_bf(c, 1, 0)           /* c. op */
#define rvc_f3(c) rv_bf(c, 15, 13)         /* c. funct3 */
#define rvc_rp(r) ((r) + 8)                /* c. prime register offsetter */
#define rvc_ird(c) rv_bf(c, 11, 7)         /* c. ci-format rd/rs1  */
#define rvc_irpl(c) rvc_rp(rv_bf(c, 4, 2)) /* c. rd'/rs2' (bits 4-2) */
#define rvc_irph(c) rvc_rp(rv_bf(c, 9, 7)) /* c. rd'/rs1' (bits 9-7) */
#define rvc_imm_ciw(c)                                                         \
  (rv_tbf(c, 10, 7, 6) | rv_tbf(c, 12, 11, 4) | rv_tb(c, 6, 3) |               \
   rv_tb(c, 5, 2)) /* CIW imm. for c.addi4spn */
#define rvc_imm_cl(c)                                                          \
  (rv_tb(c, 5, 6) | rv_tbf(c, 12, 10, 3) |                                     \
   rv_tb(c, 6, 2)) /* CL imm. for c.lw/c.sw */
#define rvc_imm_ci(c)                                                          \
  (rv_signext(rv_tb(c, 12, 5), 5) |                                            \
   rv_bf(c, 6, 2)) /* CI imm. for c.addi/c.li/c.lui */
#define rvc_imm_ci_b(c)                                                        \
  (rv_signext(rv_tb(c, 12, 9), 9) | rv_tbf(c, 4, 3, 7) | rv_tb(c, 5, 6) |      \
   rv_tb(c, 2, 5) | rv_tb(c, 6, 4)) /* CI imm. for c.addi16sp */
#define rvc_imm_ci_c(c)                                                        \
  (rv_tbf(c, 3, 2, 6) | rv_tb(c, 12, 5) |                                      \
   rv_tbf(c, 6, 4, 2)) /* CI imm. for c.lwsp */
#define rvc_imm_cj(c)                                                          \
  (rv_signext(rv_tb(c, 12, 11), 11) | rv_tb(c, 8, 10) | rv_tbf(c, 10, 9, 8) |  \
   rv_tb(c, 6, 7) | rv_tb(c, 7, 6) | rv_tb(c, 2, 5) | rv_tb(c, 11, 4) |        \
   rv_tbf(c, 5, 3, 1)) /* CJ imm. for c.jalr/c.j */
#define rvc_imm_cb(c)                                                          \
  (rv_signext(rv_tb(c, 12, 8), 8) | rv_tbf(c, 6, 5, 6) | rv_tb(c, 2, 5) |      \
   rv_tbf(c, 11, 10, 3) | rv_tbf(c, 4, 3, 1)) /* CB imm. for c.beqz/c.bnez */
#define rvc_imm_css(c)                                                         \
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

rv_u32 rvc_inst(rv_u32 c) { /* convert compressed to regular inst. */
  if (rvc_op(c) == 0) {
    if (rvc_f3(c) == 0 && c != 0) { /* c.addi4spn -> addi rd', x2, nzuimm */
      return rv_i_i(4, 0, rvc_irpl(c), 2, rvc_imm_ciw(c));
    } else if (c == 0) { /* illegal instr. */
      return 0;
    } else if (rvc_f3(c) == 2) { /* c.lw -> lw rd', offset(rs1') */
      return rv_i_i(0, 2, rvc_irpl(c), rvc_irph(c), rvc_imm_cl(c));
    } else if (rvc_f3(c) == 6) { /* c.sw -> sw rs2', offset(rs1') */
      return rv_i_s(8, 2, rvc_irph(c), rvc_irpl(c), rvc_imm_cl(c));
    } else {
      return 0;
    }
  } else if (rvc_op(c) == 1) {
    if (rvc_f3(c) == 0) { /* c.addi -> addi rd, rd, nzimm */
      return rv_i_i(4, 0, rvc_ird(c), rvc_ird(c), rvc_imm_ci(c));
    } else if (rvc_f3(c) == 1) { /* c.jal -> jal x1, offset */
      return rv_i_j(27, 1, rvc_imm_cj(c));
    } else if (rvc_f3(c) == 2) { /* c.li -> addi rd, x0, imm */
      return rv_i_i(4, 0, rvc_ird(c), 0, rvc_imm_ci(c));
    } else if (rvc_f3(c) == 3) { /* 01/011: LUI/ADDI16SP */
      if (rvc_ird(c) == 2) {     /* c.addi16sp -> addi x2, x2, nzimm */
        return rv_i_i(4, 0, 2, 2, rvc_imm_ci_b(c));
      } else if (rvc_ird(c) != 0) { /* c.lui -> lui rd, nzimm */
        return rv_i_u(13, rvc_ird(c), rvc_imm_ci(c));
      } else {
        return 0;
      }
    } else if (rvc_f3(c) == 4) {   /* 01/100: MISC-ALU */
      if (rv_bf(c, 11, 10) == 0) { /* c.srli -> srli rd', rd', shamt */
        return rv_i_r(4, 5, rvc_irph(c), rvc_irph(c), rvc_imm_ci(c) & 0x1F, 0);
      } else if (rv_bf(c, 11, 10) == 1) { /* c.srai -> srai rd', rd', shamt */
        return rv_i_r(4, 5, rvc_irph(c), rvc_irph(c), rvc_imm_ci(c) & 0x1F, 32);
      } else if (rv_bf(c, 11, 10) == 2) { /* c.andi -> andi rd', rd', imm */
        return rv_i_i(4, 7, rvc_irph(c), rvc_irph(c), rvc_imm_ci(c));
      } else if (rv_bf(c, 11, 10) == 3) {
        if (rv_bf(c, 6, 5) == 0) { /* c.sub -> sub rd', rd', rs2' */
          return rv_i_r(12, 0, rvc_irph(c), rvc_irph(c), rvc_irpl(c), 32);
        } else if (rv_bf(c, 6, 5) == 1) { /* c.xor -> xor rd', rd', rs2' */
          return rv_i_r(12, 4, rvc_irph(c), rvc_irph(c), rvc_irpl(c), 0);
        } else if (rv_bf(c, 6, 5) == 2) { /* c.or -> or rd', rd', rs2' */
          return rv_i_r(12, 6, rvc_irph(c), rvc_irph(c), rvc_irpl(c), 0);
        } else if (rv_bf(c, 6, 5) == 3) { /* c.and -> and rd', rd', rs2' */
          return rv_i_r(12, 7, rvc_irph(c), rvc_irph(c), rvc_irpl(c), 0);
        } else {
          return 0;
        }
      } else {
        return 0;
      }
    } else if (rvc_f3(c) == 5) { /* c.j -> jal x0, offset */
      return rv_i_j(27, 0, rvc_imm_cj(c));
    } else if (rvc_f3(c) == 6) { /* c.beqz -> beq rs1' x0, offset */
      return rv_i_b(24, 0, rvc_irph(c), 0, rvc_imm_cb(c));
    } else if (rvc_f3(c) == 7) { /* c.bnez -> bne rs1' x0, offset */
      return rv_i_b(24, 1, rvc_irph(c), 0, rvc_imm_cb(c));
    } else {
      return 0;
    }
  } else if (rvc_op(c) == 2) {
    if (rvc_f3(c) == 0) { /* c.slli -> slli rd, rd, shamt */
      return rv_i_r(4, 1, rvc_ird(c), rvc_ird(c), rvc_imm_ci(c) & 0x1F, 0);
    } else if (rvc_f3(c) == 2) { /* c.lwsp -> lw rd, offset(x2) */
      return rv_i_i(0, 2, rvc_ird(c), 2, rvc_imm_ci_c(c));
    } else if (rvc_f3(c) == 4 && !rv_b(c, 12) &&
               !rv_bf(c, 6, 2)) { /* c.jr -> jalr x0, 0(rs1) */
      return rv_i_i(25, 0, 0, rvc_ird(c), 0);
    } else if (rvc_f3(c) == 4 && !rv_b(c, 12)) { /* c.mv -> add rd, x0, rs2 */
      return rv_i_r(12, 0, rvc_ird(c), 0, rv_bf(c, 6, 2), 0);
    } else if (rvc_f3(c) == 4 && rv_b(c, 12) && rvc_ird(c) &&
               !rv_bf(c, 6, 2)) { /* c.jalr -> jalr x1, 0(rs1) */
      return rv_i_i(25, 0, 1, rvc_ird(c), 0);
    } else if (rvc_f3(c) == 4 && rv_b(c, 12) && rvc_ird(c) &&
               rv_bf(c, 6, 2)) { /* c.add -> add rd, rd, rs2 */
      return rv_i_r(12, 0, rvc_ird(c), rvc_ird(c), rv_bf(c, 6, 2), 0);
    } else if (rvc_f3(c) == 6) { /* c.swsp -> sw rs2, offset(x2) */
      return rv_i_s(8, 2, 2, rv_bf(c, 6, 2), rvc_imm_css(c));
    } else {
      return 0;
    }
  } else {
    return 0;
  }
}
#endif /* RVC */

#ifdef RVF

#include <math.h>

rv_f32 rvf_launder_u32(rv_u32 x) { return *((rv_f32 *)&x); } /* f32 -> u32 */
rv_u32 rvf_launder_f32(rv_f32 x) { return *((rv_u32 *)&x); } /* u32 -> f32 */

rv_f32 rvf_lr(rv *cpu, rv_u8 i) { return cpu->f[i]; } /* load float register */

void rvf_sr(rv *cpu, rv_u8 i, rv_f32 v) { /* store float register */
  cpu->f[i] = v;
}

#define rvf_irm(i) rv_bf(i, 14, 12)  /* float rounding mode */
#define rvf_ifmt(i) rv_bf(i, 26, 25) /* float width */
#define rvf_if5(i) rv_bf(i, 31, 27)  /* float funct5 */

rv_u32 rvf_inst_flw(rv *cpu, rv_u32 i) { /* load fp op */
  rv_u32 addr = rv_lr(cpu, rv_irs1(i)) + rv_iimm_i(i);
  rv_res res;
  rv_f32 out;
  if (rvf_irm(i) != 2) /* no widths other than single precision allowed */
    return RV_EILL;
  printf("(FLW) %08X -> ", addr);
  res = rv_lw(cpu, addr);
  if (rv_isbad(res)) {
    printf("fault\n");
    return RV_ELFAULT;
  }
  out = rvf_launder_u32((rv_u32)res);
  rvf_sr(cpu, rv_ird(i), out);
  printf("%f (%08X)\n", out, (rv_u32)res);
  cpu->ip = cpu->next_ip;
  return 0;
}

rv_u32 rvf_inst_fsw(rv *cpu, rv_u32 i) { /* store fp op */
  rv_u32 addr = rv_lr(cpu, rv_irs1(i)) + rv_iimm_s(i);
  rv_res res;
  if (rvf_irm(i) != 2) /* no widths other than single precision allowed */
    return RV_EILL;
  printf("(FSW) %08X -> ", addr);
  res = rv_sw(cpu, addr, rvf_launder_f32(rvf_lr(cpu, rv_irs2(i))));
  if (rv_isbad(res)) {
    printf("fault\n");
    return RV_ESFAULT;
  }
  cpu->ip = cpu->next_ip;
  return 0;
}

rv_u32 rvf_inst_op(rv *cpu, rv_u32 i) { /* computational fp op */
  rv_f32 a = rvf_lr(cpu, rv_irs1(i));
  rv_f32 b = rvf_lr(cpu, rv_irs2(i));
  rv_f32 y;
  if (rvf_ifmt(i) != 0) /* no widths other than single precision allowed */
    return RV_EILL;
  if (rvf_if5(i) == 0) /* fadd.s */
    y = a + b;
  else if (rvf_if5(i) == 1) /* fsub.s */
    y = a - b;
  else if (rvf_if5(i) == 2) /* fmul.s */
    y = a * b;
  else if (rvf_if5(i) == 3) /* fdiv.s */
    y = a / b;
  else if (rvf_if5(i) == 4) {
    rv_u32 sgn;
    if (rvf_irm(i) == 0) /* fsgnj.s */
      sgn = rvf_launder_f32(b) & RV_SBIT;
    else if (rvf_irm(i) == 1) /* fsgnjn.s */
      sgn = ~rvf_launder_f32(b) & RV_SBIT;
    else if (rvf_irm(i) == 2) /* fsgnjx.s */
      sgn = (rvf_launder_f32(b) ^ rvf_launder_f32(a)) & RV_SBIT;
    else
      return RV_EILL;
    y = rvf_launder_u32((rvf_launder_f32(a) & (~RV_SBIT)) | sgn);
  } else if (rvf_if5(i) == 5) { /* fmin.s, fmax.s */
    if (rvf_irm(i) > 1)
      return RV_EILL;
    y = (a > b) ? (rvf_irm(i)) ? a : b : (rvf_irm(i)) ? b : a;
  } else if (rvf_if5(i) == 11) /* fsqrt.s */
    y = sqrtf(a);
  else if (rvf_if5(i) == 24 && rv_irs2(i) <= 1) { /* fcvt.w.s, fcvt.wu.s */
    y = rvf_lr(cpu, rv_ird(i)); /* ensure final sr is a no-op */
    rv_sr(cpu, rv_ird(i), rv_irs2(i) ? (rv_u32)a : (rv_u32)(rv_s32)a);
  } else if (rvf_if5(i) == 26 && rv_irs2(i) <= 1) { /* fcvt.s.w, fcvt.s.wu */
    rv_u32 r = rv_lr(cpu, rv_irs1(i));
    y = rv_irs2(i) ? (rv_f32)r : (rv_f32)(rv_s32)r;
  } else if (rvf_if5(i) == 30 && rv_irs2(i) == 0) { /* fmv.x.w */
    y = rvf_lr(cpu, rv_ird(i));
    rv_sr(cpu, rv_ird(i), rvf_launder_f32(a));
  } else if (rvf_if5(i) == 30 && rv_irs2(i) == 0) { /* fmv.w.x */
    y = rvf_launder_u32(rv_lr(cpu, rv_irs1(i)));
  } else
    return RV_EILL;
  rvf_sr(cpu, rv_ird(i), y);
  cpu->ip = cpu->next_ip;
  return 0;
}

#endif /* RVF */

rv_u32 rv_inst(rv *cpu) {
  /* fetch instruction */
  rv_res ires = rv_lw(cpu, cpu->ip);
  rv_u32 i = (rv_u32)ires;
  if (rv_isbad(ires))
    printf("(IF) %08X -> fault\n", cpu->ip);
  else
    printf("(IF) %08X -> %08X\n", cpu->ip, i);
  if (rv_isbad(ires))
    return rv_except(cpu, RV_EIFAULT);
  cpu->next_ip = cpu->ip + rv_isz(i);
#if RVC
  if (rv_isz(i) != 4)
    i = rvc_inst(i & 0xFFFF);
#endif
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
        return rv_except(cpu, RV_EILL);
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
        return rv_except(cpu, RV_EILL);
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
        cpu->next_ip = targ;
      } else if (rv_if3(i) == 2 || rv_if3(i) == 3) {
        return rv_except(cpu, RV_EILL);
      }
    } else {
      return rv_except(cpu, RV_EILL);
    }
  } else if (rv_iopl(i) == 1) {
#if RVF
    if (rv_ioph(i) == 0) { /* 00/001: LOAD-FP */
      return rvf_inst_flw(cpu, i);
    } else if (rv_ioph(i) == 1) { /* 01/001: STORE-FP */
      return rvf_inst_fsw(cpu, i);
    } else
#endif
        if (rv_ioph(i) == 3 && rv_if3(i) == 0) { /* 11/001: JALR */
      rv_u32 target = (rv_lr(cpu, rv_irs1(i)) + rv_iimm_i(i)) & (~(rv_u32)1);
      rv_sr(cpu, rv_ird(i), cpu->next_ip); /* jalr */
      cpu->next_ip = target;
    } else {
      return rv_except(cpu, RV_EILL);
    }
  } else if (rv_iopl(i) == 3) {
    if (rv_ioph(i) == 0) {  /* 00/011: MISC-MEM */
      if (rv_if3(i) == 0) { /* fence */
        rv_u32 fm = rv_bf(i, 31, 28);
        if (fm && fm != 8) /* fm != 0000/1000 */
          return rv_except(cpu, RV_EILL);
      } else if (rv_if3(i) == 1) { /* fence.i */
      } else {
        return rv_except(cpu, RV_EILL);
      }
    } else if (rv_ioph(i) == 3) {          /* 11/011: JAL */
      rv_sr(cpu, rv_ird(i), cpu->next_ip); /* jal */
      cpu->next_ip = cpu->ip + rv_iimm_j(i);
    } else {
      return rv_except(cpu, RV_EILL);
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
    }
#if RVF
    else if (rv_ioph(i) == 2) { /* 10/100: OP-FP */
      return rvf_inst_op(cpu, i);
    }
#endif
    else if (rv_ioph(i) == 3) { /* 11/100: SYSTEM */
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
            cpu->next_ip = cpu->csrs.mepc;
          } else if (!rv_irs1(i) && !rv_irs2(i) && !rv_if7(i)) { /* ecall */
            return rv_except(cpu, RV_EECALL);
          } else if (!rv_irs1(i) && rv_irs2(i) == 1 &&
                     !rv_if7(i)) { /* ebreak */
            return rv_except(cpu, RV_EBP);
          } else {
            return rv_except(cpu, RV_EILL);
          }
        } else {
          return rv_except(cpu, RV_EILL);
        }
      } else {
        return rv_except(cpu, RV_EILL);
      }
    } else {
      return rv_except(cpu, RV_EILL);
    }
  } else if (rv_iopl(i) == 5) {
    if (rv_ioph(i) == 0) {                           /* 00/101: AUIPC */
      rv_sr(cpu, rv_ird(i), rv_iimm_u(i) + cpu->ip); /* auipc */
    } else if (rv_ioph(i) == 1) {                    /* 01/101: LUI */
      rv_sr(cpu, rv_ird(i), rv_iimm_u(i));           /* lui */
    } else {
      return rv_except(cpu, RV_EILL);
    }
  } else {
    return rv_except(cpu, RV_EILL);
  }
  cpu->ip = cpu->next_ip;
  return 0;
}
