#include "rv_gdb.h"
#include "ext/tinycthread.h"
#include "rv.h"

#include <assert.h>
#include <stdlib.h>

#define RV_GDB_READ_CHUNK 1024

void rv_gdb_buf_init(rv_gdb_buf *buf, rv_gdb_buf_fill fill) {
  buf->b = NULL;
  buf->alloc = 0;
  buf->sz = 0;
  buf->ptr = 0;
  buf->fill = fill;
}

void rv_gdb_buf_destroy(rv_gdb_buf *buf) {
  if (buf->b)
    free(buf->b);
}

void rv_gdb_buf_clear(rv_gdb_buf *buf) {
  buf->sz = 0;
  buf->ptr = 0;
}

int rv_gdb_buf_grow(rv_gdb_buf *buf, rv_u32 req_sz) {
  if (req_sz > buf->alloc) {
    rv_u8 *new = realloc(buf->b, req_sz + 1);
    if (!new)
      return RV_GDB_ERR_NOMEM;
    buf->alloc = req_sz;
    buf->b = new;
  }
  return 0;
}

int rv_gdb_buf_next(rv_gdb_buf *buf, rv_gdb *gdb, rv_u8 *out) {
  int err = 0;
  if (buf->sz == buf->ptr) { /* need more */
    if ((err = rv_gdb_buf_grow(buf, buf->sz + RV_GDB_READ_CHUNK)))
      return err;
    if ((err = (buf->fill(buf, gdb))))
      return err;
    buf->b[buf->sz] = '\0';
  }
  *out = buf->b[buf->ptr++];
  return err;
}

int rv_gdb_buf_append(rv_gdb_buf *buf, rv_u8 *s, rv_u32 s_sz) {
  int err = 0;
  if ((err = rv_gdb_buf_grow(buf, buf->sz + s_sz)))
    return err;
  memcpy(buf->b + buf->sz, s, s_sz);
  buf->sz += s_sz;
  buf->b[buf->sz] = '\0';
  return err;
}

int rv_gdb_recv(rv_gdb_buf *buf, rv_gdb *gdb) {
  ssize_t rv;
reread:
  rv = read(gdb->conn, buf->b + buf->sz, RV_GDB_READ_CHUNK);
  assert(rv <= RV_GDB_READ_CHUNK);
  if (rv <= 0) {
    if (errno == EINTR)
      goto reread;
    perror("read");
    return RV_GDB_ERR_READ;
  }
  buf->sz += rv;
  return 0;
}

void rv_gdb_init(rv_gdb *gdb) {
  gdb->harts = NULL;
  gdb->harts_sz = 0;
  gdb->hart_idx = 0;
  rv_gdb_buf_init(&gdb->rx_pre_buf, &rv_gdb_recv);
  rv_gdb_buf_init(&gdb->rx_buf, NULL);
  rv_gdb_buf_init(&gdb->tx_buf, NULL);
  gdb->conn = -1;
  gdb->bp = NULL;
  gdb->bp_sz = 0;
  gdb->tx_seq = 1; /* to handle first '+' */
  gdb->tx_ack = 0;
  gdb->rx_seq = 0;
  gdb->rx_ack = 0;
  assert(mtx_init(&gdb->mtx_in, mtx_plain) != thrd_error);
  assert(mtx_init(&gdb->mtx_rx, mtx_plain) != thrd_error);
  assert(cnd_init(&gdb->cnd_evt) != thrd_error);
}

int rv_gdb_addhart(rv_gdb *gdb, rv *cpu, const char *tid) {
  rv_gdb_hart *harts =
      realloc(gdb->harts, (gdb->harts_sz + 1) * sizeof(rv_gdb_hart));
  if (!harts)
    return RV_GDB_ERR_NOMEM;
  gdb->harts = harts;
  gdb->harts[gdb->harts_sz].cpu = cpu;
  gdb->harts[gdb->harts_sz].tid = tid;
  gdb->harts[gdb->harts_sz].except = 0;
  assert(mtx_init(&gdb->harts[gdb->harts_sz].mtx, mtx_plain) != thrd_error);
  gdb->harts[gdb->harts_sz++].state = RV_GDB_CONTINUE;
  return 0;
}

