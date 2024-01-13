#ifndef RV_UART_H
#define RV_UART_H

#include "rv.h"

typedef rv_res (*rv_uart_cb)(void *user, rv_u8 *byte, rv_u32 write);

#define RV_UART_FIFO_SIZE 8U

typedef struct rv_uart_fifo {
  rv_u8 buf[8];
  rv_u16 read, write;
  rv_u32 size;
} rv_uart_fifo;

typedef struct rv_uart {
  rv_uart_cb cb;
  void *user;
  rv_uart_fifo rx;
  rv_uart_fifo tx;
  rv_u32 txctrl, rxctrl, ip, ie, div, clk;
} rv_uart;

void rv_uart_init(rv_uart *uart, void *user, rv_uart_cb cb);
rv_res rv_uart_bus(rv_uart *uart, rv_u32 addr, rv_u32 *data, rv_u32 str);
rv_u32 rv_uart_update(rv_uart *uart);

#endif
