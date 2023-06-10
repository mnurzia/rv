#include "rv.h"

#include <assert.h>
#include <stdarg.h>
#include <string.h>

#define RV_RESET_VEC 0x80000000 /* CPU reset vector */

#if RV_VERBOSE
#include <stdio.h>
#define rv_dbg printf
#else
void rv_dbg(const char *fmt, ...) {
  (void)fmt;
  return;
}
#endif

void rv_init(rv *cpu, void *user, rv_load_cb load_cb, rv_store_cb store_cb) {
  cpu->user = user;
  cpu->load_cb = load_cb;
  cpu->store_cb = store_cb;
  cpu->pc = RV_RESET_VEC;
  memset(cpu->r, 0, sizeof(cpu->r));
  memset(&cpu->csrs, 0, sizeof(cpu->csrs));
}

rv_res rv_lb(rv *cpu, rv_u32 addr, rv_u8 *data) { /* load byte */
  return cpu->load_cb(cpu->user, addr, data);
}

rv_res rv_lh(rv *cpu, rv_u32 addr, rv_u16 *data) { /* load half */
  return rv_lb(cpu, addr, (rv_u8 *)data) ||
         rv_lb(cpu, addr + 1, ((rv_u8 *)data) + 1);
}

rv_res rv_lw(rv *cpu, rv_u32 addr, rv_u32 *data) { /* load word */
  return rv_lh(cpu, addr, (rv_u16 *)data) ||
         rv_lh(cpu, addr + 2, ((rv_u16 *)data) + 1);
}

rv_res rv_sb(rv *cpu, rv_u32 addr, rv_u8 data) { /* store byte */
  return cpu->store_cb(cpu->user, addr, data);
}

rv_res rv_sh(rv *cpu, rv_u32 addr, rv_u16 data) { /* store half */
  return rv_sb(cpu, addr, data & 0xFF) || rv_sb(cpu, addr + 1, data >> 8);
}

rv_res rv_sw(rv *cpu, rv_u32 addr, rv_u32 data) { /* store word */
  return rv_sh(cpu, addr, data & 0xFFFF) || rv_sh(cpu, addr + 2, data >> 16);
}

rv_u32 rv_signext(rv_u32 x, rv_u32 h) { /* sign-extend x from h'th bit */
  return (0 - (x >> h)) << h | x;
}

#define RV_SBIT 0x80000000                  /* sign bit */
#define rv_sgn(x) (!!((rv_u32)(x)&RV_SBIT)) /* extract sign */

/* compute overflow */
#define rv_ovf(a, b, y) ((((a) ^ (b)) & RV_SBIT) && (((y) ^ (a)) & RV_SBIT))

#define rv_bf(i, h, l)                                                         \
  (((i) >> (l)) & ((1 << ((h) - (l) + 1)) - 1))    /* extract bit field */
#define rv_b(i, l) rv_bf(i, l, l)                  /* extract bit */
#define rv_tb(i, l, o) (rv_b(i, l) << (o))         /* translate bit */
#define rv_tbf(i, h, l, o) (rv_bf(i, h, l) << (o)) /* translate bit field */

/* instruction field macros */
#define rv_ioph(i) rv_bf(i, 6, 5)                     /* [h]i bits of opcode */
#define rv_iopl(i) rv_bf(i, 4, 2)                     /* [l]o bits of opcode */
#define rv_if3(i) rv_bf(i, 14, 12)                    /* funct3 */
#define rv_if7(i) rv_bf(i, 31, 25)                    /* funct7 */
#define rv_ird(i) rv_bf(i, 11, 7)                     /* rd */
#define rv_irs1(i) rv_bf(i, 19, 15)                   /* rs1 */
#define rv_irs2(i) rv_bf(i, 24, 20)                   /* rs2 */
#define rv_iimm_i(i) rv_signext(rv_bf(i, 31, 20), 11) /* imm. for I-type */
#define rv_iimm_iu(i) rv_bf(i, 31, 20) /* zero-ext'd. imm. for I-type */
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

