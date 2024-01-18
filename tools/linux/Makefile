SRCS=rv.c rv_clint.c rv_plic.c rv_uart.c mach.c
HDRS=rv.h rv_clint.h rv_plic.h rv_uart.h

CFLAGS=--std=c89 -Wall -Wextra -pedantic -Wshadow -g
LIBS=-lncurses

mach: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LIBS)

mach-fast: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -O3 $(SRCS) -o $@ $(LIBS)

clean:
	rm -rf mach mach-fast
