SRCS=rv.c rv_clint.c rv_plic.c rv_uart.c mach.c
HDRS=rv.h rv_clint.h rv_plic.h rv_uart.h

CFLAGS=--std=c89 -Wall -Wextra -pedantic -Wshadow -g

mach: mach.c $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(SRCS) -o $@

mach-fast: mach.c $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(SRCS) -o $@

build/buildroot.tar.xz:
	mkdir -p build
	curl -L https://buildroot.org/downloads/buildroot-2023.11.1.tar.gz -o $@
	tar -C build -xf $@ 

linux: build/buildroot.tar.xz
	make BR2_EXTERNAL=$(shell realpath ./buildroot) -C build/buildroot* rv_defconfig
	make BR2_EXTERNAL=$(shell realpath ./buildroot) -C build/buildroot* 
	make BR2_EXTERNAL=$(shell realpath ./buildroot) -C build/buildroot* linux-rebuild opensbi-rebuild all

clean:
	rm -rf mach mach-fast
