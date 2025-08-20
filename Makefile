CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -g

test:
	$(CC) $(CFLAGS) -o test test.c $(LDFLAGS)
	./test

nanomagick: examples/nanomagick/nanomagick.c grayskull.h
	$(CC) $(CFLAGS) -I. -o nanomagick examples/nanomagick/nanomagick.c $(LDFLAGS)

document_scanner: examples/document_scanner/document_scanner.c grayskull.h
	$(CC) $(CFLAGS) -I. -o document_scanner examples/document_scanner/document_scanner.c $(LDFLAGS)

.PHONY: test