CFLAGS ?= -std=c99 -Wall -Wextra -Werror -pedantic -g

all: test nanomagick document_scanner aruco_detector

test:
	$(CC) $(CFLAGS) -o test test.c $(LDFLAGS)
	./test

testdata: nanomagick document_scanner aruco_detector
	mkdir -p out
	./nanomagick identify testdata/grayskull.pgm
	./nanomagick view testdata/grayskull.pgm
	./nanomagick identify testdata/lena.pgm
	./nanomagick view testdata/lena.pgm
	./nanomagick resize 128 64 testdata/lena.pgm out/lena_128x64.pgm
	./nanomagick crop 32 32 64 64 testdata/lena.pgm out/lena_crop.pgm
	./nanomagick blur 1 testdata/lena.pgm out/lena_blur.pgm
	./nanomagick blur 9 testdata/lena.pgm out/lena_blur_9.pgm
	./nanomagick threshold 128 out/lena_blur.pgm out/lena_threshold_128.pgm
	./nanomagick otsu out/lena_blur.pgm out/lena_otsu.pgm
	./nanomagick erode out/lena_otsu.pgm out/lena_erode.pgm
	./nanomagick erode out/lena_erode.pgm out/lena_erode2.pgm
	./nanomagick dilate out/lena_erode2.pgm out/lena_dilate.pgm
	./nanomagick dilate out/lena_erode2.pgm out/lena_dilate2.pgm
	./nanomagick sobel testdata/lena.pgm out/lena_sobel.pgm
	./nanomagick adaptive 15 5 testdata/lena.pgm out/lena_adaptive.pgm
	# document scanner
	./document_scanner testdata/receipt.pgm out/scan_receipt.pgm
	./document_scanner testdata/document.pgm out/scan_document.pgm

nanomagick: examples/nanomagick/nanomagick.c grayskull.h
	$(CC) $(CFLAGS) -I. -o nanomagick examples/nanomagick/nanomagick.c $(LDFLAGS)

document_scanner: examples/document_scanner/document_scanner.c grayskull.h
	$(CC) $(CFLAGS) -I. -o document_scanner examples/document_scanner/document_scanner.c $(LDFLAGS)

aruco_detector: examples/aruco_detector/aruco_detector.c grayskull.h
	$(CC) $(CFLAGS) -I. -o aruco_detector examples/aruco_detector/aruco_detector.c -lm $(LDFLAGS)

.PHONY: all test testdata