rv_u32 rv_lr(rv *cpu, rv_u8 i) { /* load register */
  return cpu->r[i];
}

void rv_sr(rv *cpu, rv_u8 i, rv_u32 v) { /* store register */
  cpu->r[i] = i ? v : 0;
}

rv_res rv_csr(rv *cpu, rv_u32 csr, rv_u32 v, int w, rv_u32 *out) { /* csr op */
  rv_u32 *y /* output */, mask = (rv_u32)-1; /* all ones */
  if (csr == 0xF14) {                        /*C mhartid */
    y = &cpu->csrs.mhartid, mask = 0;
  } else if (csr == 0x305) { /*C mtvec */
    y = &cpu->csrs.mtvec;
  } else if (csr == 0x304) { /*C mie */
    y = &cpu->csrs.mie;
  } else if (csr == 0x300) { /*C mstatus */
    y = &cpu->csrs.mstatus, mask = 0x807FF615;
  } else if (csr == 0x310) { /*C mstatush */
    y = &cpu->csrs.mstatush, mask = 0x00000030;
  } else if (csr == 0x341) { /*C mepc */
    y = &cpu->csrs.mepc;
  } else if (csr == 0x342) { /*C mcause */
    y = &cpu->csrs.mcause;
  } else
    return RV_BAD;
  *out = *y;
  if (w && !mask) /* attempt to write to read-only reg */
    return RV_BAD;
  if (w) /* write relevant bits in v to p */
    *y = (*y & ~mask) | (v & mask);
  return RV_OK;
}

rv_res rv_scsr(rv *cpu, rv_u32 csr, rv_u32 v, rv_u32 *out) { /* store csr */
  rv_dbg("(SCSR) %04X <- %08X\n", csr, v);
  return rv_csr(cpu, csr, v, 1 /* write */, out);
}

rv_res rv_lcsr(rv *cpu, rv_u32 csr, rv_u32 *out) { /* load csr */
  rv_dbg("(LCSR) %04X\n", csr);
  return rv_csr(cpu, csr, 0, 0 /* read */, out);
}

rv_u32 rv_except(rv *cpu, rv_u32 cause) { /* set exception state */
  /* if mtvec[0], return 4 * cause + mtvec, otherwise just return mtvec */
  cpu->pc = ~(~cpu->csrs.mtvec | 1) + 4 * (cause - 1) * (cpu->csrs.mtvec & 1);
  return cause;
}

#ifdef RVM

#define rvm_lo(w) ((w) & (rv_u32)0xFFFFU) /* low 16 bits of 32-bit word */
#define rvm_hi(w) ((w) >> 16)             /* high 16 bits of 32-bit word */

rv_u32 rvm_ahh(rv_u32 a, rv_u32 b, rv_u32 cin, rv_u32 *cout) { /* adc 16 bit */
  rv_u32 sum = a + b + cin; /* cin must be less than 2. */
  *cout = rvm_hi(sum);
  return rvm_lo(sum);
}

rv_u32 rvm_mhh(rv_u32 a, rv_u32 b, rv_u32 *cout) { /* mul 16 bit */
  rv_u32 prod = a * b;
  *cout = rvm_hi(prod);
  return rvm_lo(prod);
}

rv_u32 rvm(rv_u32 a, rv_u32 b, rv_u32 *hi) { /* 32 x 32 -> 64 bit multiply */
  rv_u32 al = rvm_lo(a), ah = rvm_hi(a), bl = rvm_lo(b), bh = rvm_hi(b);
  rv_u32 qh, ql = rvm_mhh(al, bl, &qh);    /* qh, ql = al * bl      */
  rv_u32 rh, rl = rvm_mhh(al, bh, &rh);    /* rh, rl = al * bh      */
  rv_u32 sh, sl = rvm_mhh(ah, bl, &sh);    /* sh, sl = ah * bl      */
  rv_u32 th, tl = rvm_mhh(ah, bh, &th);    /* th, tl = ah * bh      */
  rv_u32 mc, m = rvm_ahh(rl, sl, 0, &mc);  /*  m, nc = rl + sl      */
  rv_u32 nc, n = rvm_ahh(rh, sh, mc, &nc); /*  n, nc = rh + sh + nc */
  rv_u32 x = ql;                           /*  x, 0  = ql           */
  rv_u32 yc, y = rvm_ahh(m, qh, 0, &yc);   /*  y, yc = qh + m       */
  rv_u32 zc, z = rvm_ahh(n, tl, yc, &zc);  /*  z, zc = tl + n  + yc */
  rv_u32 wc, w = rvm_ahh(th, nc, zc, &wc); /*  w, 0  = th + nc + zc */
  *hi = z | (w << 16);                     /* hi = (w, z)           */
  return x | (y << 16);                    /* lo = (y, x)           */
}

