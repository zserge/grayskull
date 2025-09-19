#ifndef GRAYSKULL_H
#define GRAYSKULL_H

#include <limits.h>
#include <stdint.h>

#ifndef GS_API
#define GS_API static inline
#endif

#define GS_MIN(a, b) ((a) < (b) ? (a) : (b))
#define GS_MAX(a, b) ((a) > (b) ? (a) : (b))

struct gs_point {
  unsigned x, y;
};

struct gs_rect {
  unsigned x, y, w, h;
};

typedef uint8_t gs_label;

struct gs_blob {
  gs_label label;
  unsigned area;
  struct gs_rect box;
  struct gs_point centroid;
};

struct gs_contour {
  struct gs_rect box;
  struct gs_point start;
  unsigned length;
};

struct gs_image {
  unsigned w, h;
  uint8_t *data;
};

static inline int gs_valid(struct gs_image img) { return img.data && img.w > 0 && img.h > 0; }

#ifdef GS_NO_STDLIB  // no asserts, no memory allocation, no file I/O
#define gs_assert(cond)
#else
#include <stdio.h>
#include <stdlib.h>

#define gs_assert(cond)                               \
  if (!(cond)) {                                      \
    fprintf(stderr, "Assertion failed: %s\n", #cond); \
    abort();                                          \
  }

GS_API struct gs_image gs_alloc(unsigned w, unsigned h) {
  if (w == 0 || h == 0) return (struct gs_image){0, 0, NULL};
  uint8_t *data = (uint8_t *)calloc(w * h, sizeof(uint8_t));
  return (struct gs_image){w, h, data};
}

GS_API void gs_free(struct gs_image img) { free(img.data); }

GS_API struct gs_image gs_read_pgm(const char *path) {
  struct gs_image img = {0, 0, NULL};
  FILE *f = (path[0] == '-' && !path[1]) ? stdin : fopen(path, "rb");
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
  FILE *f = (path[0] == '-' && !path[1]) ? stdout : fopen(path, "wb");
  if (!f) return -1;
  fprintf(f, "P5\n%u %u\n255\n", img.w, img.h);
  size_t written = fwrite(img.data, sizeof(uint8_t), img.w * img.h, f);
  fclose(f);
  return (written == (size_t)(img.w * img.h)) ? 0 : -1;
}
#endif  // GS_NO_STDLIB

#define gs_for(img, x, y)                \
  for (unsigned y = 0; y < (img).h; y++) \
    for (unsigned x = 0; x < (img).w; x++)

//
// Image processing
//

GS_API void gs_crop(struct gs_image dst, struct gs_image src, struct gs_rect roi) {
  gs_assert(gs_valid(dst) && gs_valid(src) && roi.x + roi.w <= src.w && roi.y + roi.h <= src.h &&
            dst.w == roi.w && dst.h == roi.h);
  gs_for(roi, x, y) dst.data[y * dst.w + x] = src.data[(roi.y + y) * src.w + (roi.x + x)];
}

GS_API void gs_copy(struct gs_image dst, struct gs_image src) {
  gs_crop(dst, src, (struct gs_rect){0, 0, src.w, src.h});
}

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
    if (varBetween > varMax) varMax = varBetween, threshold = t;
  }
  return threshold;
}

GS_API void gs_threshold(struct gs_image img, uint8_t thresh) {
  gs_assert(gs_valid(img));
  for (unsigned i = 0; i < img.w * img.h; i++) img.data[i] = (img.data[i] > thresh) ? 255 : 0;
}

GS_API void gs_adaptive_threshold(struct gs_image dst, struct gs_image src, unsigned radius,
                                  int c) {
  gs_assert(gs_valid(dst) && gs_valid(src) && dst.w == src.w && dst.h == src.h);
  gs_for(src, x, y) {
    unsigned sum = 0, count = 0;
    for (int dy = -radius; dy <= (int)radius; dy++) {
      for (int dx = -radius; dx <= (int)radius; dx++) {
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

GS_API void gs_blur(struct gs_image dst, struct gs_image src, unsigned radius) {
  gs_assert(gs_valid(src) && gs_valid(dst) && dst.w == src.w && dst.h == src.h);
  gs_for(src, x, y) {
    unsigned sum = 0, count = 0;
    for (int dy = -radius; dy <= (int)radius; dy++) {
      for (int dx = -radius; dx <= (int)radius; dx++) {
        int sy = y + dy, sx = x + dx;
        if (sy >= 0 && sy < (int)src.h && sx >= 0 && sx < (int)src.w) {
          sum += src.data[sy * src.w + sx];
          count++;
        }
      }
    }
    dst.data[y * dst.w + x] = (uint8_t)(sum / count);
  }
}

enum { GS_ERODE, GS_DILATE };
static inline void gs_morph(struct gs_image dst, struct gs_image src, int op) {
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
GS_API void gs_erode(struct gs_image dst, struct gs_image src) { gs_morph(dst, src, GS_ERODE); }
GS_API void gs_dilate(struct gs_image dst, struct gs_image src) { gs_morph(dst, src, GS_DILATE); }

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
      int magnitude = ((gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy)) / 2;
      dst.data[y * dst.w + x] = (uint8_t)GS_MAX(0, GS_MIN(magnitude, 255));
    }
  }
}

//
// Connected components (blobs)
//
static inline gs_label gs_root(gs_label x, gs_label *parents) {
  while (parents[x] != x) x = parents[x] = parents[parents[x]];
  return x;
}

GS_API unsigned gs_blobs(struct gs_image img, gs_label *labels, struct gs_blob *blobs,
                         unsigned nblobs) {
  gs_assert(gs_valid(img) && labels != NULL && blobs != NULL && nblobs > 0);
  unsigned w = img.w;
  gs_label next = 1, parents[nblobs + 1];
  for (unsigned i = 0; i < img.w * img.h; i++) labels[i] = 0;
  for (unsigned i = 0; i < nblobs; i++)
    blobs[i] = (struct gs_blob){0, 0, {UINT_MAX, UINT_MAX, 0, 0}, {0, 0}};
  for (unsigned i = 0; i <= nblobs; i++) parents[i] = i;
  // first pass: label and union
  gs_for(img, x, y) {
    if (img.data[y * w + x] < 128) continue;  // skip background pixels
    gs_label left = (x > 0) ? labels[y * w + (x - 1)] : 0;
    gs_label top = (y > 0) ? labels[(y - 1) * w + x] : 0;
    // 4-connectivity: pick smallest from left and top, if any is non-zero
    gs_label n = (left && top ? GS_MIN(left, top) : (left ? left : (top ? top : 0)));
    if (!n) {                       // new component
      if (next > nblobs) continue;  // out of labels
      blobs[next - 1] = (struct gs_blob){next, 1, {x, y, 1, 1}, {x, y}};
      labels[y * w + x] = next++;
    } else {  // existing component
      labels[y * w + x] = n;
      struct gs_blob *b = &blobs[n - 1];
      b->area++;
      b->box.x = GS_MIN(x, b->box.x), b->box.y = GS_MIN(y, b->box.y);
      // keep bottom-right point coordinates in w/h of the rect, adjust later
      b->box.w = GS_MAX(x, b->box.w), b->box.h = GS_MAX(y, b->box.h);
      // union if labels are different
      if (left && top && left != top) {
        gs_label root1 = gs_root(left, parents), root2 = gs_root(top, parents);
        if (root1 != root2) parents[GS_MAX(root1, root2)] = GS_MIN(root1, root2);
      }
    }
  }
  // merge blobs
  for (int i = 0; i < next - 1; i++) {
    gs_label root = gs_root(blobs[i].label, parents);
    if (root != blobs[i].label) {
      struct gs_blob *broot = &blobs[root - 1];
      broot->area += blobs[i].area;
      broot->box.x = GS_MIN(broot->box.x, blobs[i].box.x);
      broot->box.y = GS_MIN(broot->box.y, blobs[i].box.y);
      broot->box.w = GS_MAX(broot->box.w, blobs[i].box.w);
      broot->box.h = GS_MAX(broot->box.h, blobs[i].box.h);
      blobs[i].area = 0;
    }
  }
  // second pass: update labels
  gs_for(img, x, y) {
    gs_label l = labels[y * w + x];
    if (l) labels[y * w + x] = gs_root(l, parents);
  }

  // compact blobs
  unsigned m = 0;
  for (unsigned i = 0; i < next - 1; i++) {
    if (blobs[i].area == 0) continue;
    // fix rect width/height from bottom-right point to actual width/height
    blobs[i].box.w = blobs[i].box.w - blobs[i].box.x + 1;
    blobs[i].box.h = blobs[i].box.h - blobs[i].box.y + 1;
    blobs[m] = blobs[i], blobs[m].label = m + 1, m++;
  }

  return m;  // number of non-empty blobs
}

GS_API void gs_perspective_correct(struct gs_image dst, struct gs_image src, struct gs_point c[4]) {
  gs_assert(gs_valid(dst) && gs_valid(src));
  float w = dst.w - 1.0f, h = dst.h - 1.0f;
  gs_for(dst, x, y) {
    float u = x / w, v = y / h;
    float top_x = c[0].x * (1 - u) + c[1].x * u;
    float top_y = c[0].y * (1 - u) + c[1].y * u;
    float bot_x = c[3].x * (1 - u) + c[2].x * u;
    float bot_y = c[3].y * (1 - u) + c[2].y * u;
    float src_x = top_x * (1 - v) + bot_x * v;
    float src_y = top_y * (1 - v) + bot_y * v;
    src_x = GS_MAX(0.0f, GS_MIN(src_x, src.w - 1.0f));
    src_y = GS_MAX(0.0f, GS_MIN(src_y, src.h - 1.0f));
    unsigned sx = (unsigned)src_x, sy = (unsigned)src_y;
    unsigned sx1 = GS_MIN(sx + 1, src.w - 1), sy1 = GS_MIN(sy + 1, src.h - 1);
    float dx = src_x - sx, dy = src_y - sy;
    uint8_t c00 = src.data[sy * src.w + sx], c01 = src.data[sy * src.w + sx1],
            c10 = src.data[sy1 * src.w + sx], c11 = src.data[sy1 * src.w + sx1];
    dst.data[y * dst.w + x] = (uint8_t)((c00 * (1 - dx) * (1 - dy)) + (c01 * dx * (1 - dy)) +
                                        (c10 * (1 - dx) * dy) + (c11 * dx * dy));
  }
}

GS_API void gs_trace_contour(struct gs_image img, struct gs_image visited, struct gs_contour *c) {
  gs_assert(gs_valid(img) && gs_valid(visited) && img.w == visited.w && img.h == visited.h);
  static const int dx[] = {1, 1, 0, -1, -1, -1, 0, 1};
  static const int dy[] = {0, 1, 1, 1, 0, -1, -1, -1};

  c->length = 0;
  c->box = (struct gs_rect){c->start.x, c->start.y, 1, 1};

  struct gs_point p = c->start;
  unsigned dir = 7, seenstart = 0;

  for (;;) {
    if (!visited.data[p.y * visited.w + p.x]) c->length++;
    visited.data[p.y * visited.w + p.x] = 255;
    int ndir = (dir + 1) % 8, found = 0;
    for (int i = 0; i < 8; i++) {
      int d = (ndir + i) % 8, nx = p.x + dx[d], ny = p.y + dy[d];
      if (nx >= 0 && nx < (int)img.w && ny >= 0 && ny < (int)img.h &&
          img.data[ny * img.w + nx] > 128) {
        p = (struct gs_point){nx, ny};
        dir = (d + 6) % 8;
        found = 1;
        break;
      }
    }
    if (!found) break;  // open contour
    c->box.x = GS_MIN(c->box.x, p.x);
    c->box.y = GS_MIN(c->box.y, p.y);
    c->box.w = GS_MAX(c->box.w, p.x - c->box.x + 1);
    c->box.h = GS_MAX(c->box.h, p.y - c->box.y + 1);
    if (p.x == c->start.x && p.y == c->start.y) {
      if (seenstart) break;  // stop: second time at the starting point
      seenstart = 1;
    }
  }
}
#endif  // GRAYSKULL_H
