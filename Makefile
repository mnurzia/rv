RVCC := /opt/homebrew/opt/riscv-gnu-toolchain/bin/riscv64-unknown-elf-gcc
RVOD := /opt/homebrew/opt/riscv-gnu-toolchain/bin/riscv64-unknown-elf-objdump
RVOC := /opt/homebrew/opt/riscv-gnu-toolchain/bin/riscv64-unknown-elf-objcopy
CC := clang

all: bin/rt.bin bin/rv bin/rt.dmp

bin:
	mkdir -p bin

tests:
	mkdir -p tests

bin/rt.o: bin rt/main.c rt/rt.s link.ld
	$(RVCC) -nostdlib -nostartfiles -Tlink.ld -march=rv32i -mabi=ilp32 -o bin/rt.o rt/main.c rt/rt.s -e _start -O -g -no-pie

bin/rt.bin: bin bin/rt.o
	$(RVOC) -g -O binary bin/rt.o bin/rt.bin

bin/rt.dmp: bin bin/rt.o
	$(RVOC) -g bin/rt.o bin/rt-nodbg.o
	$(RVOD) -D -M no-aliases -M numeric bin/rt-nodbg.o > bin/rt.dmp

bin/rv: bin rv.c
	$(CC) -o bin/rv rv.c -Wall -Werror --std=c89 -pedantic -Wextra -g -fsanitize=address

dump: bin bin/rt.dmp
	cat bin/rt.dmp

test-files: tests
	cp riscv-tests/isa/rv32ui-p-* tests
	cp riscv-tests/isa/rv32uc-p-* tests
	rm -rf tests/*.dump
	find tests -name '*' ! -name '*.dmp' -exec bash -c "$(RVOD) -D -M no-aliases -M numeric '{}' > '{}.dmp'" \;
	find tests -name '*' ! -name '*.dmp' -exec bash -c "$(RVOC) -O binary '{}' '{}'" \;

test: bin/rv test-files test.py
	python test.py

clean:
	rm -rf bin
	rm -rf tests
