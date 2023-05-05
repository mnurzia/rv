#ifndef MN_RV_GDB_H
#define MN_RV_GDB_H

#include "ext/tinycthread.h"
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
#define RV_GDB_ERR_SOCKET -8
#define RV_GDB_ERR_BIND -9
#define RV_GDB_ERR_LISTEN -10
#define RV_GDB_ERR_ACCEPT -11

#define RV_GDB_STOP 0
#define RV_GDB_STEP 1
#define RV_GDB_CONTINUE 2

typedef struct rv_gdb_hart rv_gdb_hart;

struct rv_gdb_hart {
  rv *cpu;
  rv_u32 state;
  rv_u32 except;
  const char *tid;
  mtx_t mtx;
};

typedef struct rv_gdb rv_gdb;
typedef struct rv_gdb_buf rv_gdb_buf;

typedef int (*rv_gdb_buf_fill)(rv_gdb_buf *, rv_gdb *);

struct rv_gdb_buf {
  rv_u8 *b;
  rv_u32 ptr;
  rv_u32 sz;
  rv_u32 alloc;
  rv_gdb_buf_fill fill;
};

struct rv_gdb {
  rv_gdb_hart *harts;
  rv_u32 harts_sz;
  rv_u32 hart_idx;
  rv_gdb_buf rx_pre_buf;
  rv_gdb_buf rx_buf;
  rv_gdb_buf tx_buf;
  rv_u32 *bp;
  rv_u32 bp_sz;
  rv_u32 tx_seq;
  rv_u32 tx_ack;
  rv_u32 rx_seq;
  rv_u32 rx_ack;
  int conn;
  cnd_t cnd_evt;
  mtx_t mtx_in;
  mtx_t mtx_rx;
};

void rv_gdb_init(rv_gdb *gdb);

void rv_gdb_destroy(rv_gdb *gdb);

#endif /* MN_RV_GDB_H */
