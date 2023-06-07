#ifndef MN_RV_H
#define MN_RV_H

#define RVM 1
#define RVC 1

#define RV_EIALIGN 1 /* 3.1.15 Machine Cause Register (mcause) */
#define RV_EIFAULT 2
#define RV_EILL 3
#define RV_EBP 4
#define RV_ELALIGN 5
#define RV_ELFAULT 6
#define RV_ESALIGN 7
#define RV_ESFAULT 8
#define RV_EECALL 9

#define rv_isbad(x) ((x) >> 32)

#if __STDC__ && __STDC_VERSION__ >= 199901L
#include <stdint.h>
typedef uint8_t rv_u8;
typedef int32_t rv_s32;
typedef uint32_t rv_u32;
typedef uint64_t rv_res;
typedef uint64_t rv_u64;
typedef int64_t rv_s64;
#else
typedef unsigned char rv_u8;
typedef int rv_s32;
typedef unsigned int rv_u32;
typedef unsigned long rv_res;
typedef unsigned long rv_u64; /* TODO: this isn't very portable... */
typedef signed long rv_s64;
#endif

#define RV_BAD ((rv_res)1 << 32) /* represents an exceptional value */

typedef rv_res (*rv_load_cb)(void *user, rv_u32 addr);
typedef rv_res (*rv_store_cb)(void *user, rv_u32 addr, rv_u8 data);

typedef struct rv_csrs {
  rv_u32 mhartid, mstatus, mstatush, mscratch, mepc, mcause, mtval, mip, mtinst,
      mtval2, mtvec, mie;
} rv_csrs;

typedef struct rv {
  rv_load_cb load_cb;
  rv_store_cb store_cb;
  rv_u32 r[32];
  rv_u32 pc;
  rv_u32 next_pc;
  void *user;
  rv_csrs csrs;
} rv;

void rv_init(rv *cpu, void *user, rv_load_cb load_cb, rv_store_cb store_cb);

rv_u32 rv_inst(rv *cpu); /* returns 0 on success, anything else is exception */

#endif
