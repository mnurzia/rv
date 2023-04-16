RVCC := /opt/homebrew/opt/riscv-gnu-toolchain/bin/riscv64-unknown-elf-gcc
RVOD := /opt/homebrew/opt/riscv-gnu-toolchain/bin/riscv64-unknown-elf-objdump
RVOC := /opt/homebrew/opt/riscv-gnu-toolchain/bin/riscv64-unknown-elf-objcopy

all: bin/test_prog.bin bin/rv bin/test_prog.dmp

bin:
	mkdir bin

bin/test_prog.o: bin test_prog.c rt.s link.ld
	$(RVCC) -nostdlib -nostartfiles -Tlink.ld -march=rv32i -mabi=ilp32 -o bin/test_prog.o test_prog.c rt.s -e _start

bin/test_prog.bin: bin bin/test_prog.o
	$(RVOC) -O binary bin/test_prog.o bin/test_prog.bin

bin/test_prog.dmp: bin bin/test_prog.o
	$(RVOD) -D -M no-aliases bin/test_prog.o > bin/test_prog.dmp

bin/rv: bin rv.c
	gcc -o bin/rv rv.c -Wall -Werror --std=c89 -pedantic -Wextra -g

dump: bin bin/test_prog.dmp
	cat bin/test_prog.dmp

clean:
	rm -rf bin
