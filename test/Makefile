RVOD:=riscv64-unknown-elf-objdump
RVOC:=riscv64-unknown-elf-objcopy
CC:=cc
RISCV_TESTS:=$(RISCV)/target/share/riscv-tests

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
	rm -rf vectors/*.dump
	find vectors -not -path vectors -name '*' ! -name '*.dmp' -exec bash -c "$(RVOD) -D -M no-aliases -M numeric '{}' > '{}.dmp'" \;
	find vectors -not -path vectors -name '*' ! -name '*.dmp' -exec bash -c "$(RVOC) -O binary '{}' '{}'" \;

run_test: run_test.c ../rv.c
	$(CC) --std=c89 -Wall -Wextra -pedantic -Wshadow run_test.c ../rv.c -g -o run_test

run_test_debug: run_test.c ../rv.c
	$(CC) --std=c89 -Wall -Wextra -pedantic -Wshadow run_test.c ../rv.c -DRV_VERBOSE=1 -g -o run_test_debug

rv32%: vectors run_test_debug
	./run_test_debug vectors/$@

clean:
	rm -rf vectors run_test run_test_debug *.dSYM
