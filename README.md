# 🏰 Grayskull

Grayskull is a minimalist, dependency-free alternative to OpenCV designed for microcontrollers and other resource-constrained devices. It focuses on **grayscale** images and provides modern, practical algorithms that fit in a few kilobytes of code. Single-header design, integer-based operations, pure C99.

## Features

* Image operations: copy, crop, resize (bilinear)
* Filtering: blur, Sobel edges, thresholding (global, Otsu, adaptive)
* Morphology: erosion, dilation
* Geometry: connected components, perspective warp
* Utilities: PGM read/write

## Quickstart

```c
#include "grayskull.h"

struct gs_image img = gs_read_pgm("input.pgm");
struct gs_image blurred = gs_alloc(img.w, img.h);
struct gs_image binary = gs_alloc(img.w, img.h);

gs_blur(blurred, img, 2);
gs_threshold(binary, blurred, gs_otsu_theshold(blurred));

gs_write_pgm(binary, "output.pgm");
gs_free(img);
gs_free(blurred);
gs_free(binary);
```

_Note that `gs_alloc`/`gs_free` are optional helpers; you can allocate image pixel buffers any way you like._

## API Reference

```c
struct gs_point { unsigned x, y; }; // corners
struct gs_rect { unsigned x, y, w, h; }; // ROI
struct gs_quad { struct gs_point corners[4]; };
struct gs_image { unsigned w, h; uint8_t *data; };

void gs_crop(struct gs_image dst, struct gs_image src, struct gs_rect roi);
void gs_copy(struct gs_image dst, struct gs_image src);
void gs_resize(struct gs_image dst, struct gs_image src);

void gs_histogram(struct gs_image img, unsigned hist[256]);
void gs_threshold(struct gs_image img, uint8_t threshold);
uint8_t gs_otsu_theshold(struct gs_image img);
void gs_adaptive_threshold(struct gs_image dst, struct gs_image src, unsigned block_size, float c);

void gs_blur(struct gs_image dst, struct gs_image src, int radius);

void gs_erode(struct gs_image dst, struct gs_image src);
void gs_dilate(struct gs_image dst, struct gs_image src);

void gs_sobel(struct gs_image dst, struct gs_image src);

unsigned gs_connected_components(struct gs_image img, gs_label *labels,
                                        struct gs_component *comp, size_t comp_size,
                                        gs_label *table, size_t table_size, int fullconn);

void gs_perspective_correct(struct gs_image dst, struct gs_image src, struct gs_quad quad);

// Optional:
struct gs_image gs_alloc(unsigned w, unsigned h);
void gs_free(struct gs_image img);
struct gs_image gs_read_pgm(const char *path);
int gs_write_pgm(struct gs_image img, const char *path);
```

## License

This project is licensed under the MIT License. Feel free to use in research, products, and your next embedded vision project!
