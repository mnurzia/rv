#include "rv.h"

#include <string.h>

#define RV_RESET_VEC 0x80000000 /* CPU reset vector */

#if defined(RV_VERBOSE)
#include <stdio.h>
#define rv_dbg printf
#else
static void rv_dbg(const char *fmt, ...) {
  (void)fmt;
  return;
}
#endif

void rv_init(rv *cpu, void *user, rv_bus_cb bus_cb) {
  memset(cpu, 0, sizeof(*cpu));
  cpu->user = user;
  cpu->bus_cb = bus_cb;
  cpu->pc = RV_RESET_VEC;
  cpu->csrs.misa = (1 << 30) | (RVM << 12) | (RVC << 2) | (RVA << 0) |
                   (RVS << 18) | (RVS << 20);
  cpu->priv = RV_PMACH;
}

/* sign-extend x from h'th bit */
static rv_u32 rv_signext(rv_u32 x, rv_u32 h) { return (0 - (x >> h)) << h | x; }

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
#define rv_if5(i) rv_bf(i, 31, 27)                    /* funct5 */
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

/* load register */
static rv_u32 rv_lr(rv *cpu, rv_u8 i) {
  /* rv_dbg(" (LR) x%02i -> %08X\n", i, cpu->r[i]); */
  return cpu->r[i];
}

/* store register */
static void rv_sr(rv *cpu, rv_u8 i, rv_u32 v) {
  /* rv_dbg(" (SR) x%02i <- %08X\n", i, v); */
  cpu->r[i] = i ? v : 0;
}

/* csr operation */
static rv_res rv_csr(rv *cpu, rv_u32 csr, rv_u32 v, rv_u32 w, rv_u32 *out) {
  rv_u32 *y /* output */, wmask = (rv_u32)-1, rmask = wmask;
  rv_u32 rw = rv_bf(csr, 11, 10), priv = rv_bf(csr, 9, 8);
  if ((w && rw == 3) || cpu->priv < priv)
    return RV_BAD;
  if (csr == 0x100) { /*C sstatus */
    y = &cpu->csrs.mstatus, wmask = rmask = 0x800DE762;
  } else if (csr == 0x104) { /*C sie */
    y = &cpu->csrs.mie, wmask = 0x222, rmask = 0x222;
  } else if (csr == 0x105) { /*C stvec */
    y = &cpu->csrs.stvec;
  } else if (csr == 0x106) { /*C scounteren */
    y = &cpu->csrs.scounteren, wmask = 0;
  } else if (csr == 0x140) { /*C sscratch */
    y = &cpu->csrs.sscratch;
  } else if (csr == 0x141) { /*C sepc */
    y = &cpu->csrs.sepc;
  } else if (csr == 0x142) { /*C scause */
    y = &cpu->csrs.scause;
  } else if (csr == 0x143) { /*C stval */
    y = &cpu->csrs.stval;
  } else if (csr == 0x144) { /*C sip */
    y = &cpu->csrs.mip, wmask = 0x222, rmask = 0x222;
  } else if (csr == 0x180) { /*C satp */
    y = &cpu->csrs.satp;
  } else if (csr == 0x300) { /*C mstatus */
    y = &cpu->csrs.mstatus, wmask = rmask = 0x807FFFE6;
  } else if (csr == 0x301) { /*C misa */
    y = &cpu->csrs.misa, wmask = 0;
  } else if (csr == 0x302) { /*C medeleg */
    y = &cpu->csrs.medeleg;
  } else if (csr == 0x303) { /*C mideleg */
    y = &cpu->csrs.mideleg;
  } else if (csr == 0x304) { /*C mie */
    y = &cpu->csrs.mie, wmask = 0xAAA;
  } else if (csr == 0x305) { /*C mtvec */
    y = &cpu->csrs.mtvec;
  } else if (csr == 0x306) { /*C mcounteren */
    y = &cpu->csrs.mcounteren, wmask = 0;
  } else if (csr == 0x310) { /*C mstatush */
    y = &cpu->csrs.mstatush, wmask = rmask = 0x00000030;
  } else if (csr == 0x340) { /*C mscratch */
    y = &cpu->csrs.mscratch;
  } else if (csr == 0x341) { /*C mepc */
    y = &cpu->csrs.mepc;
  } else if (csr == 0x342) { /*C mcause */
    y = &cpu->csrs.mcause;
  } else if (csr == 0x343) { /*C mtval */
    y = &cpu->csrs.mtval, wmask = 0;
  } else if (csr == 0x344) { /*C mip */
    y = &cpu->csrs.mip, wmask = 0xAAA;
  } else if (csr == 0xC01) { /*C time */
    y = &cpu->csrs.mtime;
  } else if (csr == 0xC81) { /*C timeh */
    y = &cpu->csrs.mtimeh;
  } else if (csr == 0xF11) { /*C mvendorid */
    y = &cpu->csrs.mvendorid, wmask = 0;
  } else if (csr == 0xF12) { /*C marchid */
    y = &cpu->csrs.marchid, wmask = 0;
  } else if (csr == 0xF13) { /*C mimpid */
    y = &cpu->csrs.mimpid, wmask = 0;
  } else if (csr == 0xF14) { /*C mhartid */
    y = &cpu->csrs.mhartid;
  } else
    return RV_BAD;
  *out = *y & rmask;
  *y = w ? (*y & ~wmask) | (v & wmask) : *y; /* write relevant bits in v to p */
  return RV_OK;
}