#endif

#ifdef RVC
#define rvc_op(c) rv_bf(c, 1, 0)           /* c. op */
#define rvc_f3(c) rv_bf(c, 15, 13)         /* c. funct3 */
#define rvc_rp(r) ((r) + 8)                /* c. r' register offsetter */
#define rvc_ird(c) rv_bf(c, 11, 7)         /* c. ci-format rd/rs1  */
#define rvc_irpl(c) rvc_rp(rv_bf(c, 4, 2)) /* c. rd'/rs2' (bits 4-2) */
#define rvc_irph(c) rvc_rp(rv_bf(c, 9, 7)) /* c. rd'/rs1' (bits 9-7) */
#define rvc_imm_ciw(c)                     /* CIW imm. for c.addi4spn */       \
  (rv_tbf(c, 10, 7, 6) | rv_tbf(c, 12, 11, 4) | rv_tb(c, 6, 3) | rv_tb(c, 5, 2))
#define rvc_imm_cl(c) /* CL imm. for c.lw/c.sw */                              \
  (rv_tb(c, 5, 6) | rv_tbf(c, 12, 10, 3) | rv_tb(c, 6, 2))
#define rvc_imm_ci(c) /* CI imm. for c.addi/c.li/c.lui */                      \
  (rv_signext(rv_tb(c, 12, 5), 5) | rv_bf(c, 6, 2))
#define rvc_imm_ci_b(c) /* CI imm. for c.addi16sp */                           \
  (rv_signext(rv_tb(c, 12, 9), 9) | rv_tbf(c, 4, 3, 7) | rv_tb(c, 5, 6) |      \
   rv_tb(c, 2, 5) | rv_tb(c, 6, 4))
#define rvc_imm_ci_c(c) /* CI imm. for c.lwsp */                               \
  (rv_tbf(c, 3, 2, 6) | rv_tb(c, 12, 5) | rv_tbf(c, 6, 4, 2))
#define rvc_imm_cj(c) /* CJ imm. for c.jalr/c.j */                             \
  (rv_signext(rv_tb(c, 12, 11), 11) | rv_tb(c, 8, 10) | rv_tbf(c, 10, 9, 8) |  \
   rv_tb(c, 6, 7) | rv_tb(c, 7, 6) | rv_tb(c, 2, 5) | rv_tb(c, 11, 4) |        \
   rv_tbf(c, 5, 3, 1))
#define rvc_imm_cb(c) /* CB imm. for c.beqz/c.bnez */                          \
  (rv_signext(rv_tb(c, 12, 8), 8) | rv_tbf(c, 6, 5, 6) | rv_tb(c, 2, 5) |      \
   rv_tbf(c, 11, 10, 3) | rv_tbf(c, 4, 3, 1))
#define rvc_imm_css(c) /* CSS imm. for c.swsp */                               \
  (rv_tbf(c, 8, 7, 6) | rv_tbf(c, 12, 9, 2))

/* macros to make all uncompressed instruction types */
#define rv_i_i(op, f3, rd, rs1, imm) /* I-type */                              \
  ((imm) << 20 | (rs1) << 15 | (f3) << 12 | (rd) << 7 | (op) << 2 | 3)
#define rv_i_s(op, f3, rs1, rs2, imm) /* S-type */                             \
  (rv_bf(imm, 11, 5) << 25 | (rs2) << 20 | (rs1) << 15 | (f3) << 12 |          \
   rv_bf(imm, 4, 0) << 7 | (op) << 2 | 3)
