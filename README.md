# 🏰 Grayskull

Grayskull is a minimalist, dependency-free computer vision library designed for microcontrollers and other resource-constrained devices. It focuses on **grayscale** images and provides modern, practical algorithms that fit in a few kilobytes of code. Single-header design, integer-based operations, pure C99.

## Features

* Image operations: copy, crop, resize (bilinear), downsample
* Filtering: blur, Sobel edges, thresholding (global, Otsu, adaptive)
* Morphology: erosion, dilation
* Geometry: connected components, perspective warp
* Features: FAST/ORB keypoints and descriptors (object tracking)
* Local binary patterns: LBP cascades to detect faces, vehicles etc
* Utilities: PGM read/write

As usual, no dependencies, no dynamic memory allocation, no C++, no surprises. Just a single header file under 1KLOC.

Check out the [examples](examples) folder for more!

[Online demo](https://zserge.com/grayskull/): try Grayskull in your browser.

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
struct gs_image { unsigned w, h; uint8_t *data; };
struct gs_rect { unsigned x, y, w, h; }; // ROI
struct gs_point { unsigned x, y; }; // corners

uint8_t gs_get(struct gs_image img, unsigned x, unsigned y);
void gs_set(struct gs_image img, unsigned x, unsigned y, uint8_t value);
void gs_crop(struct gs_image dst, struct gs_image src, struct gs_rect roi);
void gs_copy(struct gs_image dst, struct gs_image src);
void gs_resize(struct gs_image dst, struct gs_image src);
void gs_downsample(struct gs_image dst, struct gs_image src);

// Thresholding
void gs_histogram(struct gs_image img, unsigned hist[256]);
void gs_threshold(struct gs_image img, uint8_t threshold);
uint8_t gs_otsu_threshold(struct gs_image img);
void gs_adaptive_threshold(struct gs_image dst, struct gs_image src, unsigned radius, int c);

// Filters
void gs_blur(struct gs_image dst, struct gs_image src, unsigned radius);
void gs_erode(struct gs_image dst, struct gs_image src);
void gs_dilate(struct gs_image dst, struct gs_image src);
void gs_sobel(struct gs_image dst, struct gs_image src);

// Blobs (connected components) and contours
typedef uint16_t gs_label;
struct gs_blob { gs_label label; unsigned area; struct gs_rect box; struct gs_point centroid; };
struct gs_contour { struct gs_rect box; struct gs_point start; unsigned length; };
unsigned gs_blobs(struct gs_image img, gs_label *labels, struct gs_blob *blobs, unsigned nblobs);
void gs_blob_corners(struct gs_image img, gs_label *labels, struct gs_blob *b, struct gs_point c[4]);
void gs_perspective_correct(struct gs_image dst, struct gs_image src, struct gs_point c[4]);
void gs_trace_contour(struct gs_image img, struct gs_image visited, struct gs_contour *c);

// FAST/ORB
struct gs_keypoint { struct gs_point pt; unsigned response; float angle; uint32_t descriptor[8]; };
struct gs_match { unsigned idx1, idx2; unsigned distance; };
unsigned gs_fast(struct gs_image img, struct gs_image scoremap, struct gs_keypoint *kps, unsigned nkps, unsigned threshold);
float gs_compute_orientation(struct gs_image img, unsigned x, unsigned y, unsigned r);
void gs_brief_descriptor(struct gs_image img, struct gs_keypoint *kp);
unsigned gs_orb_extract(struct gs_image img, struct gs_keypoint *kps, unsigned nkps, unsigned threshold, uint8_t *scoremap_buffer);
unsigned gs_match_orb(const struct gs_keypoint *kps1, unsigned n1, const struct gs_keypoint *kps2, unsigned n2, struct gs_match *matches, unsigned max_matches, float max_distance);

// LBP cascades
struct gs_lbp_cascade { uint16_t window_w, window_h; uint16_t nfeatures, nweaks, nstages; const int8_t *features; /* [nfeatures * 4] */ const uint16_t *weak_feature_idx; const float *weak_left_val, *weak_right_val; const uint16_t *weak_subset_offset, *weak_num_subsets; const int32_t *subsets; const uint16_t *stage_weak_start, *stage_nweaks; const float *stage_threshold; };
void gs_integral(struct gs_image src, unsigned *ii);
unsigned gs_lbp_window(const struct gs_lbp_cascade *c, const unsigned *ii, unsigned iw, unsigned ih, int x, int y, float scale);
unsigned gs_lbp_detect(const struct gs_lbp_cascade *c, const unsigned *ii, unsigned iw, unsigned ih, struct gs_rect *rects, unsigned max_rects, float scale_factor, float min_scale, float max_scale, int step);

// Optional:
struct gs_image gs_alloc(unsigned w, unsigned h);
void gs_free(struct gs_image img);
struct gs_image gs_read_pgm(const char *path);
int gs_write_pgm(struct gs_image img, const char *path);
```

## License

This project is licensed under the MIT License. Feel free to use in research, products, and your next embedded vision project!
