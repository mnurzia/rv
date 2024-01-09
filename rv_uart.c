#include "rv_uart.h"

#define LSR_DR (1U << 0)  /* receiver data ready */
#define LSR_THE (1U << 5) /* transmitter hold empty */
#define LSR_TSE (1U << 6) /* transmitter shift empty */

#define IER_EDA (1U << 0)
#define IER_TRE (1U << 1)
#define IER_RSI (1U << 2)
#define IER_MSI (1U << 3)

#define IIR_IIP (1U << 0)

void rv_uart_init(rv_uart *uart, void *user, rv_uart_cb cb) {
  memset(uart, 0, sizeof(*uart));
  uart->cb = cb;
  uart->user = user;
}

rv_res rv_uart_bus(rv_uart *uart, rv_u32 addr, rv_u32 *data, rv_u32 str) {
  addr >>= 2;
  if (addr >= 0x100)
    return RV_BAD;
  if (addr == 0) {
    if (!str) { /*R Receiver Buffer Register */
      *data = uart->rbr;
      uart->lsr &= ~LSR_DR;
    } else { /* write */
      uart->thr = *data;
      uart->lsr &= ~(LSR_THE | LSR_TSE);
    }
  } else if (addr == 1) { /*R Interrupt Enable Register */
    if (!str)
      *data = 0;
  } else if (addr == 5) { /*R Line Status Register */
    if (!str) {
      *data = uart->lsr;
    }
  }
  return RV_OK;
}

void rv_uart_update(rv_uart *uart) {
  rv_u8 in_byte;
  if (!(uart->lsr & LSR_THE)) {
    rv_u8 out = (rv_u8)(uart->thr & 0xFF);
    uart->cb(uart->user, &out, 1);
    uart->lsr |= LSR_TSE | LSR_THE;
  }
  if (!(uart->lsr & LSR_DR) && uart->cb(uart->user, &in_byte, 0) == RV_OK) {
    uart->lsr |= LSR_DR;
    uart->rbr = in_byte;
  }
}