/* store csr */
static rv_res rv_scsr(rv *cpu, rv_u32 csr, rv_u32 v, rv_u32 *out) {
  rv_dbg("(SCSR@%X) %04X <- %08X\n", cpu->priv, csr, v);
  return rv_csr(cpu, csr, v, 1 /* write */, out);
}

/* load csr */
static rv_res rv_lcsr(rv *cpu, rv_u32 csr, rv_u32 *out) {
  rv_dbg("(LCSR@%X) %04X\n", cpu->priv, csr);
  return rv_csr(cpu, csr, 0, 0 /* read */, out);
}

/* set exception state */
static rv_u32 rv_except(rv *cpu, rv_u32 cause, rv_u32 tval) {
  rv_u32 is_interrupt = !!(cause & 0x80000000), rcause = cause & ~0x80000000;
  rv_priv xp = /* destination privilege, switch from y = cpu->priv to this */
      (cpu->priv < RV_PMACH) &&
              ((is_interrupt ? cpu->csrs.mideleg : cpu->csrs.medeleg) &
               (1 << rcause))
          ? RV_PSUPER
          : RV_PMACH;
  rv_u32 *xtvec = &cpu->csrs.mtvec, *xepc = &cpu->csrs.mepc,
         *xcause = &cpu->csrs.mcause, *xtval = &cpu->csrs.mtval;
  rv_u32 ie = rv_b(cpu->csrs.mstatus, xp); /* xie */
  rv_dbg("(E) %08X %08X [%08X]\n", cpu->pc, cause, rcause);
  if (xp == RV_PSUPER) /* select s-mode regs */
    xtvec = &cpu->csrs.stvec, xepc = &cpu->csrs.sepc,
    xcause = &cpu->csrs.scause, xtval = &cpu->csrs.stval;
  cpu->csrs.mstatus &=
      (xp == RV_PMACH ? 0xFFFFE777 : 0x807FFEC9); /* {xpp, xie, xpie} <- 0 */
  cpu->csrs.mstatus |= (cpu->priv << (xp == RV_PMACH ? 11 : 8)) /* xpp <- y */
                       | ie << (4 + xp);   /* xpie <- xie */
  *xepc = cpu->pc;                         /* xepc <- pc */
  *xcause = rcause | (is_interrupt << 31); /* xcause <- cause */
  *xtval = tval;                           /* xtval <- tval */
  cpu->priv = xp;                          /* priv <- x */
  /* if tvec[0], return 4 * cause + vec, otherwise just return vec */
  cpu->pc = *xtvec & ~3U + 4 * rcause * (*xtvec & 1 && is_interrupt);
  return cause;
}