void rv_gdb_destroy(rv_gdb *gdb) {
  rv_gdb_buf_destroy(&gdb->rx_pre_buf);
  rv_gdb_buf_destroy(&gdb->rx_buf);
  rv_gdb_buf_destroy(&gdb->tx_buf);
  mtx_destroy(&gdb->mtx_in);
  mtx_destroy(&gdb->mtx_rx);
  cnd_destroy(&gdb->cnd_evt);
  {
    rv_u32 i;
    for (i = 0; i < gdb->harts_sz; i++) {
      mtx_destroy(&gdb->harts[i].mtx);
    }
  }
}

int rv_gdb_in(rv_gdb *gdb, rv_u8 *out) {
  return rv_gdb_buf_next(&gdb->rx_pre_buf, gdb, out);
}

int rv_gdb_rx(rv_gdb *gdb, rv_u8 *out) {
  return rv_gdb_buf_next(&gdb->rx_buf, gdb, out);
}

int rv_gdb_hex2num(rv_u8 c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  else if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  else if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  else
    return RV_GDB_ERR_FMT;
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

int rv_gdb_tx(rv_gdb *gdb, rv_u8 c) {
  return rv_gdb_buf_append(&gdb->tx_buf, &c, 1);
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
  gdb->tx_buf.sz = 0;
  if (gdb->rx_ack < gdb->rx_seq) {
    if ((err = rv_gdb_tx(gdb, '+')))
      return err;
    gdb->rx_ack++;
  }
  if ((err = rv_gdb_tx(gdb, '$')))
    return err;
  return 0;
}

int rv_gdb_tx_end(rv_gdb *gdb) {
  rv_u8 cksum = 0;
  rv_u32 i;
  int err = 0;
  for (i = 2; i < gdb->tx_buf.sz; i++) {
    cksum += gdb->tx_buf.b[i];
  }
  if ((err = rv_gdb_tx(gdb, '#')))
    return err;
  if ((err = rv_gdb_tx(gdb, rv_gdb_num2hex(cksum >> 4))))
    return err;
  if ((err = rv_gdb_tx(gdb, rv_gdb_num2hex(cksum & 0xF))))
    return err;
  if (write(gdb->conn, gdb->tx_buf.b, gdb->tx_buf.sz) != gdb->tx_buf.sz)
    return RV_GDB_ERR_WRITE;
  printf("[Tx] %s %u\n", gdb->tx_buf.b, ++gdb->tx_seq);
  return 0;
}

int rv_gdb_rx_pre(rv_gdb *gdb, const char *s) {
  rv_u32 i = gdb->rx_buf.ptr;
  while (1) {
    if (*s == '\0') {
      gdb->rx_buf.ptr = i;
      return 1;
    }
    if (i == gdb->rx_buf.sz)
      return 0;
    if (gdb->rx_buf.b[i] != *s)
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
  rv_u32 saved = gdb->rx_buf.ptr;
  assert(ndig <= 8);
  for (i = 0; i < ndig / 2; i++) {
    for (j = 0; j < 2; j++) {
      unsigned int dig_idx = (le ? i * 2 : (ndig - (i * 2) - 2)) + 1 - j;
      if ((err = rv_gdb_rx(gdb, &c)))
        goto error;
      if ((err = rv_gdb_hex2num(c)) < 0)
        goto error;
      out |= (rv_u32)err << (dig_idx * 4);
    }
  }
  err = 0;
  *v = out;
  return err;
error:
  gdb->rx_buf.ptr = saved;
  return err;
}

int rv_gdb_rx_svwh(rv_gdb *gdb, rv_u32 *v) {
  int err = 0;
  rv_u32 out = 0;
  rv_u32 saved = gdb->rx_buf.ptr;
  int i = 0;
  while (1) {
    rv_u8 c;
    if ((err = rv_gdb_rx(gdb, &c))) {
      goto done;
    }
    if ((err = rv_gdb_hex2num(c)) < 0) {
      gdb->rx_buf.ptr--;
      goto done;
    }
    out <<= 4;
    out |= (rv_u32)err;
    i++;
  }
done:
  if (!i) {
    gdb->rx_buf.ptr = saved;
    return RV_GDB_ERR_EOF;
  }
  *v = out;
  return 0;
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

rv_u32 rv_gdb_cvt_sig(rv_u32 except) {
  if (!except)
    return 0; /* none */
  except -= 1;
  if (except == RV_EIFAULT || except == RV_ELFAULT || except == RV_ESFAULT)
    return 10; /* SIGBUS */
  else if (except == RV_EIALIGN || except == RV_ELALIGN || except == RV_ESALIGN)
    return 10; /* SIGBUS */
  else if (except == RV_EECALL)
    return 5; /* SIGTRAP */
  else if (except == RV_EILL)
    return 4; /* SIGILL */
  else
    return 6; /* SIGABRT */
}

int rv_gdb_stop(rv_gdb *gdb) {
  int err = 0;
  rv_u32 i;
  for (i = 0; i < gdb->harts_sz; i++) {
    rv_gdb_hart *h = gdb->harts + i;
    if ((err = rv_gdb_tx_begin(gdb)))
      return err;
    if ((err = rv_gdb_tx(gdb, 'T')))
      return err;
    if ((err = rv_gdb_tx_h(gdb, rv_gdb_cvt_sig(h->except), 2, 0)))
      return err;
    if ((err = rv_gdb_tx_s(gdb, "thread:")))
      return err;
    if ((err = rv_gdb_tx(gdb, rv_gdb_num2hex((rv_u8)i + 1))))
      return err;
    if ((err = rv_gdb_tx(gdb, ';')))
      return err;
    if ((err = rv_gdb_tx_end(gdb)))
      return err;
    break;
  }
  return err;
}

rv *rv_gdb_cpu(rv_gdb *gdb) { return gdb->harts[gdb->hart_idx].cpu; }

int rv_gdb_packet(rv_gdb *gdb) {
  rv_u8 c;
  int err = 0;
  printf("[Rx] %s\n", gdb->rx_buf.b);
  if ((err = rv_gdb_rx(gdb, &c)))
    return err;
  if (c == '?') { /* stop reason -> initial stop */
    return rv_gdb_stop(gdb);
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
        return RV_GDB_ERR_FMT;
      gdb->hart_idx = tid - 1;
    }
    if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_s(gdb, "OK")) ||
        (err = rv_gdb_tx_end(gdb)))
      return err;
  } else if (c == 'T') { /* is thread alive */
    if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_s(gdb, "OK")) ||
        (err = rv_gdb_tx_end(gdb)))
      return err;
  } else if (c == 'Z' || c == 'z') { /* breakpoint */
    if ((err = rv_gdb_rx(gdb, &c)))  /* kind */
      return err;
    if (c != '0')
      return RV_GDB_ERR_FMT;

  } else if (c == 'c' || c == 's') { /* continue/step */
    rv_u32 addr = 0;
    if ((err = rv_gdb_rx_svwh(gdb, &addr) >= 0)) {
      rv_gdb_cpu(gdb)->pc = addr;
    }
    gdb->harts[gdb->hart_idx].state = c == 'c' ? RV_GDB_CONTINUE : RV_GDB_STEP;
    return 1;
  } else if (c == 'g') { /* general registers */
    rv_u32 i;
    if ((err = rv_gdb_tx_begin(gdb)))
      return err;
    for (i = 0; i < 32; i++)
      if ((err = rv_gdb_tx_h(gdb, rv_gdb_cpu(gdb)->r[i], 8, 1)))
        return err;
    if ((err = rv_gdb_tx_h(gdb, rv_gdb_cpu(gdb)->pc, 8, 1)))
      return err;
    if ((err = rv_gdb_tx_end(gdb)))
      return err;
  } else if (c == 'm') { /* read memory */
    rv_u32 addr = 0, size = 0, i;
    rv_res res;
    if ((err = rv_gdb_rx_svwh(gdb, &addr)))
      return err;
    if ((err = rv_gdb_rx(gdb, &c)))
      return err;
    if (c != ',')
      return RV_GDB_ERR_FMT;
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
        return RV_GDB_ERR_FMT;
      if ((err = rv_gdb_tx_begin(gdb)) ||
          (err = rv_gdb_tx_sh(gdb, (rv_u8 *)(gdb->harts[tid - 1].tid))) ||
          (err = rv_gdb_tx_end(gdb)))
        return err;
      return 0;
    } else {
      assert(0);
    }
  } else if (c == 'v') { /* variable...? Idk what the v stands for. */
    if (rv_gdb_rx_pre(gdb, "MustReplyEmpty")) { /* unknown packet test */
      if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_end(gdb)))
        return err;
    } else if (rv_gdb_rx_pre(gdb, "Cont?")) { /* vCont support query */
      if ((err = rv_gdb_tx_begin(gdb)) ||
          (err = rv_gdb_tx_s(gdb, "vCont;c;s;C;S;t")) ||
          (err = rv_gdb_tx_end(gdb)))
        return err;
    } else if (rv_gdb_rx_pre(gdb, "Cont")) { /* vCont */
      rv_u32 i;
      while (1) {
        rv_u32 tid = 0; /* all thrds */
        rv_u8 act;
        rv_u32 sig = 0;
        if ((err = rv_gdb_rx(gdb, &c)) && err != RV_GDB_ERR_EOF)
          return err;
        if (err) /* eof */
          break;
        if (c != ';')
          return RV_GDB_ERR_FMT;
        if ((err = rv_gdb_rx(gdb, &act))) /* parse action */
          return err;
        if ((act == 'C' || act == 'S') && (err = rv_gdb_rx_h(gdb, &sig, 2, 0)))
          return err; /* parse signal */
        if ((err = rv_gdb_rx(gdb, &c)) &&
            err != RV_GDB_ERR_EOF) /* parse colon (maybe) */
          return err;
        if (!err && c != ':')
          gdb->rx_buf.ptr--;
        else if (!err && (err = rv_gdb_rx_svwh(
                              gdb, &tid))) { /* couldn't parse hex tid */
          rv_u8 a, b;
          if ((err = rv_gdb_rx(gdb, &a)) ||
              (err = rv_gdb_rx(gdb, &b))) /* parse -1 */
            return err;
          if (a != '-' || b != '1')
            return RV_GDB_ERR_FMT;
        }
        for (i = 0; i < gdb->harts_sz; i++) {
          rv_u32 state;
          if (act == 'c' || act == 'C') /* continue */
            state = RV_GDB_CONTINUE;
          else if (act == 's' || act == 'S') /* step */
            state = RV_GDB_STEP;
          else
            return RV_GDB_ERR_FMT;
          if ((!tid || (i == tid - 1)) &&
              !gdb->harts[i].state) { /* write state to hart */
            printf("[vCont] setting thread %u state to %u\n", i, state);
            gdb->harts[i].state = state;
          }
        }
      }
      return 1;
    } else if (rv_gdb_rx_pre(gdb, "Kill")) { /* kill, ignore pid for now */
      if ((err = rv_gdb_tx_begin(gdb)) || (err = rv_gdb_tx_end(gdb))) /* ack */
        return err;
      return 2;
    } else {
      assert(0);
    }
  } else {
    assert(0);
  }
  return err;
}

