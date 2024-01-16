RVOD=riscv64-unknown-elf-objdump
RVOC=riscv64-unknown-elf-objcopy
CC=cc
RISCV_TESTS=$(RISCV)/target/share/riscv-tests
CFLAGS=--std=c89 -Wall -Wextra -pedantic -Wshadow -g

all: test

test: vectors run_test
	python test.py

vectors:
	mkdir -p vectors
	cp $(RISCV_TESTS)/isa/rv32ui-p-* vectors
	cp $(RISCV_TESTS)/isa/rv32uc-p-* vectors
	cp $(RISCV_TESTS)/isa/rv32um-p-* vectors
	cp $(RISCV_TESTS)/isa/rv32ua-p-* vectors
	cp $(RISCV_TESTS)/isa/rv32si-p-* vectors
	cp $(RISCV_TESTS)/isa/rv32mi-p-* vectors
	rm -rf vectors/rv32mi-p-breakpoint # breakpoints not supported
	rm -rf vectors/*.dump
	find vectors -not -path vectors -name '*' ! -name '*.dmp' -exec bash -c "$(RVOD) -D -M no-aliases -M numeric '{}' > '{}.dmp'" \;
	find vectors -not -path vectors -name '*' ! -name '*.dmp' -exec bash -c "$(RVOC) -O binary '{}' '{}'" \;

run_test: run_test.c rv.c
	$(CC) $(CFLAGS) run_test.c rv.c -o $@

rv32%: vectors run_test
	./run_test vectors/$@

clean:
	rm -rf vectors run_test run_test_debug *.dSYM
