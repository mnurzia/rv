#ifndef MN_RV_GDB_H
#define MN_RV_GDB_H

#include "rv.h"

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

#endif /* MN_RV_GDB_H */