int rv_gdb_next_cmd(rv_gdb *gdb) {
  int err = 0;
  rv_u8 c;
  rv_u8 cksum = 0;
  rv_u32 start_ptr = 0;
ack:
  if ((err = rv_gdb_in(gdb, &c)))
    return err;
  if (c == '+') {
    assert(gdb->tx_ack != gdb->tx_seq);
    gdb->tx_ack++;
    goto ack;
  }
  if (c != '$')
    return RV_GDB_ERR_FMT;
  start_ptr = gdb->rx_pre_buf.ptr;
  while (1) {
    if ((err = rv_gdb_in(gdb, &c)))
      return err;
    if (c == '#') { /* packet end */
      gdb->rx_seq++;
      if ((err = rv_gdb_next_hex(gdb, &c)))
        return err;
      if (c != cksum)
        return RV_GDB_ERR_CORRUPT;
      /* copy packet to rx buf */
      mtx_lock(&gdb->mtx_rx);
      rv_gdb_buf_clear(&gdb->rx_buf);
      err = rv_gdb_buf_append(&gdb->rx_buf, gdb->rx_pre_buf.b + start_ptr,
                              gdb->rx_pre_buf.sz - 3);
      mtx_unlock(&gdb->mtx_rx);
      break;
    } else {
      cksum += c;
    }
  }
  return 0;
}

