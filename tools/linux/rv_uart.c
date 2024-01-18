#include "rv_uart.h"

#include <string.h>

void rv_uart_fifo_init(rv_uart_fifo *fifo) { memset(fifo, 0, sizeof(*fifo)); }

void rv_uart_fifo_put(rv_uart_fifo *fifo, rv_u8 byte) {
  if (fifo->size == RV_UART_FIFO_SIZE)
    return;
  fifo->buf[(fifo->read + fifo->size) & (RV_UART_FIFO_SIZE - 1)] = byte;
  fifo->size++;
}

rv_u8 rv_uart_fifo_get(rv_uart_fifo *fifo) {
  rv_u8 ch = fifo->buf[fifo->read];
  if (!fifo->size)
    return 0;
  fifo->read = (fifo->read + 1) & (RV_UART_FIFO_SIZE - 1);
  fifo->size--;
  return ch;
}

void rv_uart_init(rv_uart *uart, void *user, rv_uart_cb cb) {
  memset(uart, 0, sizeof(*uart));
  uart->user = user;
  uart->cb = cb;
  uart->div = 3;
  rv_uart_fifo_init(&uart->tx);
  rv_uart_fifo_init(&uart->rx);
}

rv_res rv_uart_bus(rv_uart *uart, rv_u32 addr, rv_u8 *d, rv_u32 is_store,
                   rv_u32 width) {
  rv_u32 *data = (rv_u32 *)d;
  if (width != 4)
    return RV_BAD_ALIGN;
  if (addr == 0x00) { /*R txdata */
    if (is_store)
      rv_uart_fifo_put(&uart->tx, (rv_u8)*data);
    else
      *data = (rv_u32)(uart->tx.size == RV_UART_FIFO_SIZE) << 31U;
  } else if (addr == 0x04) { /*R rxdata */
    if (!is_store)
      *data = ((rv_u32)(!uart->rx.size) << 31U) | rv_uart_fifo_get(&uart->rx);
  } else if (addr == 0x08) { /*R txctrl */
    if (is_store)
      uart->txctrl = *data & 0x00070003; /* no nstop supported */
    else
      *data = uart->txctrl;
  } else if (addr == 0x0C) { /*R rxctrl */
    if (is_store)
      uart->rxctrl = *data & 0x00070001;
    else
      *data = uart->rxctrl;
  } else if (addr == 0x10) { /*R ie */
    if (is_store)
      uart->ie = *data & 3;
    else
      *data = uart->ie;
  } else if (addr == 0x14) { /*R ip */
    if (!is_store)
      *data = uart->ip;
  } else if (addr == 0x18) { /*R div */
    if (is_store)
      uart->div = *data & 0xFFFF;
    else
      *data = uart->div;
  } else if (addr == 0x1C) { /*R unused */
    if (!is_store)
      *data = 0;
  } else {
    return RV_BAD;
  }
  return RV_OK;
}

rv_u32 rv_uart_update(rv_uart *uart) {
  rv_u8 byte = uart->tx.buf[uart->tx.read];
  if (++uart->clk >= uart->div) {
    if ((uart->txctrl & 1) && uart->tx.size &&
        (uart->cb(uart->user, &byte, 1) == RV_OK))
      rv_uart_fifo_get(&uart->tx);
    if ((uart->rxctrl & 1) && (uart->rx.size < RV_UART_FIFO_SIZE) &&
        (uart->cb(uart->user, &byte, 0) == RV_OK))
      rv_uart_fifo_put(&uart->rx, byte);
    uart->clk = 0;
  }
  if ((uart->txctrl >> 16) > uart->tx.size && (uart->ie & 1))
    uart->ip |= 1;
  else
    uart->ip &= ~(1U);
  if ((uart->rxctrl >> 16) < uart->rx.size && (uart->ie & 2))
    uart->ip |= 2;
  else
    uart->ip &= ~(2U);
  return !!uart->ip;
}
