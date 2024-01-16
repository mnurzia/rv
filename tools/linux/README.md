# Linux Machine

This directory contains everything necessary to build and run a minimal Linux kernel targeting `rv`.

[`mach.c`](mach.c) implements a basic machine with the following hardware:
- [`rv.c`](../../rv.c) RISC-V cpu core (duh)
- [`rv_clint.c`](rv_clint.c) RISC-V Core-Local Interrupt Controller (CLINT)
- [`rv_plic.c`](rv_plic.c) RISC-V Platform-Level Interrupt Controller (PLIC)
- [`rv_uart.c`](rv_uart.c) SiFive Universal Asynchronous Receiver/Transmitter (UART) (2x)