#define RV_GDB_STATE_THRD_RUN 0
#define RV_GDB_STATE_PRE_STOP 1
#define RV_GDB_STATE_STOP 2

int rv_gdb_recv_thrd(void *arg) {
  rv_gdb *gdb = arg;
  int err = 0, sock;
  rv_u32 i;
  struct sockaddr_in addr;
  int addrlen = sizeof(addr);
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    return RV_GDB_ERR_SOCKET;
  }
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(61444);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    err = RV_GDB_ERR_BIND;
    goto cleanup;
  }
  if (listen(sock, 3) < 0) {
    perror("listen");
    err = RV_GDB_ERR_LISTEN;
    goto cleanup;
  }
  /* wait for a connection */
  if ((gdb->conn =
           accept(sock, (struct sockaddr *)&addr, (socklen_t *)&addrlen)) < 0) {
    perror("accept");
    err = RV_GDB_ERR_ACCEPT;
    goto cleanup;
  }
  /* on receive, stop all thrds, look for commands */
  for (i = 0; i < gdb->harts_sz; i++) {
    mtx_lock(&gdb->harts[i].mtx);
    if (gdb->harts[i].state == RV_GDB_STATE_THRD_RUN)
      gdb->harts[i].state = RV_GDB_STATE_PRE_STOP;
    mtx_unlock(&gdb->harts[i].mtx);
  }
  /* spin until threads all stopped (shouldn't take long) */
