#include "rv_gdb.h"

#include <assert.h>
#include <stdlib.h>

#define RV_GDB_BUF_ALLOC 1024

int rv_gdb_init(rv_gdb *gdb, int sock) {
  assert(sock);
  gdb->harts = NULL;
  gdb->harts_sz = 0;
  gdb->hart_idx = 0;
  gdb->rx_pre = NULL;
  gdb->rx_pre_ptr = 0;
  gdb->rx_pre_sz = 0;
  gdb->rx = NULL;
  gdb->rx_ptr = 0;
  gdb->rx_sz = 0;
  gdb->rx_alloc = 0;
  gdb->tx = NULL;
  gdb->tx_sz = 0;
  gdb->tx_alloc = 0;
  gdb->sock = sock;
  gdb->bp = NULL;
  gdb->bp_sz = 0;
  gdb->tx_seq = 1; /* to handle first '+' */
  gdb->tx_ack = 0;
  gdb->rx_seq = 0;
  gdb->rx_ack = 0;
  if (!(gdb->rx_pre = malloc(RV_GDB_BUF_ALLOC)))
    return RV_GDB_ERR_NOMEM;
  memset(gdb->rx_pre, 0, RV_GDB_BUF_ALLOC);
  return 0;
}

int rv_gdb_addhart(rv_gdb *gdb, rv *cpu, const char *tid) {
  rv_gdb_hart *harts =
      realloc(gdb->harts, (gdb->harts_sz + 1) * sizeof(rv_gdb_hart));
  if (!harts)
    return RV_GDB_ERR_NOMEM;
  gdb->harts = harts;
  gdb->harts[gdb->harts_sz].cpu = cpu;
  gdb->harts[gdb->harts_sz].tid = tid;
  gdb->harts[gdb->harts_sz].next_state = 0;
  gdb->harts[gdb->harts_sz].except = 0;
  gdb->harts[gdb->harts_sz].need_send = 0;
  gdb->harts[gdb->harts_sz++].state = RV_GDB_CONTINUE;
  return 0;
}

void rv_gdb_destroy(rv_gdb *gdb) {
  if (gdb->rx_pre)
    free(gdb->rx_pre);
  if (gdb->rx)
    free(gdb->rx);
  if (gdb->tx)
    free(gdb->tx);
  if (gdb->harts)
    free(gdb->harts);
  if (gdb->bp)
    free(gdb->bp);
}

int rv_gdb_recv(rv_gdb *gdb) {
  ssize_t rv;
reread:
  rv = read(gdb->sock, gdb->rx_pre, RV_GDB_BUF_ALLOC);
  assert(rv <= RV_GDB_BUF_ALLOC);
  if (rv <= 0) {
    if (errno == EINTR)
      goto reread;
    return RV_GDB_ERR_READ;
  }
  gdb->rx_pre_sz = (rv_u32)rv;
  gdb->rx_pre_ptr = 0;
  return 0;
}

int rv_gdb_in(rv_gdb *gdb, rv_u8 *out) {
  int err = 0;
  if (gdb->rx_pre_ptr == gdb->rx_pre_sz && (err = rv_gdb_recv(gdb)))
    return err;
  *out = gdb->rx_pre[gdb->rx_pre_ptr++];
  return err;
}

int rv_gdb_rx(rv_gdb *gdb, rv_u8 *out) {
  if (gdb->rx_ptr == gdb->rx_sz)
    return RV_GDB_ERR_EOF;
  *out = gdb->rx[gdb->rx_ptr++];
  return 0;
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
  if (gdb->tx_sz + 1 >= gdb->tx_alloc) {
    rv_u32 new_alloc = gdb->tx_alloc ? gdb->tx_alloc << 1 : 16;
    rv_u8 *tmp = realloc(gdb->tx, new_alloc);
    if (!tmp)
      return RV_GDB_ERR_NOMEM;
    gdb->tx = tmp;
    gdb->tx_alloc = new_alloc;
  }
  gdb->tx[gdb->tx_sz++] = c;
  gdb->tx[gdb->tx_sz] = '\0';
  return 0;
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
  gdb->tx_sz = 0;
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
  for (i = 2; i < gdb->tx_sz; i++) {
    cksum += gdb->tx[i];
  }
  if ((err = rv_gdb_tx(gdb, '#')))
    return err;
  if ((err = rv_gdb_tx(gdb, rv_gdb_num2hex(cksum >> 4))))
    return err;
  if ((err = rv_gdb_tx(gdb, rv_gdb_num2hex(cksum & 0xF))))
    return err;
  if (write(gdb->sock, gdb->tx, gdb->tx_sz) != gdb->tx_sz)
    return RV_GDB_ERR_WRITE;
  printf("[Tx] %s %u\n", gdb->tx, ++gdb->tx_seq);
  return 0;
}

