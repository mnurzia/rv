/* RV32I[MC] emulator. */
#ifndef MN_RV_H
#define MN_RV_H

/* Set to 1 for extremely fine-grained debug output. */
#define RV_VERBOSE 1

/* Enabled extensions. */
#define RVM 1 /* Multiplication and Division */
#define RVC 1 /* Compressed Instructions */

/* Exception list. */
#define RV_EIALIGN 1 /* Instruction alignment exception. */
#define RV_EIFAULT 2 /* Instruction fault exception. */
#define RV_EILL 3    /* Illegal instruction exception. */
#define RV_EBP 4     /* Breakpoint. */
#define RV_ELALIGN 5 /* Load alignment exception. */
#define RV_ELFAULT 6 /* Load fault. */
#define RV_ESALIGN 7 /* Store alignment exception. */
#define RV_ESFAULT 8 /* Store fault. */
#define RV_EECALL 9  /* Environment call. */

#if __STDC__ && __STDC_VERSION__ >= 199901L /* Attempt to load stdint.h. */
#include <stdint.h>
typedef uint8_t rv_u8;
typedef uint16_t rv_u16;
typedef int32_t rv_s32;
typedef uint32_t rv_u32;
#else
/* Assumption: Two's complement arithmetic is used. */
typedef unsigned char rv_u8;   /* Assumption: CHAR_BIT == 8 */
typedef unsigned short rv_u16; /* Assumption: sizeof(rv_u16) == 2 */
typedef int rv_s32;            /* Assumption: sizeof(rv_s32) == 4 */
typedef unsigned int rv_u32;   /* Assumption: sizeof(rv_u32) == 4 */
#endif /* (slight) deviations from c89. Sorry {TI, Cray, DEC, et. al.} */

/* Result type: one of {RV_OK, RV_BAD} */
typedef rv_u32 rv_res;

#define RV_OK 0
#define RV_BAD 1

typedef struct rv_csrs {
  rv_u32 mhartid, mstatus, mstatush, mscratch, mepc, mcause, mtval, mip, mtinst,
      mtval2, mtvec, mie;
} rv_csrs;

/* Memory access callbacks: data is input/output, return RV_BAD on fault */
typedef rv_res (*rv_store_cb)(void *user, rv_u32 addr, rv_u8 data);
typedef rv_res (*rv_load_cb)(void *user, rv_u32 addr, rv_u8 *data);

typedef struct rv {
  rv_load_cb load_cb;
  rv_store_cb store_cb;
  rv_u32 r[32];
  rv_u32 pc;
  rv_u32 next_pc;
  void *user;
  rv_csrs csrs;
} rv;

/* Initialize CPU. */
void rv_init(rv *cpu, void *user, rv_load_cb load_cb, rv_store_cb store_cb);

/* Single-step CPU. Returns 0 on success, one of RV_E* on exception. */
rv_u32 rv_inst(rv *cpu);

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