cont:
  for (i = 0; i < 4; i++) {
    rv_u32 state;
    mtx_lock(&gdb->harts[i].mtx);
    state = gdb->harts[i].state;
    mtx_unlock(&gdb->harts[i].mtx);
    if (state != RV_GDB_STATE_STOP)
      goto cont;
  }
  /* all threads are now stopped */
  while (1) {
    err = rv_gdb_next_cmd(gdb);
    assert(!err);
    if (err) {
      goto cleanup;
    } else {
      cnd_signal(&gdb->cnd_evt);
    }
  }
cleanup:
  shutdown(sock, SHUT_RDWR);
  return err;
}

int rv_gdb_cmd_thrd(void *arg) {
  rv_gdb *gdb = arg;
  int err = 0;
  while (1) {
    mtx_lock(&gdb->mtx_in);
    cnd_wait(&gdb->cnd_evt, &gdb->mtx_in);
    mtx_lock(&gdb->mtx_rx); /* check if signal on input */
    if (gdb->rx_buf.sz)
      err = rv_gdb_packet(gdb);
    mtx_unlock(&gdb->mtx_rx);
    mtx_unlock(&gdb->mtx_in);
    assert(err >= 0);
  }
  return 0;
}

typedef struct rv_gdb_hart_thrd_arg {
  rv_gdb *gdb;
  rv_u32 idx;
} rv_gdb_hart_thrd_arg;

int rv_gdb_hart_thrd(void *arg) {
  rv_gdb_hart_thrd_arg *targ = arg;
  rv_gdb_hart *hart = targ->gdb->harts + targ->idx;
  rv_gdb *gdb = targ->gdb;
  int i;
  rv_u32 rv;
  while (1) {
    for (i = 0; i < 512; i++) { /* free-run and attempt stop every 512 cyc */
      if ((rv = rv_inst(hart->cpu))) {
        mtx_lock(&hart->mtx);
        hart->state = RV_GDB_STATE_STOP;
        printf("[%i] setting thrd state to STOP...\n", targ->idx);
        hart->except = rv;
        goto ret_unlock;
      }
    }
    usleep(1000 * 500);
    mtx_lock(&hart->mtx);
    if (hart->state == RV_GDB_STATE_PRE_STOP) {
      hart->state = RV_GDB_STATE_STOP;
      printf("[%i] setting thrd state to STOP....\n", targ->idx);
      hart->except = 0;
      goto ret_unlock;
    }
    mtx_unlock(&hart->mtx);
  }
ret_unlock:
  mtx_unlock(&hart->mtx);
  cnd_signal(&gdb->cnd_evt);
  return 0;
}

int open_sock(void) {
  int sock, new;
  struct sockaddr_in addr;
  int addrlen = sizeof(addr);
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("\n Socket creation error \n");
    return 1;
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
  return new;
}

#include "machine.h"

int main(int argc, const char **argv) {
  thrd_t recv_thrd, hart_thrds[4];
  rv_gdb_hart_thrd_arg args[4];
  machine m;
  rv_u32 i;
  (void)argc;
  machine_init(&m, argv[1]);
  for (i = 0; i < 4; i++) {
    const char *names[] = {"cpu", "cpu1", "cpu2", "cpu3"};
    rv_gdb_addhart(&m.gdb, m.cpus + i, names[i]);
    args[i].gdb = &m.gdb;
    args[i].idx = i;
    assert(thrd_create(&hart_thrds[i], rv_gdb_hart_thrd, args + i) !=
           thrd_error);
  }
  assert(thrd_create(&recv_thrd, rv_gdb_recv_thrd, &m.gdb) != thrd_error);
  rv_gdb_cmd_thrd(&m.gdb);
  return 0;
}