#define rv_i_u(op, rd, imm) /* U-type */                                       \
  ((imm) << 12 | (rd) << 7 | (op) << 2 | 3)
#define rv_i_r(op, f3, rd, rs1, rs2, f7) /* R-type */                          \
  ((f7) << 25 | (rs2) << 20 | (rs1) << 15 | (f3) << 12 | (rd) << 7 |           \
   (op) << 2 | 3)
#define rv_i_j(op, rd, imm) /* J-type */                                       \
  (rv_b(imm, 20) << 31 | rv_bf(imm, 10, 1) << 21 | rv_b(imm, 11) << 20 |       \
   rv_bf(imm, 19, 12) << 12 | (rd) << 7 | (op) << 2 | 3)
#define rv_i_b(op, f3, rs1, rs2, imm) /* B-type */                             \
  (rv_b(imm, 12) << 31 | rv_bf(imm, 10, 5) << 25 | (rs2) << 20 | (rs1) << 15 | \
   (f3) << 12 | rv_bf(imm, 4, 1) << 8 | rv_b(imm, 11) << 7 | (op) << 2 | 3)

rv_u32 rvc_inst(rv_u32 c) { /* decompress instruction */
  if (rvc_op(c) == 0) {
    if (rvc_f3(c) == 0 && c != 0) { /* c.addi4spn -> addi rd', x2, nzuimm */
      return rv_i_i(4, 0, rvc_irpl(c), 2, rvc_imm_ciw(c));
    } else if (c == 0) { /* illegal */
      return 0;
    } else if (rvc_f3(c) == 2) { /*I c.lw -> lw rd', offset(rs1') */
      return rv_i_i(0, 2, rvc_irpl(c), rvc_irph(c), rvc_imm_cl(c));
    } else if (rvc_f3(c) == 6) { /*I c.sw -> sw rs2', offset(rs1') */
      return rv_i_s(8, 2, rvc_irph(c), rvc_irpl(c), rvc_imm_cl(c));
    } else { /* illegal */
      return 0;
    }
  } else if (rvc_op(c) == 1) {
    if (rvc_f3(c) == 0) { /*I c.addi -> addi rd, rd, nzimm */
      return rv_i_i(4, 0, rvc_ird(c), rvc_ird(c), rvc_imm_ci(c));
    } else if (rvc_f3(c) == 1) { /*I c.jal -> jal x1, offset */
      return rv_i_j(27, 1, rvc_imm_cj(c));
    } else if (rvc_f3(c) == 2) { /*I c.li -> addi rd, x0, imm */
      return rv_i_i(4, 0, rvc_ird(c), 0, rvc_imm_ci(c));
    } else if (rvc_f3(c) == 3) { /* 01/011: LUI/ADDI16SP */
      if (rvc_ird(c) == 2) {     /*I c.addi16sp -> addi x2, x2, nzimm */
        return rv_i_i(4, 0, 2, 2, rvc_imm_ci_b(c));
      } else if (rvc_ird(c) != 0) { /*I c.lui -> lui rd, nzimm */
        return rv_i_u(13, rvc_ird(c), rvc_imm_ci(c));
      } else { /* illegal */
        return 0;
      }
    } else if (rvc_f3(c) == 4) {   /* 01/100: MISC-ALU */
      if (rv_bf(c, 11, 10) == 0) { /*I c.srli -> srli rd', rd', shamt */
        return rv_i_r(4, 5, rvc_irph(c), rvc_irph(c), rvc_imm_ci(c) & 0x1F, 0);
      } else if (rv_bf(c, 11, 10) == 1) { /*I c.srai -> srai rd', rd', shamt */
        return rv_i_r(4, 5, rvc_irph(c), rvc_irph(c), rvc_imm_ci(c) & 0x1F, 32);
      } else if (rv_bf(c, 11, 10) == 2) { /*I c.andi -> andi rd', rd', imm */
        return rv_i_i(4, 7, rvc_irph(c), rvc_irph(c), rvc_imm_ci(c));
      } else if (rv_bf(c, 11, 10) == 3) {
        if (rv_bf(c, 6, 5) == 0) { /*I c.sub -> sub rd', rd', rs2' */
          return rv_i_r(12, 0, rvc_irph(c), rvc_irph(c), rvc_irpl(c), 32);
        } else if (rv_bf(c, 6, 5) == 1) { /*I c.xor -> xor rd', rd', rs2' */
          return rv_i_r(12, 4, rvc_irph(c), rvc_irph(c), rvc_irpl(c), 0);
        } else if (rv_bf(c, 6, 5) == 2) { /*I c.or -> or rd', rd', rs2' */
          return rv_i_r(12, 6, rvc_irph(c), rvc_irph(c), rvc_irpl(c), 0);
        } else if (rv_bf(c, 6, 5) == 3) { /*I c.and -> and rd', rd', rs2' */
          return rv_i_r(12, 7, rvc_irph(c), rvc_irph(c), rvc_irpl(c), 0);
        } else { /* illegal */
          return 0;
        }
      } else { /* illegal */
        return 0;
      }
    } else if (rvc_f3(c) == 5) { /*I c.j -> jal x0, offset */
      return rv_i_j(27, 0, rvc_imm_cj(c));
    } else if (rvc_f3(c) == 6) { /*I c.beqz -> beq rs1' x0, offset */
      return rv_i_b(24, 0, rvc_irph(c), 0, rvc_imm_cb(c));
    } else if (rvc_f3(c) == 7) { /*I c.bnez -> bne rs1' x0, offset */
      return rv_i_b(24, 1, rvc_irph(c), 0, rvc_imm_cb(c));
    } else { /* illegal */
      return 0;
    }
  } else if (rvc_op(c) == 2) {
    if (rvc_f3(c) == 0) { /*I c.slli -> slli rd, rd, shamt */
      return rv_i_r(4, 1, rvc_ird(c), rvc_ird(c), rvc_imm_ci(c) & 0x1F, 0);
    } else if (rvc_f3(c) == 2) { /*I c.lwsp -> lw rd, offset(x2) */
      return rv_i_i(0, 2, rvc_ird(c), 2, rvc_imm_ci_c(c));
    } else if (rvc_f3(c) == 4 && !rv_b(c, 12) && !rv_bf(c, 6, 2)) {
      /*I c.jr -> jalr x0, 0(rs1) */
      return rv_i_i(25, 0, 0, rvc_ird(c), 0);
    } else if (rvc_f3(c) == 4 && !rv_b(c, 12)) { /*I c.mv -> add rd, x0, rs2 */
      return rv_i_r(12, 0, rvc_ird(c), 0, rv_bf(c, 6, 2), 0);
    } else if (rvc_f3(c) == 4 && rv_b(c, 12) && rvc_ird(c) &&
               !rv_bf(c, 6, 2)) { /*I c.jalr -> jalr x1, 0(rs1) */
      return rv_i_i(25, 0, 1, rvc_ird(c), 0);
    } else if (rvc_f3(c) == 4 && rv_b(c, 12) && rvc_ird(c) &&
               rv_bf(c, 6, 2)) { /*I c.add -> add rd, rd, rs2 */
      return rv_i_r(12, 0, rvc_ird(c), rvc_ird(c), rv_bf(c, 6, 2), 0);
    } else if (rvc_f3(c) == 6) { /*I c.swsp -> sw rs2, offset(x2) */
      return rv_i_s(8, 2, 2, rv_bf(c, 6, 2), rvc_imm_css(c));
    } else { /* illegal */
      return 0;
    }
  } else { /* illegal */
    return 0;
  }
}
#endif /* RVC */

