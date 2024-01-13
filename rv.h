/* RV32I[MAC] emulator. */
#ifndef MN_RV_H
#define MN_RV_H

/* Set to 1 for extremely fine-grained debug output. */
/*#define RV_VERBOSE 0*/

/* Enabled extensions. */
#define RVA 1 /* Atomics */
#define RVC 1 /* Compressed Instructions */
#define RVM 1 /* Multiplication and Division */
#define RVS 1 /* Supervisor */

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

/* Result type: one of {RV_OK, RV_BAD} */
typedef rv_u32 rv_res;

#define RV_OK 0
#define RV_BAD 1
#define RV_PAGEFAULT 2

typedef struct rv_csrs {
  rv_u32 mhartid, mstatus, mstatush, mscratch, mepc, mcause, mtval, mip, mtvec,
      mie, misa, mvendorid, marchid, mimpid, medeleg, mideleg, mcounteren,
      mtime, mtimeh;
  rv_u32 /* sstatus, */ sie, stvec, scounteren, senvcfg, sscratch, sepc, scause,
      stval, sip, satp, scontext;
} rv_csrs;

/* Memory access callback: data is input/output, return RV_BAD on fault */
typedef rv_res (*rv_bus_cb)(void *user, rv_u32 addr, rv_u32 *data, rv_u32 str);

typedef enum rv_priv { RV_PUSER = 0, RV_PSUPER = 1, RV_PMACH = 3 } rv_priv;
typedef enum rv_access { RV_AR = 1, RV_AW = 2, RV_AX = 4 } rv_access;
typedef enum rv_cause { RV_CSW = 1, RV_CTIM = 2, RV_CEXT = 4 } rv_cause;

typedef struct rv {
  rv_bus_cb bus_cb;
  rv_u32 r[32];
  rv_u32 pc;
  rv_u32 next_pc;
  void *user;
  rv_csrs csrs;
  rv_u32 priv;
#if RVA
  rv_u32 reserve, reserve_valid;
#endif
} rv;

/* Initialize CPU. You can call this again on `cpu` to reset it. */
void rv_init(rv *cpu, void *user, rv_bus_cb bus_cb);

/* Single-step CPU. Returns 0 on success, one of RV_E* on exception. */
rv_u32 rv_step(rv *cpu);

/* Trigger an interrupt. */
rv_u32 rv_irq(rv *cpu, rv_cause cause);

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
