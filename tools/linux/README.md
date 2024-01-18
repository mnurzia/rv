# Linux Machine

This directory contains everything necessary to build and run a minimal Linux kernel targeting `rv`.

[`mach.c`](mach.c) implements a basic machine with the following hardware:
- [`rv.c`](../../rv.c) RISC-V cpu core (duh)
- [`rv_clint.c`](rv_clint.c) RISC-V Core-Local Interruptor (CLINT)
- [`rv_plic.c`](rv_plic.c) RISC-V Platform-Level Interrupt Controller (PLIC)
- [`rv_uart.c`](rv_uart.c) SiFive Universal Asynchronous Receiver/Transmitter (UART) (2x)

## Build
Congratulations, you get to compile Linux!

```shell
# [All commands are executed in this directory (rv/tools/linux)]
# download/extract buildroot
curl -L https://github.com/buildroot/buildroot/archive/refs/tags/2023.11.1.tar.gz -o buildroot.tar.gz
tar -xf buildroot.tar.gz -C buildroot
# build linux
make BR2_EXTERNAL=$(realpath extern) -C buildroot rv_defconfig 
make -C buildroot 
# build linux once more to fix initrd issues (should not take long)
make -C buildroot linux-rebuild opensbi-rebuild all
# build the machine
make mach
# run the machine
./mach buildroot/output/images/fw_payload.bin buildroot/output/images/rv.dtb
```