rv_u32 rv_step(rv *cpu) {                  /* single step */
  rv_u32 i, err = rv_lw(cpu, cpu->pc, &i); /* fetch instruction into i */
  if (err)
    rv_dbg("(IF) %08X -> fault\n", cpu->pc);
  else
    rv_dbg("(IF) %08X -> %08X\n", cpu->pc, i);
  if (err)
    return rv_except(cpu, RV_EIFAULT);
  cpu->next_pc = cpu->pc + rv_isz(i);
#if RVC
  if (rv_isz(i) != 4)         /* if it's a compressed instruction... */
    i = rvc_inst(i & 0xFFFF); /* decompress it */
#endif
  if (rv_iopl(i) == 0) {
    if (rv_ioph(i) == 0) { /*Q 00/000: LOAD */
      rv_u32 addr = rv_lr(cpu, rv_irs1(i)) + rv_iimm_i(i);
      rv_u32 v;     /* loaded value */
      rv_u16 tmp16; /* temporary for 16-bit loads */
      rv_u8 tmp8;   /* temporary for  8-bit loads */
      rv_dbg("(L%.2s) %08X -> ",
             (const char *)"b\0h\0w\0XXbuhuXXXX" + 2 * rv_if3(i), addr);
      if (rv_if3(i) == 0) { /*I lb */
        err = rv_lb(cpu, addr, &tmp8);
        v = rv_signext((rv_u32)tmp8, 7);
      } else if (rv_if3(i) == 1) { /*I lh */
        err = rv_lh(cpu, addr, &tmp16);
        v = rv_signext((rv_u32)tmp16, 15);
      } else if (rv_if3(i) == 2) { /*I lw */
        err = rv_lw(cpu, addr, &v);
      } else if (rv_if3(i) == 4) { /*I lbu */
        err = rv_lb(cpu, addr, &tmp8);
        v = (rv_u32)tmp8;
      } else if (rv_if3(i) == 5) { /*I lhu */
        err = rv_lh(cpu, addr, &tmp16);
        v = (rv_u32)tmp16;
      } else
        return rv_except(cpu, RV_EILL);
      if (err)
        rv_dbg("fault\n");
      else
        rv_dbg("%08X\n", v);
      if (err)
        return rv_except(cpu, RV_ELFAULT);
      else
        rv_sr(cpu, rv_ird(i), v);
    } else if (rv_ioph(i) == 1) { /*Q 01/000: STORE */
      rv_u32 addr = rv_lr(cpu, rv_irs1(i)) + rv_iimm_s(i);
      rv_dbg("(S%.2s) %08X <- %08X",
             (char *)"b\0h\0w\0XXXXXXXXXX" + 2 * rv_if3(i), addr,
             rv_lr(cpu, rv_irs2(i)));
      if (rv_if3(i) == 0) { /*I sb */
        err = rv_sb(cpu, addr, rv_lr(cpu, rv_irs2(i)) & 0xFF);
      } else if (rv_if3(i) == 1) { /*I sh */
        err = rv_sh(cpu, addr, rv_lr(cpu, rv_irs2(i)) & 0xFFFF);
      } else if (rv_if3(i) == 2) { /*I sw */
        err = rv_sw(cpu, addr, rv_lr(cpu, rv_irs2(i)));
      } else
        return rv_except(cpu, RV_EILL);
      rv_dbg(err ? "-> fault\n" : "\n");
      if (err)
        return rv_except(cpu, RV_ESFAULT);
    } else if (rv_ioph(i) == 3) { /*Q 11/000: BRANCH */
      rv_u32 a = rv_lr(cpu, rv_irs1(i)), b = rv_lr(cpu, rv_irs2(i));
      rv_u32 y = a - b; /* comparison value */
      rv_u32 zero = !y, sgn = rv_sgn(y), ovf = rv_ovf(a, b, y), carry = y > a;
      rv_u32 targ = cpu->pc + rv_iimm_b(i);   /* computed branch target */
      if ((rv_if3(i) == 0 && zero) ||         /*I beq */
          (rv_if3(i) == 1 && !zero) ||        /*I bne */
          (rv_if3(i) == 4 && (sgn != ovf)) || /*I blt */
          (rv_if3(i) == 5 && (sgn == ovf)) || /*I bge */
          (rv_if3(i) == 6 && carry) ||        /*I bltu */
          (rv_if3(i) == 7 && !carry)          /*I bgtu */
      ) {
        cpu->next_pc = targ; /* take branch */
      } else if (rv_if3(i) == 2 || rv_if3(i) == 3)
        return rv_except(cpu, RV_EILL);
      /* default: don't take branch */
    } else
      return rv_except(cpu, RV_EILL);
  } else if (rv_iopl(i) == 1) {
    if (rv_ioph(i) == 3 && rv_if3(i) == 0) { /*Q 11/001: JALR */
      rv_u32 target = (rv_lr(cpu, rv_irs1(i)) + rv_iimm_i(i)); /*I jalr */
      rv_sr(cpu, rv_ird(i), cpu->next_pc);
      cpu->next_pc = target & (~(rv_u32)1); /* target is two-byte aligned */
    } else
      return rv_except(cpu, RV_EILL);
  } else if (rv_iopl(i) == 3) {
    if (rv_ioph(i) == 0) {            /*Q 00/011: MISC-MEM */
      if (rv_if3(i) == 0) {           /*I fence */
        rv_u32 fm = rv_bf(i, 31, 28); /* extract fm field */
        if (fm && fm != 8)
          return rv_except(cpu, RV_EILL); /* fm must be 0/8, others reserved */
      } else if (rv_if3(i) == 1) {        /*I fence.i */
      } else
        return rv_except(cpu, RV_EILL);
    } else if (rv_ioph(i) == 3) {          /*Q 11/011: JAL */
      rv_sr(cpu, rv_ird(i), cpu->next_pc); /*I jal */
      cpu->next_pc = cpu->pc + rv_iimm_j(i);
    } else
      return rv_except(cpu, RV_EILL);
  } else if (rv_iopl(i) == 4) { /* ALU section */
    if (rv_ioph(i) == 0 ||      /*Q 00/100: OP-IMM */
        rv_ioph(i) == 1) {      /*Q 01/100: OP */
      rv_u32 a = rv_lr(cpu, rv_irs1(i));
      rv_u32 b = rv_ioph(i) ? rv_lr(cpu, rv_irs2(i)) : rv_iimm_i(i);
      rv_u32 s = (rv_ioph(i) || rv_if3(i)) ? rv_b(i, 30) : 0; /* alt. ALU op */
      rv_u32 y /* result */, sh = b & 0x1F;                   /* shift amount */
#if RVM
      if (!rv_ioph(i) || !rv_b(i, 25)) {
#endif
        if (rv_if3(i) == 0)      /*I add, addi, sub */
          y = s ? a - b : a + b; /* subtract if alt. op, otherwise add */
        else if (rv_if3(i) == 1) /*I sll, slli */
          y = a << sh;
        else if (rv_if3(i) == 2) /*I slt, slti */
          y = rv_ovf(a, b, a - b) != rv_sgn(a - b);
        else if (rv_if3(i) == 3) /*I sltu, sltiu */
          y = (a - b) > a;
        else if (rv_if3(i) == 4) /*I xor, xori */
          y = a ^ b;
        else if (rv_if3(i) == 5) /*I srl, srli, sra, srai */
          y = (a >> sh) | ((0U - (s && rv_sgn(a))) << (0x1F - sh));
        else if (rv_if3(i) == 6) /*I or, ori */
          y = a | b;
        else /*I and, andi */
          y = a & b;
#if RVM
      } else {
        rv_u32 as = 0 /* sgn(a) */, bs = 0 /* sgn(b) */, ylo, yhi; /* result */
        if (rv_if3(i) < 4) {              /*I mul, mulh, mulhsu, mulhu */
          if (rv_if3(i) < 3 && rv_sgn(a)) /* a is signed iff f3 in {0, 1, 2} */
            a = ~a + 1, as = 1;
          if (rv_if3(i) < 2 && rv_sgn(b)) /* b is signed iff f3 in {0, 1} */
            b = ~b + 1, bs = 1;
          ylo = rvm(a, b, &yhi); /* perform multiply */
          if (as ^ bs) {         /* invert output quantity if result <0 */
            ylo = ~ylo + 1, yhi = ~yhi; /* two's complement */
            if (!ylo)                   /* carry out of lo */
              yhi++;                    /* propagate carry to hi */
          }
          y = rv_if3(i) ? yhi : ylo; /* return hi word if mulh, otherwise lo */
        } else {
          if (rv_if3(i) == 4) /*I div */
            y = b ? (rv_u32)((rv_s32)a / (rv_s32)b) : (rv_u32)(-1);
          else if (rv_if3(i) == 5) /*I divu */
            y = b ? (a / b) : (rv_u32)(-1);
          else if (rv_if3(i) == 6) /*I rem */
            y = (rv_u32)((rv_s32)a % (rv_s32)b);
          else /*I remu */
            y = a % b;
        }
      }
#endif
      rv_sr(cpu, rv_ird(i), y);   /* set register to ALU output */
    } else if (rv_ioph(i) == 3) { /*Q 11/100: SYSTEM */
      rv_u32 csr = rv_iimm_iu(i) /* CSR number */, y; /* result */
      rv_u32 s = rv_if3(i) & 4 ? rv_irs1(i) : rv_lr(cpu, rv_irs1(i)); /* uimm */
      if ((rv_if3(i) & 3) == 1) {    /*I csrrw, csrrwi */
        if (rv_irs1(i)) {            /* perform CSR load */
          if (rv_lcsr(cpu, csr, &y)) /* load CSR into y */
            return rv_except(cpu, RV_EILL);
          if (rv_ird(i))
            rv_sr(cpu, rv_ird(i), y); /* store y into rd */
        }
        if (rv_scsr(cpu, csr, s, &y)) /* set CSR to s [y unused]*/
          return rv_except(cpu, RV_EILL);
      } else if ((rv_if3(i) & 3) == 2) { /*I csrrs, csrrsi */
        if (rv_lcsr(cpu, csr, &y))       /* load CSR into y */
          return rv_except(cpu, RV_EILL);
        rv_sr(cpu, rv_ird(i), y);                       /* store y into rd */
        if (rv_irs1(i) && rv_scsr(cpu, csr, y | s, &y)) /*     y|s into CSR */
          return rv_except(cpu, RV_EILL);
      } else if ((rv_if3(i) & 3) == 3) { /*I csrrc, csrrci */
        if (rv_lcsr(cpu, csr, &y))       /* load CSR into y */
          return rv_except(cpu, RV_EILL);
        rv_sr(cpu, rv_ird(i), y);                        /* store y into rd */
        if (rv_irs1(i) && rv_scsr(cpu, csr, y & ~s, &y)) /*    y&~s into CSR */
          return rv_except(cpu, RV_EILL);
      } else if (!rv_if3(i)) {
        if (!rv_ird(i)) {
          if (!rv_irs1(i) && rv_irs2(i) == 2 && rv_if7(i) == 24) { /*I mret */
            cpu->next_pc = cpu->csrs.mepc; /* return from exception routine */
          } else if (!rv_irs1(i) && !rv_irs2(i) && !rv_if7(i)) { /*I ecall */
            return rv_except(cpu, RV_EECALL);
          } else if (!rv_irs1(i) && rv_irs2(i) == 1 && !rv_if7(i)) {
            return rv_except(cpu, RV_EBP); /*I ebreak */
          } else
            return rv_except(cpu, RV_EILL);
        } else
          return rv_except(cpu, RV_EILL);
      } else
        return rv_except(cpu, RV_EILL);
    } else
      return rv_except(cpu, RV_EILL);
  } else if (rv_iopl(i) == 5) {
    if (rv_ioph(i) == 0) {                           /*Q 00/101: AUIPC */
      rv_sr(cpu, rv_ird(i), rv_iimm_u(i) + cpu->pc); /*I auipc */
    } else if (rv_ioph(i) == 1) {                    /*Q 01/101: LUI */
      rv_sr(cpu, rv_ird(i), rv_iimm_u(i));           /*I lui */
    } else
      return rv_except(cpu, RV_EILL);
  } else
    return rv_except(cpu, RV_EILL);
  cpu->pc = cpu->next_pc;
  return 0;
}