/* sv32 virtual address -> physical address */
rv_u32 rv_vmm(rv *cpu, rv_u32 va, rv_u32 *pa, rv_access access) {
  rv_u32 epriv = rv_b(cpu->csrs.mstatus, 17) && access != RV_AX
                     ? rv_bf(cpu->csrs.mstatus, 12, 11)
                     : cpu->priv; /* effective privilege mode */
  if (!rv_b(cpu->csrs.satp, 31) || epriv > RV_PSUPER) {
    *pa = va; /* if !satp.mode, no translation */
  } else {
    rv_u32 ppn = rv_bf(cpu->csrs.satp, 21, 0) /* satp.ppn */,
           a = ppn << 12 /* a = satp.ppn * PAGESIZE */,
           i = 1 /* i = LEVELS - 1 */, pte, pte_addr, pte_access, tlb = 0;
    if (cpu->tlb_valid && cpu->tlb_va == (va & ~0xFFFU))
      pte = cpu->tlb_pte, tlb = 1, i = cpu->tlb_i;
    while (!tlb) {
      pte_addr = a + (rv_bf(va, 21 + 10 * i, 12 + 10 * i)
                      << 2); /* pte_addr = a + va.vpn[i] * PTESIZE */
      if (cpu->bus_cb(cpu->user, pte_addr, (rv_u8 *)&pte, 0, 4))
        return RV_BAD;
      if (!rv_b(pte, 0) || (!rv_b(pte, 1) && rv_b(pte, 2)))
        return RV_PAGEFAULT; /* pte.v == 0, or (pte.r == 0 and pte.w == 1) */
      if (rv_b(pte, 1) || rv_b(pte, 3))
        break; /* if pte.r = 1 or pte.x = 1, this is a leaf page */
      if (i == 0)
        return RV_PAGEFAULT; /* if i - 1 < 0, pagefault */
      i = i - 1;
      a = rv_tbf(pte, 31, 10, 12); /* a = pte.ppn[*] * PAGESIZE */
    }
    if (!tlb)
      cpu->tlb_valid = 1, cpu->tlb_va = va & ~0xFFFU, cpu->tlb_pte = pte,
      cpu->tlb_i = i;
    if (!rv_b(pte, 4) && epriv == RV_PUSER)
      return RV_PAGEFAULT; /* u-bit not set */
    if (epriv == RV_PSUPER && !rv_b(cpu->csrs.mstatus, 18) && rv_b(pte, 4))
      return RV_PAGEFAULT; /* mstatus.sum bit wrong */
    pte_access = rv_bf(pte, 3, 1);
    if (rv_b(cpu->csrs.mstatus, 19))
      pte_access |= rv_b(pte_access, 2) << 1; /* pte.r = pte.x if MXR bit set */
    if (~pte_access & access)
      return RV_PAGEFAULT; /* mismatching access type (an actual fault) */
    if (i && rv_bf(pte, 19, 10))
      return RV_PAGEFAULT; /* misaligned megapage */
    if (!rv_b(pte, 6) || ((access & RV_AW) && !rv_b(pte, 7)))
      return RV_PAGEFAULT; /* pte.a == 0, or we are writing and pte.d == 0 */
    /* pa.ppn[1:i] = pte.ppn[1:i] */
    *pa = rv_tbf(pte, 31, 10 + 10 * i, 12 + 10 * i) | rv_bf(va, 11 + 10 * i, 0);
  }
  return RV_OK;
}

#ifdef RVM

#define rvm_lo(w) ((w) & (rv_u32)0xFFFFU) /* low 16 bits of 32-bit word */
#define rvm_hi(w) ((w) >> 16)             /* high 16 bits of 32-bit word */

/* adc 16 bit */
static rv_u32 rvm_ahh(rv_u32 a, rv_u32 b, rv_u32 cin, rv_u32 *cout) {
  rv_u32 sum = a + b + cin; /* cin must be less than 2. */
  *cout = rvm_hi(sum);
  return rvm_lo(sum);
}

/* mul 16 bit */
static rv_u32 rvm_mhh(rv_u32 a, rv_u32 b, rv_u32 *cout) {
  rv_u32 prod = a * b;
  *cout = rvm_hi(prod);
  return rvm_lo(prod);
}

