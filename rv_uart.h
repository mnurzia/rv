#ifndef RV_UART_H
#define RV_UART_H

#include "rv.h"

#include <string.h>

typedef rv_res (*rv_uart_cb)(void *user, rv_u8 *byte, rv_u32 write);

typedef struct rv_uart {
  rv_uart_cb cb;
  void *user;
  rv_u32 lsr, iir, rbr, thr, ier;
} rv_uart;

void rv_uart_init(rv_uart *uart, void *user, rv_uart_cb cb);
rv_res rv_uart_bus(rv_uart *uart, rv_u32 addr, rv_u32 *data, rv_u32 str);
void rv_uart_update(rv_uart *uart);
int rv_uart_i(rv_uart *uart);

#endif
