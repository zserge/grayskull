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

typedef uint16_t gs_label;

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

struct gs_keypoint {
  struct gs_point pt;
  unsigned response;
  float angle;
  uint32_t descriptor[8];
};

struct gs_match {
  unsigned idx1, idx2;
  unsigned distance;
};

struct gs_image {
  unsigned w, h;
  uint8_t *data;
};

static inline int gs_valid(struct gs_image img) { return img.data && img.w > 0 && img.h > 0; }

#ifdef GS_NO_STDLIB  // no asserts, no memory allocation, no file I/O
#define gs_assert(cond)
static inline float gs_atan2(float y, float x) {
  if (x == 0.0f) { return (y > 0.0f ? 1.570796f : (y < 0.0f ? -1.570796f : 0.0f)); }
  float r, angle, abs_y = (y >= 0.0f ? y : -y);
  if (x >= 0.0f)
    r = (x - abs_y) / (x + abs_y), angle = 0.785398f - 0.785398f * r;
  else
    r = (x + abs_y) / (abs_y - x), angle = 3.0f * 0.785398f - 0.785398f * r;
  return (y < 0.0f ? -angle : angle);
}

static inline float gs_sin(float x) {
  while (x > 3.141592f) x -= 6.283185f;
  while (x < -3.141592f) x += 6.283185f;
  int sign = 1;
  if (x < 0) x = -x, sign = -1;
  if (x > 1.570796f) x = 3.141592f - x;
  float x2 = x * x, res = x * (1.0f - x2 * (0.16666667f - 0.0083333310f * x2));
  return sign * res;
}
#else
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define gs_assert(cond)                               \
  if (!(cond)) {                                      \
    fprintf(stderr, "Assertion failed: %s\n", #cond); \
    abort();                                          \
  }

static inline float gs_atan2(float y, float x) { return atan2f(y, x); }
static inline float gs_sin(float x) { return sinf(x); }

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

GS_API uint8_t gs_get(struct gs_image img, unsigned x, unsigned y) {
  return (gs_valid(img) && x < img.w && y < img.h) ? img.data[y * img.w + x] : 0;
}
GS_API void gs_set(struct gs_image img, unsigned x, unsigned y, uint8_t value) {
  if (gs_valid(img) && x < img.w && y < img.h) img.data[y * img.w + x] = value;
}

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

GS_API void gs_downsample(struct gs_image src, struct gs_image dst) {
  gs_assert(gs_valid(src) && gs_valid(dst) && dst.w == src.w / 2 && dst.h == src.h / 2);
  gs_for(dst, x, y) {
    unsigned src_x = x * 2, src_y = y * 2;
    unsigned sum = gs_get(src, src_x, src_y) + gs_get(src, src_x + 1, src_y) +
                   gs_get(src, src_x, src_y + 1) + gs_get(src, src_x + 1, src_y + 1);
    gs_set(dst, x, y, (uint8_t)(sum / 4));
  }
}

GS_API void gs_histogram(struct gs_image img, unsigned hist[256]) {
  gs_assert(gs_valid(img) && hist != NULL);
  for (unsigned i = 0; i < 256; i++) hist[i] = 0;
  for (unsigned i = 0; i < img.w * img.h; i++) hist[img.data[i]]++;
}

GS_API uint8_t gs_otsu_threshold(struct gs_image img) {
  gs_assert(gs_valid(img));
  unsigned hist[256] = {0}, wb = 0, wf = 0, threshold = 0;
  gs_histogram(img, hist);
  float sum = 0, sumB = 0, varMax = -1.0;
  for (unsigned i = 0; i < 256; i++) sum += (float)i * hist[i];
  for (unsigned t = 0; t < 256; t++) {
    wb += hist[t];
    if (wb == 0) continue;
    wf = img.w * img.h - wb;
    if (wf == 0) break;
    sumB += (float)t * hist[t];
    float mB = (float)sumB / wb;
    float mF = (float)(sum - sumB) / wf;
    float varBetween = (float)wb * (float)wf * (mB - mF) * (mB - mF);
    if (varBetween > varMax) varMax = varBetween, threshold = t;
  }
  return (uint8_t)threshold;
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
  unsigned cx[nblobs], cy[nblobs];
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
      blobs[next - 1] = (struct gs_blob){next, 1, {x, y, x, y}, {x, y}};
      cx[next - 1] = x, cy[next - 1] = y;
      labels[y * w + x] = next++;
    } else {  // existing component
      labels[y * w + x] = n;
      struct gs_blob *b = &blobs[n - 1];
      cx[n - 1] += x, cy[n - 1] += y;
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
      cx[root - 1] += cx[i], cy[root - 1] += cy[i];
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
  for (int i = 0; i < next - 1; i++) {
    if (blobs[i].area == 0) continue;
    // fix rect width/height from bottom-right point to actual width/height
    blobs[i].box.w = blobs[i].box.w - blobs[i].box.x + 1;
    blobs[i].box.h = blobs[i].box.h - blobs[i].box.y + 1;
    // calculate centroids
    blobs[i].centroid.x = cx[i] / blobs[i].area;
    blobs[i].centroid.y = cy[i] / blobs[i].area;
    // move to compacted position
    blobs[m++] = blobs[i];
  }

  return m;  // number of non-empty blobs
}

GS_API void gs_blob_corners(struct gs_image img, gs_label *labels, struct gs_blob *b,
                            struct gs_point c[4]) {
  gs_assert(gs_valid(img) && b && labels);
  struct gs_point tl = b->centroid, tr = b->centroid, br = b->centroid, bl = b->centroid;
  int min_sum = INT_MAX, max_sum = INT_MIN, min_diff = INT_MAX, max_diff = INT_MIN;
  for (unsigned y = b->box.y; y < b->box.y + b->box.h; y++) {
    for (unsigned x = b->box.x; x < b->box.x + b->box.w; x++) {
      if (img.data[y * img.w + x] < 128) continue;  // skip background pixels
      if (labels[y * img.w + x] != b->label) continue;
      int sum = (int)x + (int)y, diff = (int)x - (int)y;
      if (sum < min_sum) min_sum = sum, tl = (struct gs_point){x, y};
      if (sum > max_sum) max_sum = sum, br = (struct gs_point){x, y};
      if (diff < min_diff) min_diff = diff, bl = (struct gs_point){x, y};
      if (diff > max_diff) max_diff = diff, tr = (struct gs_point){x, y};
    }
  }
  c[0] = tl, c[1] = tr, c[2] = br, c[3] = bl;
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

GS_API unsigned gs_fast(struct gs_image img, struct gs_image scoremap, struct gs_keypoint *kps,
                        unsigned nkps, unsigned threshold) {
  gs_assert(gs_valid(img) && kps && nkps > 0);
  static const int dx[16] = {0, 1, 2, 3, 3, 3, 2, 1, 0, -1, -2, -3, -3, -3, -2, -1};
  static const int dy[16] = {-3, -3, -2, -1, 0, 1, 2, 3, 3, 3, 2, 1, 0, -1, -2, -3};
  unsigned n = 0;
  // first pass: compute score map
  for (unsigned y = 3; y < img.h - 3; y++) {
    for (unsigned x = 3; x < img.w - 3; x++) {
      uint8_t p = img.data[y * img.w + x];
      int run = 0, score = 0;
      for (int i = 0; i < 16 + 9; i++) {
        int idx = (i % 16);
        uint8_t v = img.data[(y + dy[idx]) * img.w + (x + dx[idx])];
        if (v > p + threshold) {
          run = (run > 0) ? run + 1 : 1;
        } else if (v < p - threshold) {
          run = (run < 0) ? run - 1 : -1;
        } else {
          run = 0;
        }
        if (run >= 9 || run <= -9) {
          score = 255;
          for (int j = 0; j < 16; j++) {
            int d = gs_get(img, x + dx[j], y + dy[j]) - p;
            if (d < 0) d = -d;
            if (d < score) score = d;
          }
          break;
        }
      }
      scoremap.data[y * img.w + x] = score;
    }
  }
  // second pass: non-maximum suppression
  for (unsigned y = 3; y < img.h - 3; y++) {
    for (unsigned x = 3; x < img.w - 3; x++) {
      int s = scoremap.data[y * img.w + x], is_max = 1;
      if (s == 0) continue;
      for (int yy = -1; yy <= 1 && is_max; yy++) {
        for (int xx = -1; xx <= 1; xx++) {
          if (xx == 0 && yy == 0) continue;
          if (scoremap.data[(y + yy) * img.w + (x + xx)] > s) {
            is_max = 0;
            break;
          }
        }
      }
      if (is_max && n < nkps) kps[n++] = (struct gs_keypoint){{x, y}, (unsigned)s, 0, {0}};
    }
  }
  return n;
}

//
// ORB (Oriented FAST and Rotated BRIEF)
//

// clang-format: off
static const int gs_brief_pattern[256][4] = {
    {1, 0, 1, 3},       {0, 0, 3, 2},       {-1, 1, -1, -1},     {0, -4, -3, -1},
    {-2, 1, -2, -3},    {3, 0, 0, -3},      {-1, 0, -2, 1},      {-1, -1, -1, 4},
    {0, -2, 2, -2},     {0, -4, -3, 0},     {1, 0, 0, -1},       {-3, -1, -1, 2},
    {1, -4, 1, -1},     {-1, 1, 2, 2},      {-2, -1, 1, 2},      {-1, 0, -2, -2},
    {2, 3, 0, 2},       {1, -1, 1, 3},      {0, 3, -5, 2},       {0, -1, 0, -4},
    {0, 1, 3, -1},      {-2, -1, 2, 1},     {-1, 1, 0, 2},       {-1, -1, -1, -3},
    {1, 1, 0, 0},       {-3, -1, -1, -2},   {0, 1, 4, 0},        {1, 0, -4, 0},
    {0, 5, 0, 1},       {0, -2, 2, 2},      {2, -2, 3, -3},      {1, 4, -2, -1},
    {0, -1, -3, 0},     {-2, 1, -2, 3},     {-2, -1, 2, -2},     {0, 3, -3, 0},
    {1, 2, -2, -3},     {1, 1, 1, 1},       {-1, 0, 1, -1},      {4, 1, -2, 1},
    {-2, 2, 2, -2},     {2, 1, 2, 4},       {0, -2, -2, -2},     {0, 1, 1, 2},
    {0, 3, -1, 5},      {1, -2, -2, 1},     {0, 1, 1, 0},        {-2, -3, -1, 2},
    {0, -2, 0, 1},      {-2, 0, 0, -2},     {1, 1, 2, 2},        {-3, -2, 1, 1},
    {1, 8, 1, 2},       {2, 1, -1, 2},      {-2, 0, -1, 0},      {5, -4, 1, -3},
    {-1, 2, 0, -2},     {-1, 1, -1, 0},     {0, -1, 4, 1},       {-4, 0, -1, 2},
    {-2, 0, 1, 2},      {-2, -1, -1, -1},   {4, 1, -3, 2},       {4, 2, -3, -1},
    {3, -1, 1, 2},      {-2, 0, -6, -2},    {-1, -2, 3, -3},     {-1, 0, 3, -3},
    {2, 0, -2, 1},      {0, -1, 0, -1},     {0, 1, 3, -2},       {4, -4, 0, 1},
    {1, -1, 0, -1},     {-1, 2, 1, -1},     {2, 1, 2, 1},        {-2, -1, 1, 1},
    {0, 0, 3, -1},      {1, 0, 0, 2},       {2, 2, 3, 0},        {1, -1, 1, 0},
    {0, 1, -2, 4},      {-2, -2, 2, 2},     {1, 1, 0, -2},       {0, -1, 2, 0},
    {-2, -1, 1, -1},    {-2, 0, 0, -1},     {-1, 0, -3, -3},     {-1, 0, 1, 3},
    {2, 0, 0, -2},      {0, -1, 1, -2},     {1, 3, 0, 1},        {1, -1, 0, 0},
    {0, -2, 0, 1},      {3, 2, 4, -2},      {2, 0, 4, -2},       {-2, -1, -4, -1},
    {-2, 0, 1, 4},      {2, -1, -2, 1},     {-3, 4, 2, -1},      {-3, 3, 0, 2},
    {-3, -1, 0, 0},     {-1, 1, -2, 0},     {0, 1, 1, -2},       {-3, 3, 1, -1},
    {3, 0, 2, 0},       {4, 4, 0, 2},       {1, 3, -2, 1},       {2, -4, -2, -4},
    {-1, 1, 3, 0},      {3, -3, -3, 0},     {1, 0, -4, 0},       {-3, 1, 1, -2},
    {-1, -2, 0, 2},     {-2, 1, -1, -2},    {0, -2, -1, -2},     {4, 0, -1, 0},
    {0, 0, 1, 2},       {-1, -1, -1, -5},   {-3, 3, 3, 0},       {1, 1, 6, 2},
    {0, -2, -3, 0},     {-2, -3, -1, -2},   {3, 2, 0, 3},        {0, -2, 3, 1},
    {-2, 0, -2, -3},    {2, 4, -3, 1},      {-1, -1, -1, -2},    {0, -2, 1, 0},
    {15, -10, -14, 4},  {12, -5, -12, -1},  {-10, 6, 1, 14},     {8, -10, 3, 14},
    {9, -14, -1, -5},   {-8, 10, 3, -3},    {-4, -11, -10, 10},  {6, -12, 3, 4},
    {-15, 4, 1, -4},    {-1, -15, 10, -2},  {-10, -11, 14, -5},  {15, -12, -3, -5},
    {-13, -15, -10, 2}, {8, -6, -11, 7},    {-6, -4, -14, -3},   {-8, -14, 4, -15},
    {15, -11, -7, 1},   {-7, -5, -1, 8},    {-10, 7, -13, 14},   {15, 1, -11, 14},
    {12, -4, 2, -2},    {5, 8, -5, -7},     {-14, -4, -13, -13}, {-15, -8, 6, 12},
    {13, -8, -5, -7},   {-11, -2, 12, 14},  {-13, 5, -11, -11},  {3, 11, -2, 10},
    {14, -12, 9, -3},   {-6, 9, 2, -8},     {-8, -9, -8, -2},    {3, 13, -10, -15},
    {7, 15, -1, -15},   {9, 1, -15, -1},    {7, -14, -2, 5},     {-8, -8, 3, -9},
    {3, -10, -10, -13}, {-9, 3, -8, -6},    {4, -1, -1, 13},     {-15, 4, 14, -9},
    {11, -12, 13, -10}, {9, -15, 13, -11},  {11, 7, -15, 14},    {-12, 6, -14, -6},
    {-11, 11, -6, -15}, {6, -10, -3, 15},   {-1, -12, -3, 8},    {4, 8, -1, 13},
    {-8, -11, 13, -1},  {-12, -4, -3, -14}, {11, 15, 3, 3},      {-12, -12, 10, -5},
    {11, -11, 4, -5},   {14, -6, -8, -10},  {-10, -8, 7, -1},    {10, -2, -5, -4},
    {10, -3, -8, 14},   {2, 9, -15, -1},    {-8, 12, -5, -4},    {-4, -12, 0, -12},
    {-11, 8, -11, -8},  {15, -6, 1, 12},    {15, 10, -7, 6},     {3, 13, -2, -8},
    {11, -7, 0, 3},     {1, 3, -6, 11},     {1, 5, -7, 7},       {3, 11, -10, -7},
    {-2, 1, 12, -6},    {-7, 1, -12, -7},   {1, -1, -4, -2},     {3, 1, 1, -5},
    {1, 5, -4, 0},      {-14, 4, 6, -7},    {3, 8, -2, 5},       {-6, 3, -7, 10},
    {-5, -5, 3, -5},    {-3, 9, -11, -2},   {-8, 1, 1, -8},      {-1, 2, 0, -2},
    {4, -3, 3, -8},     {8, -12, -11, 7},   {0, 9, -4, 0},       {-5, 8, 7, -6},
    {-2, -9, 12, -1},   {3, -9, 14, -5},    {-2, 2, 5, 3},       {-1, -10, 9, 9},
    {-8, -10, 9, -6},   {-5, 8, -8, 10},    {1, -1, 1, -6},      {4, -5, 4, -1},
    {9, 8, 9, -1},      {3, 7, -8, -1},     {-4, -11, 1, 7},     {-9, 5, 2, -2},
    {-4, -10, -12, -2}, {-12, 0, -2, 1},    {-1, -8, 2, 2},      {0, 5, 0, 11},
    {-10, 0, 5, -8},    {1, -7, -4, 5},     {6, 13, 0, -2},      {1, -2, 6, -4},
    {-9, -7, -11, 9},   {9, 11, -1, 8},     {4, 7, 7, -11},      {8, 12, -10, 2},
    {-3, 5, -2, -7},    {-9, 2, 2, 1},      {1, 0, 1, 1},        {2, -5, 4, -14},
    {-11, -1, 2, -1},   {-7, -9, -2, -11},  {10, -1, -8, -11},   {10, 3, 10, 3},
    {9, 0, -9, 1},      {4, 4, 4, 11},      {-2, 1, 0, -12},     {-2, 0, -5, -7},
    {-7, 8, -9, 1},     {-13, -3, -6, 4},   {3, -9, -4, -7},     {-11, -1, 5, -5},
    {-7, 2, 15, 0},     {-3, 2, 13, 6},     {1, 0, 2, 1},        {-7, -4, -4, 3}};
// clang-format: on

GS_API float gs_compute_orientation(struct gs_image img, unsigned x, unsigned y, unsigned r) {
  gs_assert(gs_valid(img) && x >= r && y >= r && x < img.w - r && y < img.h - r);
  float m01 = 0, m10 = 0;
  for (int dy = -(int)r; dy <= (int)r; dy++) {
    for (int dx = -(int)r; dx <= (int)r; dx++) {
      if (dx * dx + dy * dy <= (int)(r * r)) {
        uint8_t intensity = img.data[(y + dy) * img.w + (x + dx)];
        m01 += dy * intensity;
        m10 += dx * intensity;
      }
    }
  }
  return gs_atan2(m01, m10);
}

GS_API void gs_brief_descriptor(struct gs_image img, struct gs_keypoint *kp) {
  gs_assert(gs_valid(img) && kp);
  int x = kp->pt.x, y = kp->pt.y;
  float angle = kp->angle, sin_a = gs_sin(angle), cos_a = gs_sin((float)(angle + 1.57079f));
  for (int i = 0; i < 8; i++) kp->descriptor[i] = 0;
  for (int i = 0; i < 256; i++) {
    float dx1 = gs_brief_pattern[i][0] * cos_a - gs_brief_pattern[i][1] * sin_a;
    float dy1 = gs_brief_pattern[i][0] * sin_a + gs_brief_pattern[i][1] * cos_a;
    float dx2 = gs_brief_pattern[i][2] * cos_a - gs_brief_pattern[i][3] * sin_a;
    float dy2 = gs_brief_pattern[i][2] * sin_a + gs_brief_pattern[i][3] * cos_a;
    int x1 = x + (int)dx1, y1 = y + (int)dy1, x2 = x + (int)dx2, y2 = y + (int)dy2;
    uint8_t intensity1 = gs_get(img, x1, y1), intensity2 = gs_get(img, x2, y2);
    if (intensity1 > intensity2) kp->descriptor[i / 32] |= (1U << (i % 32));
  }
}

static void gs_sort_keypoints(struct gs_keypoint *kps, unsigned n) {
  for (unsigned i = 0; i < n - 1; i++) {
    for (unsigned j = 0; j < n - 1 - i; j++) {
      if (kps[j].response < kps[j + 1].response) {
        struct gs_keypoint temp = kps[j];
        kps[j] = kps[j + 1];
        kps[j + 1] = temp;
      }
    }
  }
}

GS_API unsigned gs_orb_extract(struct gs_image img, struct gs_keypoint *kps, unsigned nkps,
                               unsigned threshold, uint8_t *scoremap_buffer) {
  gs_assert(gs_valid(img) && kps && nkps > 0 && scoremap_buffer);
  struct gs_image scoremap = {img.w, img.h, scoremap_buffer};
  static struct gs_keypoint candidates[5000];
  unsigned n_fast = gs_fast(img, scoremap, candidates, GS_MIN(nkps * 4, 5000), threshold);
  if (n_fast > 1) gs_sort_keypoints(candidates, n_fast);
  unsigned n_orb = 0, radius = 15;
  for (unsigned i = 0; i < n_fast && n_orb < nkps; i++) {
    unsigned x = candidates[i].pt.x, y = candidates[i].pt.y;
    if (x >= radius && y >= radius && x < img.w - radius && y < img.h - radius) {
      kps[n_orb] = candidates[i];
      kps[n_orb].angle = gs_compute_orientation(img, x, y, radius);
      gs_brief_descriptor(img, &kps[n_orb]);
      n_orb++;
    }
  }
  return n_orb;
}

static inline unsigned gs_hamming_distance(const uint32_t desc1[8], const uint32_t desc2[8]) {
  unsigned dist = 0;
  for (int i = 0; i < 8; i++) {
    uint32_t xor = desc1[i] ^ desc2[i];
    while (xor) dist += xor&1, xor>>= 1;
  }
  return dist;
}

GS_API unsigned gs_match_orb(const struct gs_keypoint *kps1, unsigned n1,
                             const struct gs_keypoint *kps2, unsigned n2, struct gs_match *matches,
                             unsigned max_matches, float max_distance) {
  gs_assert(kps1 && kps2 && matches);
  unsigned n = 0;
  for (unsigned i = 0; i < n1 && n < max_matches; i++) {
    float best_dist = max_distance + 1, second_best = max_distance + 1;
    unsigned best_idx = 0;
    for (unsigned j = 0; j < n2; j++) {
      float d = gs_hamming_distance(kps1[i].descriptor, kps2[j].descriptor);
      if (d < best_dist)
        second_best = best_dist, best_dist = d, best_idx = j;
      else if (d < second_best)
        second_best = d;
    }
    if (best_dist <= max_distance && best_dist < 0.8f * second_best)
      matches[n++] = (struct gs_match){i, best_idx, (unsigned)best_dist};
  }
  return n;
}
#endif  // GRAYSKULL_H
