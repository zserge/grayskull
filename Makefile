CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -g

test:
	$(CC) $(CFLAGS) -o test test.c $(LDFLAGS)
	./test

nanomagick: examples/nanomagick/nanomagick.c grayskull.h
	$(CC) $(CFLAGS) -I. -o nanomagick examples/nanomagick/nanomagick.c $(LDFLAGS)

.PHONY: test