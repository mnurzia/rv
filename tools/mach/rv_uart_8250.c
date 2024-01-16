#include "rv_uart.h"

#include <stdio.h>

#define LSR_DR (1U << 0)  /* receiver data ready */
#define LSR_THE (1U << 5) /* transmitter hold empty */
#define LSR_TSE (1U << 6) /* transmitter shift empty */

#define IER_EDA (1U << 0)
#define IER_TRE (1U << 1)
#define IER_RSI (1U << 2)
#define IER_MSI (1U << 3)

#define IIR_IIP (1U << 0)
#define IIR_INT(iir) (((iir) >> 1) & 3)

#define INT_RLS 3U /* receiver line status */
#define INT_RDA 2U /* received data available */
#define INT_THR 1U /* transmitter hold empty */
#define INT_MOD 0U /* modem status */

void rv_uart_init(rv_uart *uart, void *user, rv_uart_cb cb) {
  memset(uart, 0, sizeof(*uart));
  uart->cb = cb;
  uart->user = user;
  uart->iir = IIR_IIP;           /* no interrupts pending */
  uart->lsr = LSR_THE | LSR_TSE; /* nothing received */
  uart->lcr = 0x03;
  uart->msr = 0x10;
  uart->mcr = 0x8;
}

int rv_uart_irq(rv_uart *uart, rv_u32 idx) {
  rv_u32 current = !(uart->iir & IIR_IIP) ? IIR_INT(uart->iir) : 0;
  rv_u32 next = idx >= current ? idx : current;
  if (!((next == INT_MOD && (uart->ier & 8)) ||
        (next == INT_THR && (uart->ier & 2)) ||
        (next == INT_RDA && (uart->ier & 1)) ||
        (next == INT_RLS && (uart->ier & 4))))
    return !(uart->iir & IIR_IIP);
  uart->iir &= ~IIR_IIP;
  uart->iir = (uart->iir & ~6U) | (next << 1);
  return !(uart->iir & IIR_IIP);
}

void rv_uart_irqclr(rv_uart *uart, rv_u32 idx) {
  if (uart->iir & IIR_IIP)
    return;
  if (((uart->iir >> 1) & 3) == idx)
    uart->iir = 1;
}

rv_res rv_uart_bus(rv_uart *uart, rv_u32 addr, rv_u32 *data, rv_u32 str) {
  addr >>= 2;
  if (addr >= 0x20)
    return RV_BAD;
  if (addr == 0) {
    if (!str) { /*R Receiver Buffer Register */
      rv_uart_irqclr(uart, 2);
      rv_uart_irq(uart, INT_RLS);
      *data = uart->rbr, uart->lsr &= ~LSR_DR;
    } else { /*R Transmitter Holding Register */
      rv_uart_irqclr(uart, 1);
      rv_uart_irq(uart, INT_RLS);
      uart->thr = *data, uart->lsr &= ~LSR_THE;
    }
  } else if (addr == 1) { /*R Interrupt Enable Register */
    if (!str)
      *data = uart->ier;
    else
      uart->ier = *data & 0xF;
  } else if (addr == 2) { /*R Interrupt Identification Register */
    if (!str) {
      *data = uart->iir;
      rv_uart_irqclr(uart, 1);
    }
  } else if (addr == 3) { /*R Line Control Register */
    if (!str) {
      *data = uart->lcr;
    } else {
      uart->lcr = *data & 0x00;
    }
  } else if (addr == 4) { /*R Modem Control Register */
    if (!str) {
      *data = uart->mcr;
    } else {
      uart->mcr = *data & 0x00;
    }
  } else if (addr == 5) { /*R Line Status Register */
    if (!str) {
      rv_uart_irqclr(uart, INT_RLS);
      *data = uart->lsr;
    }
  } else if (addr == 6) { /*R Modem Status Register */
    if (!str) {
      rv_uart_irqclr(uart, INT_MOD);
      *data = uart->msr;
    }
  } else if (addr == 7) { /*R Scratch register */
    if (!str)
      *data = uart->scr;
    else
      uart->scr = *data;
  } else {
  }
  return RV_OK;
}

int rv_uart_update(rv_uart *uart) {
  rv_u8 in_byte;
  if (!(uart->lsr & LSR_THE)) {
    rv_u8 out = (rv_u8)(uart->thr & 0xFF);
    if (uart->cb(uart->user, &out, 1) == RV_OK) {
      uart->lsr |= LSR_TSE | LSR_THE;
      rv_uart_irq(uart, INT_THR);
      rv_uart_irq(uart, INT_RLS);
    }
  }
  if (!(uart->lsr & LSR_DR) && uart->cb(uart->user, &in_byte, 0) == RV_OK) {
    uart->rbr = in_byte;
    uart->lsr |= LSR_DR;
    rv_uart_irq(uart, INT_RDA);
    rv_uart_irq(uart, INT_RLS);
  }
  return !(uart->iir & 1);
}
