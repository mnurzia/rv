SRCS=rv.c example.c
HDRS=rv.h

CFLAGS=--std=c89 -Wall -Wextra -pedantic -Wshadow -g

example: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(SRCS) -o $@

clean:
	rm -rf example
