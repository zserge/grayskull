CFLAGS ?= -std=c99 -Wall -Wextra -Werror -pedantic -g
LDFLAGS ?= -lm

all: test nanomagick document_scanner aruco_detector

test:
	$(CC) $(CFLAGS) -o test test.c $(LDFLAGS)
	./test

testdata: nanomagick
	mkdir -p out
	./nanomagick identify testdata/grayskull.pgm
	./nanomagick view testdata/grayskull.pgm
	./nanomagick identify testdata/lena.pgm
	./nanomagick resize 128 64 testdata/lena.pgm out/lena_128x64.pgm
	./nanomagick crop 32 32 64 64 testdata/lena.pgm out/lena_crop.pgm
	./nanomagick blur 1 testdata/lena.pgm out/lena_blur.pgm
	./nanomagick blur 9 testdata/lena.pgm out/lena_blur_9.pgm
	./nanomagick threshold 128 out/lena_blur.pgm out/lena_threshold_128.pgm
	./nanomagick threshold otsu out/lena_blur.pgm out/lena_otsu.pgm
	./nanomagick adaptive 15 5 testdata/lena.pgm out/lena_adaptive.pgm
	./nanomagick morph erode 2 out/lena_otsu.pgm out/lena_erode.pgm
	./nanomagick morph dilate 2 out/lena_erode.pgm out/lena_dilate.pgm
	./nanomagick sobel testdata/lena.pgm - | ./nanomagick view -
	./nanomagick blur 3 testdata/aruco.pgm - | \
		./nanomagick sobel - - | \
		./nanomagick threshold otsu - - | \
		./nanomagick morph dilate 9 - - | \
		./nanomagick morph erode 10 - - | \
		./nanomagick blobs 150 - out/aruco.pgm
		./nanomagick view out/aruco.pgm
	./nanomagick scan testdata/document.pgm out/document.pgm
	./nanomagick scan testdata/receipt.pgm out/receipt.pgm

nanomagick: examples/nanomagick/nanomagick.c grayskull.h
	$(CC) $(CFLAGS) -I. -o nanomagick examples/nanomagick/nanomagick.c $(LDFLAGS)

wasm: examples/wasm/grayskull.c grayskull.h
	clang \
		--target=wasm32 -O3 -flto -nostdlib -Wl,--no-entry -Wl,--export-all -Wl,--lto-O3 -DNDEBUG \
		-I. -o examples/wasm/grayskull.wasm examples/wasm/grayskull.c

.PHONY: all test testdata
