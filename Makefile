RVCC := /opt/homebrew/opt/riscv-gnu-toolchain/bin/riscv64-unknown-elf-gcc
RVOD := /opt/homebrew/opt/riscv-gnu-toolchain/bin/riscv64-unknown-elf-objdump
RVOC := /opt/homebrew/opt/riscv-gnu-toolchain/bin/riscv64-unknown-elf-objcopy
CC := clang

all: bin/test_prog.bin bin/rv bin/test_prog.dmp

bin:
	mkdir -p bin

tests:
	mkdir -p tests

bin/test_prog.o: bin test_prog.c rt.s link.ld
	$(RVCC) -nostdlib -nostartfiles -Tlink.ld -march=rv32i -mabi=ilp32 -o bin/test_prog.o test_prog.c rt.s -e _start -O

bin/test_prog.bin: bin bin/test_prog.o
	$(RVOC) -O binary bin/test_prog.o bin/test_prog.bin

bin/test_prog.dmp: bin bin/test_prog.o
	$(RVOD) -D -M no-aliases -M numeric bin/test_prog.o > bin/test_prog.dmp

bin/rv: bin rv.c
	$(CC) -o bin/rv rv.c -Wall -Werror --std=c89 -pedantic -Wextra -g

dump: bin bin/test_prog.dmp
	cat bin/test_prog.dmp

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
