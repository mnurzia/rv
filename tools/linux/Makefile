SRCS=rv.c rv_clint.c rv_plic.c rv_uart.c mach.c
HDRS=rv.h rv_clint.h rv_plic.h rv_uart.h

CFLAGS=--std=c89 -Wall -Wextra -pedantic -Wshadow -g

mach: mach.c $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(SRCS) -o $@

mach-fast: mach.c $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(SRCS) -o $@

clean:
	rm -rf mach mach-fast
