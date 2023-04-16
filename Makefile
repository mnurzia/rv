RVCC := /opt/homebrew/opt/riscv-gnu-toolchain/bin/riscv64-unknown-elf-gcc
RVOD := /opt/homebrew/opt/riscv-gnu-toolchain/bin/riscv64-unknown-elf-objdump
RVOC := /opt/homebrew/opt/riscv-gnu-toolchain/bin/riscv64-unknown-elf-objcopy

all: bin/test_prog.bin rv

bin/test_prog.o: test_prog.c rt.s link.ld
	$(RVCC) -nostdlib -nostartfiles -Tlink.ld -march=rv32i -mabi=ilp32 -o bin/test_prog.o test_prog.c rt.s -e _start

bin/test_prog.bin: bin/test_prog.o
	$(RVOC) -O binary bin/test_prog.o bin/test_prog.bin

bin/rv: rv.c
	gcc -o bin/rv rv.c -Wall -Werror --std=c89 -pedantic -Wextra -g

dump: bin/test_prog.o
	$(RVOD) -D -M no-aliases bin/test_prog.o

clean:
	rm -rf bin/test_prog.o bin/test_prog.bin
	rm -rf rv
