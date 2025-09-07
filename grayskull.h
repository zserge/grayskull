#ifndef GRAYSKULL_H
#define GRAYSKULL_H

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

struct gs_quad {
  struct gs_point corners[4];  // nw, ne, se, sw
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
#endif  // GS_NO_STDLIB

#define gs_for(img, x, y)                \
  for (unsigned y = 0; y < (img).h; y++) \
    for (unsigned x = 0; x < (img).w; x++)

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
        if (sy >= 0 && sy < (int) src.h && sx >= 0 && sx < (int) src.w) {
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

typedef uint16_t gs_label;
struct gs_component {
  unsigned area;
  struct gs_rect box;
};

static inline gs_label gs_find_root(gs_label x, gs_label *table) {
  if (table[x] != x) table[x] = gs_find_root(table[x], table);
  return table[x];
}

GS_API unsigned gs_connected_components(struct gs_image img, gs_label *labels,
                                        struct gs_component *comp, unsigned comp_size,
                                        gs_label *table, unsigned table_size, int fullconn) {
  gs_assert(gs_valid(img) && labels != NULL);

  gs_for(img, x, y) labels[y * img.w + x] = 0;
  for (unsigned i = 0; i < table_size; i++) table[i] = i;
  unsigned nextlbl = 1;

  gs_for(img, x, y) {
    if (!img.data[y * img.w + x]) continue;
    static const int dx[] = {-1, 0, -1, 1};
    static const int dy[] = {0, -1, -1, -1};
    gs_label neighbors[4];
    int neighbor_count = 0;

    for (int i = 0; i < (fullconn ? 4 : 2); i++) {
      int nx = x + dx[i], ny = y + dy[i];
      if (ny < 0 || ny >= (int)img.h || nx < 0 || nx >= (int)img.w) continue;
      gs_label nl = labels[ny * img.w + nx];
      if (nl) neighbors[neighbor_count++] = nl;
    }

    if (neighbor_count == 0) {
      gs_assert(nextlbl < table_size);
      labels[y * img.w + x] = nextlbl++;
    } else {
      labels[y * img.w + x] = neighbors[0];
      for (int i = 1; i < neighbor_count; i++) {
        gs_label root_a = gs_find_root(neighbors[0], table);
        gs_label root_b = gs_find_root(neighbors[i], table);
        if (root_a != root_b) {
          if (root_a < root_b)
            table[root_b] = root_a;
          else
            table[root_a] = root_b;
        }
      }
    }
  }

  gs_label label_map[table_size];
  for (unsigned i = 0; i < table_size; i++) label_map[i] = 0;

  unsigned comp_count = 0;
  for (unsigned lbl = 1; lbl < nextlbl; lbl++) {
    gs_label root = gs_find_root(lbl, table);
    if (label_map[root] == 0) { label_map[root] = ++comp_count; }
  }

  for (unsigned i = 0; i < comp_count && i < comp_size; i++) {
    comp[i] = (struct gs_component){0, {UINT32_MAX, UINT32_MAX, 0, 0}};
  }

  gs_for(img, x, y) {
    if (!img.data[y * img.w + x]) continue;
    gs_label lbl = labels[y * img.w + x];
    gs_label root = gs_find_root(lbl, table);
    gs_label comp_label = label_map[root];
    labels[y * img.w + x] = comp_label;

    if (comp_label > 0 && (comp_label - 1) < (int) comp_size) {
      unsigned comp_idx = comp_label - 1;
      comp[comp_idx].area++;
      if (x < comp[comp_idx].box.x) comp[comp_idx].box.x = x;
      if (y < comp[comp_idx].box.y) comp[comp_idx].box.y = y;
      if (x > comp[comp_idx].box.w) comp[comp_idx].box.w = x;  // max x, convert to w later
      if (y > comp[comp_idx].box.h) comp[comp_idx].box.h = y;  // max y, convert to h later
    }
  }

  for (unsigned i = 0; i < comp_count && i < comp_size; i++) {
    if (comp[i].area > 0) {
      unsigned min_x = comp[i].box.x;
      unsigned min_y = comp[i].box.y;
      unsigned max_x = comp[i].box.w;
      unsigned max_y = comp[i].box.h;
      comp[i].box.x = min_x;
      comp[i].box.y = min_y;
      comp[i].box.w = max_x - min_x + 1;
      comp[i].box.h = max_y - min_y + 1;
    }
  }

  return comp_count;
}

GS_API struct gs_rect gs_find_largest_contour(struct gs_image binary_img,
                                              struct gs_image labels_buffer) {
  gs_assert(gs_valid(binary_img) && gs_valid(labels_buffer));
  gs_assert(labels_buffer.w == binary_img.w && labels_buffer.h == binary_img.h);

  struct gs_component components[256];
  gs_label table[256];
  unsigned count = gs_connected_components(binary_img, (gs_label *)labels_buffer.data, components,
                                           256, table, 256, 0);

  struct gs_rect largest = {0, 0, 0, 0};
  unsigned max_area = 0;

  for (unsigned i = 0; i < count; i++) {
    if (components[i].area > max_area) {
      max_area = components[i].area;
      largest = components[i].box;
    }
  }

  return largest;
}

// Convert rectangle to quadrilateral for perspective correction
GS_API struct gs_quad gs_rect_to_quad(struct gs_rect rect) {
  struct gs_quad quad;
  quad.corners[0] = (struct gs_point){rect.x, rect.y};
  quad.corners[1] = (struct gs_point){rect.x + rect.w - 1, rect.y};
  quad.corners[2] = (struct gs_point){rect.x + rect.w - 1, rect.y + rect.h - 1};
  quad.corners[3] = (struct gs_point){rect.x, rect.y + rect.h - 1};
  return quad;
}

GS_API struct gs_quad gs_find_document_corners(struct gs_image binary_edges) {
  gs_assert(gs_valid(binary_edges));

  unsigned cx = binary_edges.w / 2;
  unsigned cy = binary_edges.h / 2;

  struct gs_quad quad;
  quad.corners[0] = (struct gs_point){cx, cy};
  quad.corners[1] = (struct gs_point){cx, cy};
  quad.corners[2] = (struct gs_point){cx, cy};
  quad.corners[3] = (struct gs_point){cx, cy};

  gs_for(binary_edges, x, y) {
    if (binary_edges.data[y * binary_edges.w + x] > 128) {
      if (x <= cx && y <= cy) {
        unsigned current_dist = (cx - x) + (cy - y);
        unsigned corner_dist = (cx - quad.corners[0].x) + (cy - quad.corners[0].y);
        if (current_dist > corner_dist) { quad.corners[0] = (struct gs_point){x, y}; }
      }
      if (x >= cx && y <= cy) {
        unsigned current_dist = (x - cx) + (cy - y);
        unsigned corner_dist = (quad.corners[1].x - cx) + (cy - quad.corners[1].y);
        if (current_dist > corner_dist) { quad.corners[1] = (struct gs_point){x, y}; }
      }
      if (x >= cx && y >= cy) {
        unsigned current_dist = (x - cx) + (y - cy);
        unsigned corner_dist = (quad.corners[2].x - cx) + (quad.corners[2].y - cy);
        if (current_dist > corner_dist) { quad.corners[2] = (struct gs_point){x, y}; }
      }
      if (x <= cx && y >= cy) {
        unsigned current_dist = (cx - x) + (y - cy);
        unsigned corner_dist = (cx - quad.corners[3].x) + (quad.corners[3].y - cy);
        if (current_dist > corner_dist) { quad.corners[3] = (struct gs_point){x, y}; }
      }
    }
  }
  return quad;
}

GS_API void gs_perspective_correct(struct gs_image dst, struct gs_image src, struct gs_quad quad) {
  gs_assert(gs_valid(dst) && gs_valid(src));

  float w = dst.w - 1.0f;
  float h = dst.h - 1.0f;

  gs_for(dst, x, y) {
    float u = x / w;
    float v = y / h;

    float p0x = quad.corners[0].x, p0y = quad.corners[0].y;
    float p1x = quad.corners[1].x, p1y = quad.corners[1].y;
    float p2x = quad.corners[2].x, p2y = quad.corners[2].y;
    float p3x = quad.corners[3].x, p3y = quad.corners[3].y;

    float top_x = p0x * (1 - u) + p1x * u;
    float top_y = p0y * (1 - u) + p1y * u;
    float bot_x = p3x * (1 - u) + p2x * u;
    float bot_y = p3y * (1 - u) + p2y * u;

    float src_x = top_x * (1 - v) + bot_x * v;
    float src_y = top_y * (1 - v) + bot_y * v;

    src_x = GS_MAX(0.0f, GS_MIN(src_x, src.w - 1.0f));
    src_y = GS_MAX(0.0f, GS_MIN(src_y, src.h - 1.0f));

    unsigned sx_int = (unsigned)src_x, sy_int = (unsigned)src_y;
    unsigned sx1 = GS_MIN(sx_int + 1, src.w - 1), sy1 = GS_MIN(sy_int + 1, src.h - 1);
    float dx = src_x - sx_int, dy = src_y - sy_int;

    uint8_t c00 = src.data[sy_int * src.w + sx_int];
    uint8_t c01 = src.data[sy_int * src.w + sx1];
    uint8_t c10 = src.data[sy1 * src.w + sx_int];
    uint8_t c11 = src.data[sy1 * src.w + sx1];

    dst.data[y * dst.w + x] = (uint8_t)((c00 * (1 - dx) * (1 - dy)) + (c01 * dx * (1 - dy)) +
                                        (c10 * (1 - dx) * dy) + (c11 * dx * dy));
  }
}
#endif  // GRAYSKULL_H