int rv_gdb_rx_pre(rv_gdb *gdb, const char *s) {
  rv_u32 i = gdb->rx_ptr;
  while (1) {
    if (*s == '\0') {
      gdb->rx_ptr = i;
      return 1;
    }
    if (i == gdb->rx_sz)
      return 0;
    if (gdb->rx[i] != *s)
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
  rv_u32 saved = gdb->rx_ptr;
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
  gdb->rx_ptr = saved;
  return err;
}

int rv_gdb_rx_svwh(rv_gdb *gdb, rv_u32 *v) {
  int err = 0;
  rv_u32 out = 0;
  rv_u32 saved = gdb->rx_ptr;
  int i = 0;
  while (1) {
    rv_u8 c;
    if ((err = rv_gdb_rx(gdb, &c))) {
      goto done;
    }
    if ((err = rv_gdb_hex2num(c)) < 0) {
      gdb->rx_ptr--;
      goto done;
    }
    out <<= 4;
    out |= (rv_u32)err;
    i++;
  }
done:
  if (!i) {
    gdb->rx_ptr = saved;
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
  else if (except == RV_EBREAK)
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
    if (!h->need_send) /* sent stop status */
      continue;
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
    h->need_send = 0;
    break;
  }
  for (i = 0; i < gdb->harts_sz; i++) {
    if (gdb->harts[i].need_send)
      return 1;
  }
  return err;
}

rv *rv_gdb_cpu(rv_gdb *gdb) { return gdb->harts[gdb->hart_idx].cpu; }

int rv_gdb_packet(rv_gdb *gdb) {
  rv_u8 c;
  int err = 0;
  gdb->rx_ptr = 0;
  if ((err = rv_gdb_rx(gdb, &c)))
    return err;
  if (c == '?') { /* stop reason -> run until stop */
    return 1;
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
      rv_gdb_cpu(gdb)->ip = addr;
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
    if ((err = rv_gdb_tx_h(gdb, rv_gdb_cpu(gdb)->ip, 8, 1)))
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
      for (i = 0; i < gdb->harts_sz; i++) {
        gdb->harts[i].next_state = 0;
      }
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
          gdb->rx_ptr--;
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
              !gdb->harts[i].next_state) { /* write state to hart */
            printf("[vCont] setting thread %u state to %u\n", i, state);
            gdb->harts[i].state = state;
            gdb->harts[i].next_state = 1;
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

int rv_gdb_proc(rv_gdb *gdb) {
  int err = 0;
  rv_u8 c;
  rv_u8 cksum = 0;
  gdb->rx_sz = 0;
  gdb->rx_ptr = 0;
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
  while (1) {
    if ((err = rv_gdb_in(gdb, &c)))
      return err;
    if (c == '#') { /* packet end */
      gdb->rx_seq++;
      if ((err = rv_gdb_next_hex(gdb, &c)))
        return err;
      if (c != cksum)
        return RV_GDB_ERR_CORRUPT;
      /* process packet */
      printf("[Rx] %s\n", gdb->rx);
      if (gdb->rx_sz && (err = rv_gdb_packet(gdb)))
        return err;
      break;
    } else {
      if (gdb->rx_sz + 1 >= gdb->rx_alloc) {
        rv_u32 new_alloc = gdb->rx_alloc ? gdb->rx_alloc << 1 : 16;
        rv_u8 *tmp = realloc(gdb->rx, new_alloc);
        if (!tmp)
          return RV_GDB_ERR_NOMEM;
        gdb->rx = tmp;
        gdb->rx_alloc = new_alloc;
      }
      cksum += c;
      gdb->rx[gdb->rx_sz++] = c;
      gdb->rx[gdb->rx_sz] = '\0';
    }
  }
  return 0;
}
