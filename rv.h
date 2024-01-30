/* RV32I[MAC] emulator.
 * see: https://github.com/riscv/riscv-isa-manual */
#ifndef MN_RV_H
#define MN_RV_H

#define RV_VERSION "1.0.0"

/* Exception list. */
#define RV_EIALIGN 0  /* Instruction alignment exception. */
#define RV_EIFAULT 1  /* Instruction fault exception. */
#define RV_EILL 2     /* Illegal instruction exception. */
#define RV_EBP 3      /* Breakpoint. */
#define RV_ELALIGN 4  /* Load alignment exception. */
#define RV_ELFAULT 5  /* Load fault. */
#define RV_ESALIGN 6  /* Store alignment exception. */
#define RV_ESFAULT 7  /* Store fault. */
#define RV_EUECALL 8  /* Environment call from U-mode. */
#define RV_ESECALL 9  /* Environment call from S-mode. */
#define RV_EMECALL 11 /* Environment call from M-mode. */
#define RV_EIPAGE 12  /* Instruction page fault. */
#define RV_ELPAGE 13  /* Load page fault. */
#define RV_ESPAGE 15  /* Store page fault. */

#if __STDC__ && __STDC_VERSION__ >= 199901L /* Attempt to load stdint.h. */
#include <stdint.h>
#define RV_U8_TYPE uint8_t   /* Yes, I know that these types are optional. */
#define RV_U16_TYPE uint16_t /* They *usually* exist. Regardless, rv isn't */
#define RV_S32_TYPE int32_t  /* meant to be run on systems with */
#define RV_U32_TYPE uint32_t /* CHAR_BIT != 8 or other weird integer specs. */
#else
#ifdef __UINT8_TYPE__ /* If these are here, we might as well use them. */
#define RV_U8_TYPE __UINT8_TYPE__
#define RV_U16_TYPE __UINT16_TYPE__
#define RV_S32_TYPE __INT32_TYPE__
#define RV_U32_TYPE __UINT32_TYPE__
#else
#define RV_U8_TYPE unsigned char   /* Assumption: CHAR_BIT == 8 */
#define RV_U16_TYPE unsigned short /* Assumption: sizeof(ushort) == 2 */
#define RV_S32_TYPE signed int     /* Assumption: sizeof(sint) == 4 */
#define RV_U32_TYPE unsigned int   /* Assumption: sizeof(uint) == 4 */
#endif /* (slight) deviations from c89. Sorry {TI, Cray, DEC, et. al.} */
#endif /* All I want for Christmas is C89 with stdint.h */

typedef RV_U8_TYPE rv_u8;
typedef RV_U16_TYPE rv_u16;
typedef RV_S32_TYPE rv_s32;
typedef RV_U32_TYPE rv_u32;

/* Result type: one of {RV_OK, RV_BAD, RV_PAGEFAULT, RV_BAD_ALIGN} */
typedef rv_u32 rv_res;

#define RV_OK 0
#define RV_BAD 1
#define RV_BAD_ALIGN 2
#define RV_PAGEFAULT 3
#define RV_TRAP_NONE 0x80000010
#define RV_TRAP_WFI 0x80000011

typedef struct rv_csr {
  rv_u32 /* sstatus, */ sie, stvec, scounteren, sscratch, sepc, scause, stval,
      sip, satp;
  rv_u32 mstatus, misa, medeleg, mideleg, mie, mtvec, mcounteren, mstatush,
      mscratch, mepc, mcause, mtval, mip, mtime, mtimeh, mvendorid, marchid,
      mimpid, mhartid;
  rv_u32 cycle, cycleh;
} rv_csr;

typedef enum rv_priv { RV_PUSER = 0, RV_PSUPER = 1, RV_PMACH = 3 } rv_priv;
typedef enum rv_access { RV_AR = 1, RV_AW = 2, RV_AX = 4 } rv_access;
typedef enum rv_cause { RV_CSI = 8, RV_CTI = 128, RV_CEI = 512 } rv_cause;

/* Memory access callback: data is input/output, return RV_BAD on fault.
 * Accesses are always aligned to `width`. */
typedef rv_res (*rv_bus_cb)(void *user, rv_u32 addr, rv_u8 *data,
                            rv_u32 is_store, rv_u32 width);

typedef struct rv {
  rv_bus_cb bus_cb;
  void *user;
  rv_u32 r[32];          /* registers */
  rv_u32 pc;             /* program counter */
  rv_u32 next_pc;        /* program counter for next cycle */
  rv_csr csr;            /* csr state */
  rv_u32 priv;           /* current privilege level*/
  rv_u32 res, res_valid; /* lr/sc reservation set */
  rv_u32 tlb_va, tlb_pte, tlb_valid, tlb_i;
} rv;

/* Initialize CPU. You can call this again on `cpu` to reset it. */
void rv_init(rv *cpu, void *user, rv_bus_cb bus_cb);

/* Single-step CPU. Returns trap cause if trap occurred, else `RV_TRAP_NONE` */
rv_u32 rv_step(rv *cpu);

/* Trigger interrupt(s). */
void rv_irq(rv *cpu, rv_cause cause);

/* Utility function to convert between host<->LE. */
void rv_endcpy(rv_u8 *in, rv_u8 *out, rv_u32 width, rv_u32 is_store);

#endif

/* Copyright (c) 2023 Max Nurzia

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the “Software”), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */
