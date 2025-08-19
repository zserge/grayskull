#ifndef GRAYSKULL_H
#define GRAYSKULL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define GS_API static inline

#define GS_MIN(a, b) ((a) < (b) ? (a) : (b))
#define GS_MAX(a, b) ((a) > (b) ? (a) : (b))

struct gs_point {
  unsigned x, y;
};

struct gs_rect {
  unsigned x, y, w, h;
};
struct gs_image {
  unsigned w, h;
  uint8_t *data;
};

#define gs_for(img, x, y)                \
  for (unsigned y = 0; y < (img).h; y++) \
    for (unsigned x = 0; x < (img).w; x++)

#ifndef gs_assert
#define gs_assert(cond)                               \
  if (!(cond)) {                                      \
    fprintf(stderr, "Assertion failed: %s\n", #cond); \
    abort();                                          \
  }
#endif

GS_API int gs_valid(struct gs_image img) { return img.data != NULL && img.w > 0 && img.h > 0; }

GS_API int gs_crop(struct gs_image dst, struct gs_image src, struct gs_rect roi) {
  gs_assert(gs_valid(dst) && gs_valid(src) && roi.x + roi.w <= src.w && roi.y + roi.h <= src.h &&
            dst.w == roi.w && dst.h == roi.h);
  gs_for(roi, x, y) dst.data[y * dst.w + x] = src.data[(roi.y + y) * src.w + (roi.x + x)];
  return 0;
}
#define gs_copy(dst, src) gs_crop(dst, src, (struct gs_rect){0, 0, src.w, src.h})

GS_API void gs_resize(struct gs_image dst, struct gs_image src) {
  gs_assert(gs_valid(dst) && gs_valid(src));
  gs_for(dst, x, y) {
    float sx = ((float)x + 0.5f) * src.w / dst.w - 0.5f;  // 0.5f centers the pixel
    float sy = ((float)y + 0.5f) * src.h / dst.h - 0.5f;
    sx = GS_MAX(0.0f, GS_MIN(sx, src.w - 1.0f));
    sy = GS_MAX(0.0f, GS_MIN(sy, src.h - 1.0f));
    unsigned sx_int = (unsigned)sx, sy_int = (unsigned)sy;
    unsigned sx1 = GS_MIN(sx_int + 1, src.w - 1), sy1 = GS_MIN(sy_int + 1, src.h - 1);
    float dx = sx - sx_int, dy = sy - sy_int;
    uint8_t c00 = src.data[sy_int * src.w + sx_int];
    uint8_t c01 = src.data[sy_int * src.w + sx1];
    uint8_t c10 = src.data[sy1 * src.w + sx_int];
    uint8_t c11 = src.data[sy1 * src.w + sx1];
    dst.data[y * dst.w + x] = (uint8_t)((c00 * (1 - dx) * (1 - dy)) + (c01 * dx * (1 - dy)) +
                                        (c10 * (1 - dx) * dy) + (c11 * dx * dy));
  }
}

GS_API void gs_histogram(struct gs_image img, unsigned hist[256]) {
  gs_assert(gs_valid(img) && hist != NULL);
  for (unsigned i = 0; i < 256; i++) hist[i] = 0;
  for (unsigned i = 0; i < img.w * img.h; i++) hist[img.data[i]]++;
}

GS_API uint8_t gs_otsu_theshold(struct gs_image img) {
  gs_assert(gs_valid(img));
  unsigned hist[256] = {0};
  gs_histogram(img, hist);
  unsigned sum = 0, wb = 0, wf = 0, threshold = 0;
  for (unsigned i = 0; i < 256; i++) sum += i * hist[i];
  float sumB = 0, varMax = 0;
  for (unsigned t = 0; t < 256 && wb < img.w * img.h; t++) {
    wb += hist[t];
    wf = img.w * img.h - wb;
    sumB += (float)(t * hist[t]);
    float mB = sumB / wb, mF = (sum - sumB) / wf;
    float varBetween = (float)(wb * wf) * (mB - mF) * (mB - mF);
    /*printf("  t=%d, mF=%f mB=%f, wB=%d, wF=%d, varB=%f, varM=%f, thr=%d\n", t, mF, mB, wb, wf,
     * varBetween, varMax, threshold);*/
    if (varBetween > varMax) varMax = varBetween, threshold = t;
  }
  return threshold;
}

GS_API void gs_threshold(struct gs_image img, uint8_t thresh) {
  gs_assert(gs_valid(img));
  for (unsigned i = 0; i < img.w * img.h; i++) img.data[i] = (img.data[i] > thresh) ? 255 : 0;
}

GS_API void gs_adaptive_threshold(struct gs_image dst, struct gs_image src, unsigned block_size,
                                  float c) {
  gs_assert(gs_valid(dst) && gs_valid(src) && dst.w == src.w && dst.h == src.h);
  gs_assert(block_size > 0 && block_size % 2 == 1);  // block_size must be odd

  int radius = block_size / 2;

  gs_for(src, x, y) {
    unsigned sum = 0, count = 0;
    for (int dy = -radius; dy <= radius; dy++) {
      for (int dx = -radius; dx <= radius; dx++) {
        int sy = (int)y + dy, sx = (int)x + dx;
        if (sy >= 0 && sy < (int)src.h && sx >= 0 && sx < (int)src.w) {
          sum += src.data[sy * src.w + sx];
          count++;
        }
      }
    }
    int threshold = sum / count - c;
    dst.data[y * dst.w + x] = (src.data[y * src.w + x] > threshold) ? 255 : 0;
  }
}

GS_API void gs_blur(struct gs_image dst, struct gs_image src, int radius) {
  gs_assert(gs_valid(src) && gs_valid(dst) && dst.w == src.w && dst.h == src.h && radius > 0);
  gs_for(src, x, y) {
    unsigned sum = 0, count = 0;
    for (int dy = -radius; dy <= radius; dy++) {
      for (int dx = -radius; dx <= radius; dx++) {
        unsigned sy = y + dy, sx = x + dx;
        if (sy >= 0 && sy < src.h && sx >= 0 && sx < src.w) {
          sum += src.data[sy * src.w + sx];
          count++;
        }
      }
    }
    dst.data[y * dst.w + x] = (uint8_t)(sum / count);
  }
}

enum { GS_ERODE, GS_DILATE };
GS_API void gs_morph(struct gs_image dst, struct gs_image src, int op) {
  gs_assert(gs_valid(dst) && gs_valid(src) && dst.w == src.w && dst.h == src.h);
  gs_for(src, x, y) {
    uint8_t val = op == GS_ERODE ? 255 : 0;
    for (int dy = -1; dy <= 1; dy++) {
      for (int dx = -1; dx <= 1; dx++) {
        int sy = (int)y + dy, sx = (int)x + dx;
        if (sy >= 0 && sy < (int)src.h && sx >= 0 && sx < (int)src.w) {
          uint8_t pixel = src.data[sy * src.w + sx];
          if (op == GS_DILATE && pixel > val) val = pixel;
          if (op == GS_ERODE && pixel < val) val = pixel;
        }
      }
    }
    dst.data[y * dst.w + x] = val;
  }
}
#define gs_erode(dst, src) gs_morph(dst, src, GS_ERODE)
#define gs_dilate(dst, src) gs_morph(dst, src, GS_DILATE)

GS_API void gs_sobel(struct gs_image dst, struct gs_image src) {
  gs_assert(gs_valid(dst) && gs_valid(src) && dst.w == src.w && dst.h == src.h);
  for (unsigned y = 1; y < src.h - 1; y++) {
    for (unsigned x = 1; x < src.w - 1; x++) {
      int gx = -src.data[(y - 1) * src.w + (x - 1)] + src.data[(y - 1) * src.w + (x + 1)] -
               2 * src.data[y * src.w + (x - 1)] + 2 * src.data[y * src.w + (x + 1)] -
               src.data[(y + 1) * src.w + (x - 1)] + src.data[(y + 1) * src.w + (x + 1)];
      int gy = -src.data[(y - 1) * src.w + (x - 1)] - 2 * src.data[(y - 1) * src.w + x] -
               src.data[(y - 1) * src.w + (x + 1)] + src.data[(y + 1) * src.w + (x - 1)] +
               2 * src.data[(y + 1) * src.w + x] + src.data[(y + 1) * src.w + (x + 1)];
      int magnitude = (abs(gx) + abs(gy)) / 2;
      dst.data[y * dst.w + x] = (uint8_t)GS_MAX(0, GS_MIN(magnitude, 255));
    }
  }
}

// ==============================================================================
// ==============================================================================
// ==============================================================================
// ==============================================================================
GS_API struct gs_image gs_alloc(unsigned w, unsigned h) {
  if (w == 0 || h == 0) return (struct gs_image){0, 0, NULL};
  uint8_t *data = (uint8_t *)calloc(w * h, sizeof(uint8_t));
  return (struct gs_image){w, h, data};
}
GS_API void gs_free(struct gs_image img) { free(img.data); }

GS_API struct gs_image gs_read_pgm(const char *path) {
  struct gs_image img = {0, 0, NULL};
  FILE *f = fopen(path, "rb");
  if (!f) return img;
  unsigned w, h, maxval;
  if (fscanf(f, "P5\n%u %u\n%u\n", &w, &h, &maxval) != 3 || maxval != 255) goto end;
  img = gs_alloc(w, h);
  if (!gs_valid(img)) goto end;
  if (fread(img.data, sizeof(uint8_t), w * h, f) != (size_t)(w * h)) {
    gs_free(img);
    img = (struct gs_image){0, 0, NULL};
  }
end:
  fclose(f);
  return img;
}

GS_API int gs_write_pgm(struct gs_image img, const char *path) {
  if (!gs_valid(img)) return -1;
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  fprintf(f, "P5\n%u %u\n255\n", img.w, img.h);
  size_t written = fwrite(img.data, sizeof(uint8_t), img.w * img.h, f);
  fclose(f);
  return (written == (size_t)(img.w * img.h)) ? 0 : -1;
}
#endif  // GRAYSKULL_H