/* 32 x 32 -> 64 bit multiply */
static rv_u32 rvm(rv_u32 a, rv_u32 b, rv_u32 *hi) {
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
  (rv_tbf(c, 10, 7, 6) | rv_tbf(c, 12, 11, 4) | rv_tb(c, 6, 2) | rv_tb(c, 5, 3))
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
  (rv_signext(rv_tb(c, 12, 11), 11) | rv_tb(c, 11, 4) | rv_tbf(c, 10, 9, 8) |  \
   rv_tb(c, 8, 10) | rv_tb(c, 7, 6) | rv_tb(c, 6, 7) | rv_tbf(c, 5, 3, 1) |    \
   rv_tb(c, 2, 5))
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

/* decompress instruction */
static rv_u32 rvc_inst(rv_u32 c) {
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
    } else if (rvc_f3(c) == 4 && rv_b(c, 12) && !rvc_ird(c) &&
               !rv_bf(c, 6, 2)) { /*I c.ebreak -> ebreak */
      return rv_i_i(28, 0, 0, 0, 1);
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

/* perform a bus access. access == RV_AW stores data. */
static rv_u32 rv_bus(rv *cpu, rv_u32 *va, rv_u8 *data, rv_u32 width,
                     rv_access access) {
  rv_u32 err, pa /* physical address */;
  if ((err = rv_vmm(cpu, *va, &pa, access)))
    return err; /* page or access fault */
  if (((pa + width - 1) ^ pa) & ~0xFFFU) /* page bound overrun */ {
    rv_u32 w0 = 0x1000 - (*va & 0xFFF); /* load this many bytes from 1st page */
    if ((err = cpu->bus_cb(cpu->user, pa, data, access == RV_AW, w0)))
      return err;
    width -= w0, *va += w0, data += w0;
    if ((err = rv_vmm(cpu, *va, &pa, RV_AW)))
      return err;
  }
  return cpu->bus_cb(cpu->user, pa, data, access == RV_AW, width);
}

/* instruction fetch */
static rv_u32 rv_if(rv *cpu, rv_u32 *i) {
  rv_u32 err, bound = (cpu->pc ^ (cpu->pc + 3)) & ~0xFFFU, pc = cpu->pc & ~3U;
  if (cpu->pc & 2 || bound) { /* perform if in <=2 2-byte fetches */
    rv_u32 ia = 0, ib = 0;
    pc = cpu->pc;
    if ((err = rv_bus(cpu, &pc, (rv_u8 *)&ia, 2, RV_AX)))
      return err;
    if (rv_isz(ia) == 4 && (pc += 2, 1) &&
        (err = rv_bus(cpu, &pc, (rv_u8 *)&ib, 2, RV_AX))) {
      cpu->pc += 2;
      return err;
    }
    *i = (rv_u32)ia | (rv_u32)ib << 16U;
  } else if ((err = rv_bus(cpu, &pc, (rv_u8 *)i, 4, RV_AX))) /* 4-byte fetch */
    return err;
  cpu->next_pc = cpu->pc + rv_isz(*i);
#if RVC
  if (rv_isz(*i) < 4)
    *i = rvc_inst(*i & 0xFFFF);
#endif
  return RV_OK;
}

/* single step */
rv_u32 rv_step(rv *cpu) {
  rv_u32 i, err = rv_if(cpu, &i); /* fetch instruction into i */
  if (err)
    rv_dbg("(IF) %08X -> fault\n", cpu->pc);
  else
    rv_dbg("(IF) %08X -> %08X\n", cpu->pc, i);
  if (err)
    return rv_except(cpu, err == RV_BAD ? RV_EIFAULT : RV_EIPAGE, cpu->pc);
  if (rv_iopl(i) == 0) {
    if (rv_ioph(i) == 0) { /*Q 00/000: LOAD */
      rv_u32 va = rv_lr(cpu, rv_irs1(i)) + rv_iimm_i(i);
      rv_u32 v = 0 /* loaded value */, w /* value width */, sx /* sign ext. */;
      rv_dbg("(L%.2s) %08X -> ",
             (const char *)"b\0h\0w\0XXbuhuXXXX" + 2 * rv_if3(i), va);
      w = 1 << (rv_if3(i) & 3), sx = ~rv_if3(i) & 4; /*I lb, lh, lw, lbu, lhu */
      if ((err = rv_bus(cpu, &va, (rv_u8 *)&v, w, RV_AR)))
        return rv_except(cpu, err == RV_BAD ? RV_ELFAULT : RV_ELPAGE, va);
      if ((rv_if3(i) & 3) == 3)
        return rv_except(cpu, RV_EILL, cpu->pc);
      if (sx)
        v = rv_signext(v, (w * 8 - 1));
      if (err)
        rv_dbg("fault\n");
      else
        rv_dbg("%08X\n", v);
      rv_sr(cpu, rv_ird(i), v);
    } else if (rv_ioph(i) == 1) { /*Q 01/000: STORE */
      rv_u32 va = rv_lr(cpu, rv_irs1(i)) + rv_iimm_s(i);
      rv_u32 w = 1 << (rv_if3(i) & 3), y = rv_lr(cpu, rv_irs2(i));
      rv_dbg("(S%.2s) %08X <- %08X",
             (char *)"b\0h\0w\0XXXXXXXXXX" + 2 * rv_if3(i), va, y);
      if (rv_if3(i) > 2)
        return rv_except(cpu, RV_EILL, cpu->pc);
      if ((err = rv_bus(cpu, &va, (rv_u8 *)&y, w, RV_AW)))
        return rv_except(cpu, err == RV_BAD ? RV_ESFAULT : RV_ESPAGE, va);
      rv_dbg(err ? " -> fault\n" : "\n");
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
        return rv_except(cpu, RV_EILL, cpu->pc);
      /* default: don't take branch */
    } else
      return rv_except(cpu, RV_EILL, cpu->pc);
  } else if (rv_iopl(i) == 1) {
    if (rv_ioph(i) == 3 && rv_if3(i) == 0) { /*Q 11/001: JALR */
      rv_u32 target = (rv_lr(cpu, rv_irs1(i)) + rv_iimm_i(i)); /*I jalr */
      rv_sr(cpu, rv_ird(i), cpu->next_pc);
      cpu->next_pc = target & (~(rv_u32)1); /* target is two-byte aligned */
    } else
      return rv_except(cpu, RV_EILL, cpu->pc);
  } else if (rv_iopl(i) == 3) {
    if (rv_ioph(i) == 0) {            /*Q 00/011: MISC-MEM */
      if (rv_if3(i) == 0) {           /*I fence */
        rv_u32 fm = rv_bf(i, 31, 28); /* extract fm field */
        if (fm && fm != 8)
          return rv_except(cpu, RV_EILL, cpu->pc);
      } else if (rv_if3(i) == 1) { /*I fence.i */
      } else
        return rv_except(cpu, RV_EILL, cpu->pc);
    }
#ifdef RVA
    else if (rv_ioph(i) == 1) { /*Q 01/011: AMO */
      rv_u32 a = rv_lr(cpu, rv_irs1(i)), b = rv_lr(cpu, rv_irs2(i)), x = 0, y;
      rv_u32 va = a;
      if (rv_bf(i, 14, 12) != 2) { /* width must be 2 */
        return rv_except(cpu, RV_EILL, cpu->pc);
      } else if (va & 3) {
        return rv_except(cpu, RV_ESALIGN, va); /* misaligned atomic :( */
      } else if (rv_if5(i) == 2 && !b) {       /*I lr.w */
        if ((err = rv_bus(cpu, &va, (rv_u8 *)&x, 4, RV_AR)))
          return rv_except(cpu, err == RV_BAD ? RV_ELFAULT : RV_ELPAGE, va);
        cpu->reserve = va, cpu->reserve_valid = 1;
      } else if (rv_if5(i) == 3) { /*I sc.w */
        if (cpu->reserve_valid && cpu->reserve_valid-- && cpu->reserve == va) {
          rv_sr(cpu, rv_ird(i), 0);
          if ((err = rv_bus(cpu, &va, (rv_u8 *)&b, 4, RV_AW)))
            return rv_except(cpu, err == RV_BAD ? RV_ESFAULT : RV_ESPAGE, va);
        } else
          x = 1;
      } else {
        if ((err = rv_bus(cpu, &va, (rv_u8 *)&x, 4, RV_AR)))
          return rv_except(cpu, err == RV_BAD ? RV_ELFAULT : RV_ELPAGE, va);
        if (rv_if5(i) == 0) /*I amoadd.w */
          y = x + b;
        else if (rv_if5(i) == 1) /*I amoswap.w */
          y = b;
        else if (rv_if5(i) == 4) /*I amoxor.w */
          y = x ^ b;
        else if (rv_if5(i) == 8) /*I amoor.w */
          y = x | b;
        else if (rv_if5(i) == 12) /*I amoand.w */
          y = x & b;
        else if (rv_if5(i) == 16) /*I amomin.w */
          y = rv_sgn(x - b) != rv_ovf(x, b, x - b) ? x : b;
        else if (rv_if5(i) == 20) /*I amomax.w */
          y = rv_sgn(x - b) == rv_ovf(x, b, x - b) ? x : b;
        else if (rv_if5(i) == 24) /*I amominu.w */
          y = (x - b) > x ? x : b;
        else if (rv_if5(i) == 28) /*I amomaxu.w */
          y = (x - b) <= x ? x : b;
        else
          return rv_except(cpu, RV_EILL, cpu->pc);
        if ((err = rv_bus(cpu, &va, (rv_u8 *)&y, 4, RV_AW)))
          return rv_except(cpu, err == RV_BAD ? RV_ESFAULT : RV_ESPAGE, va);
      }
      rv_sr(cpu, rv_ird(i), x);
    }
#endif
    else if (rv_ioph(i) == 3) {            /*Q 11/011: JAL */
      rv_sr(cpu, rv_ird(i), cpu->next_pc); /*I jal */
      cpu->next_pc = cpu->pc + rv_iimm_j(i);
    } else
      return rv_except(cpu, RV_EILL, cpu->pc);
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
      rv_u32 csr = rv_iimm_iu(i) /* CSR number */, y /* result */;
      rv_u32 s = rv_if3(i) & 4 ? rv_irs1(i) : rv_lr(cpu, rv_irs1(i)); /* uimm */
      if ((rv_if3(i) & 3) == 1) {    /*I csrrw, csrrwi */
        if (rv_irs1(i)) {            /* perform CSR load */
          if (rv_lcsr(cpu, csr, &y)) /* load CSR into y */
            return rv_except(cpu, RV_EILL, cpu->pc);
          if (rv_ird(i))
            rv_sr(cpu, rv_ird(i), y); /* store y into rd */
        }
        if (rv_scsr(cpu, csr, s, &y)) /* set CSR to s [y unused] */
          return rv_except(cpu, RV_EILL, cpu->pc);
      } else if ((rv_if3(i) & 3) == 2) { /*I csrrs, csrrsi */
        if (rv_lcsr(cpu, csr, &y))       /* load CSR into y */
          return rv_except(cpu, RV_EILL, cpu->pc);
        rv_sr(cpu, rv_ird(i), y);                       /* store y into rd */
        if (rv_irs1(i) && rv_scsr(cpu, csr, y | s, &y)) /*     y|s into CSR */
          return rv_except(cpu, RV_EILL, cpu->pc);
      } else if ((rv_if3(i) & 3) == 3) { /*I csrrc, csrrci */
        if (rv_lcsr(cpu, csr, &y))       /* load CSR into y */
          return rv_except(cpu, RV_EILL, cpu->pc);
        rv_sr(cpu, rv_ird(i), y);                        /* store y into rd */
        if (rv_irs1(i) && rv_scsr(cpu, csr, y & ~s, &y)) /*    y&~s into CSR */
          return rv_except(cpu, RV_EILL, cpu->pc);
      } else if (!rv_if3(i)) {
        if (!rv_ird(i)) {
          if (!rv_irs1(i) && rv_irs2(i) == 2 &&
              (rv_if7(i) == 8 || rv_if7(i) == 24)) { /*I mret, sret */
            rv_u32 xp = rv_if7(i) >> 3;              /* instruction privilege */
            rv_u32 yp = cpu->csrs.mstatus >> (xp == RV_PMACH ? 11 : 8) & xp;
            rv_u32 pie = rv_b(cpu->csrs.mstatus, 4 + xp);
            rv_u32 mprv = rv_b(cpu->csrs.mstatus, 17);
            if (rv_b(cpu->csrs.mstatus, 22) && xp == RV_PSUPER)
              return rv_except(cpu, RV_EILL, cpu->pc); /* exception if tsr=1 */
            mprv *= yp == RV_PMACH; /* if y != m, mprv' = 0 */
            cpu->csrs.mstatus &=
                xp == RV_PMACH ? 0xFFFDE777
                               : 0x807DFEC9; /* {xpp, xie, xpie, mprv} <- 0 */
            cpu->csrs.mstatus |= pie << xp   /* xie <- xpie */
                                 | 1 << (4 + xp) /* xpie <- 1 */
                                 | mprv << 17;   /* mprv <- mprv' */
            cpu->priv = yp;                      /* priv <- y */
            cpu->next_pc = xp == RV_PMACH ? cpu->csrs.mepc : cpu->csrs.sepc;
          } else if (rv_irs2(i) == 5 && rv_if7(i) == 8) { /*I wfi */
            rv_dbg("(WFI)\n");
          } else if (rv_if7(i) == 9) { /*I sfence.vma */
            cpu->tlb_valid = 0;
          } else if (!rv_irs1(i) && !rv_irs2(i) && !rv_if7(i)) { /*I ecall */
            return rv_except(cpu, RV_EUECALL + cpu->priv, cpu->pc);
          } else if (!rv_irs1(i) && rv_irs2(i) == 1 && !rv_if7(i)) {
            return rv_except(cpu, RV_EBP, cpu->pc); /*I ebreak */
          } else
            return rv_except(cpu, RV_EILL, cpu->pc);
        } else
          return rv_except(cpu, RV_EILL, cpu->pc);
      } else
        return rv_except(cpu, RV_EILL, cpu->pc);
    } else
      return rv_except(cpu, RV_EILL, cpu->pc);
  } else if (rv_iopl(i) == 5) {
    if (rv_ioph(i) == 0) {                           /*Q 00/101: AUIPC */
      rv_sr(cpu, rv_ird(i), rv_iimm_u(i) + cpu->pc); /*I auipc */
    } else if (rv_ioph(i) == 1) {                    /*Q 01/101: LUI */
      rv_sr(cpu, rv_ird(i), rv_iimm_u(i));           /*I lui */
    } else
      return rv_except(cpu, RV_EILL, cpu->pc);
  } else
    return rv_except(cpu, RV_EILL, cpu->pc);
  cpu->pc = cpu->next_pc;
  if (cpu->csrs.mip) {
    rv_u32 inter;
    for (inter = 12; inter > 0; inter--) {
      rv_priv deleg_priv = RV_PMACH;
      if (!(cpu->csrs.mip & cpu->csrs.mie & (1 << inter)))
        continue;
      if (cpu->csrs.mideleg & (1 << inter))
        deleg_priv = RV_PSUPER;
      if (((0xF0 >> (4 - cpu->priv)) & (cpu->csrs.mstatus & 0xF)) &
          (0xF >> (3 - deleg_priv)))
        return rv_except(cpu, 0x80000000U + inter, cpu->pc);
    }
  }
  return 0x80000000; /* reserved code -- no exception */
}

rv_u32 rv_irq(rv *cpu, rv_cause cause) {
  cpu->csrs.mip &= ~(1U << 3 | 1 << 7 | 1 << 9);
  if (cause & RV_CSW)
    cpu->csrs.mip |= (1 << 3);
  else if (cause & RV_CTIM)
    cpu->csrs.mip |= (1 << 7);
  else if (cause & RV_CEXT)
    cpu->csrs.mip |= (1 << 9);
  return 0;
}
