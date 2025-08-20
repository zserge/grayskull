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

struct gs_quad {
  struct gs_point corners[4];  // Top-left, top-right, bottom-right, bottom-left
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

// Simple contour detection - finds the largest connected component bounding box
GS_API struct gs_rect gs_find_largest_contour(struct gs_image binary_img) {
  gs_assert(gs_valid(binary_img));

  // Find bounding box of all white pixels
  unsigned min_x = binary_img.w, max_x = 0;
  unsigned min_y = binary_img.h, max_y = 0;
  int found_pixels = 0;

  gs_for(binary_img, x, y) {
    if (binary_img.data[y * binary_img.w + x] > 128) {  // White pixel
      if (x < min_x) min_x = x;
      if (x > max_x) max_x = x;
      if (y < min_y) min_y = y;
      if (y > max_y) max_y = y;
      found_pixels = 1;
    }
  }

  if (!found_pixels) return (struct gs_rect){0, 0, 0, 0};

  return (struct gs_rect){min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
}

// Convert rectangle to quadrilateral for perspective correction
GS_API struct gs_quad gs_rect_to_quad(struct gs_rect rect) {
  struct gs_quad quad;
  quad.corners[0] = (struct gs_point){rect.x, rect.y};                            // Top-left
  quad.corners[1] = (struct gs_point){rect.x + rect.w - 1, rect.y};               // Top-right
  quad.corners[2] = (struct gs_point){rect.x + rect.w - 1, rect.y + rect.h - 1};  // Bottom-right
  quad.corners[3] = (struct gs_point){rect.x, rect.y + rect.h - 1};               // Bottom-left
  return quad;
}

// Find document corners by detecting the outermost edge pixels in each quadrant
GS_API struct gs_quad gs_find_document_corners(struct gs_image binary_edges) {
  gs_assert(gs_valid(binary_edges));

  // Get image center
  unsigned cx = binary_edges.w / 2;
  unsigned cy = binary_edges.h / 2;

  // Initialize corners to center (fallback)
  struct gs_quad quad;
  quad.corners[0] = (struct gs_point){cx, cy};  // Top-left
  quad.corners[1] = (struct gs_point){cx, cy};  // Top-right
  quad.corners[2] = (struct gs_point){cx, cy};  // Bottom-right
  quad.corners[3] = (struct gs_point){cx, cy};  // Bottom-left

  // Find the outermost white pixels in each quadrant
  gs_for(binary_edges, x, y) {
    if (binary_edges.data[y * binary_edges.w + x] > 128) {  // White edge pixel

      // Top-left quadrant: maximize distance from center
      if (x <= cx && y <= cy) {
        unsigned current_dist = (cx - x) + (cy - y);
        unsigned corner_dist = (cx - quad.corners[0].x) + (cy - quad.corners[0].y);
        if (current_dist > corner_dist) { quad.corners[0] = (struct gs_point){x, y}; }
      }

      // Top-right quadrant: maximize distance from center
      if (x >= cx && y <= cy) {
        unsigned current_dist = (x - cx) + (cy - y);
        unsigned corner_dist = (quad.corners[1].x - cx) + (cy - quad.corners[1].y);
        if (current_dist > corner_dist) { quad.corners[1] = (struct gs_point){x, y}; }
      }

      // Bottom-right quadrant: maximize distance from center
      if (x >= cx && y >= cy) {
        unsigned current_dist = (x - cx) + (y - cy);
        unsigned corner_dist = (quad.corners[2].x - cx) + (quad.corners[2].y - cy);
        if (current_dist > corner_dist) { quad.corners[2] = (struct gs_point){x, y}; }
      }

      // Bottom-left quadrant: maximize distance from center
      if (x <= cx && y >= cy) {
        unsigned current_dist = (cx - x) + (y - cy);
        unsigned corner_dist = (cx - quad.corners[3].x) + (quad.corners[3].y - cy);
        if (current_dist > corner_dist) { quad.corners[3] = (struct gs_point){x, y}; }
      }
    }
  }

  return quad;
}

// Perspective correction using bilinear transformation
GS_API void gs_perspective_correct(struct gs_image dst, struct gs_image src, struct gs_quad quad) {
  gs_assert(gs_valid(dst) && gs_valid(src));

  // Calculate transformation from dst coordinates to src quad coordinates
  float w = dst.w - 1.0f;
  float h = dst.h - 1.0f;

  gs_for(dst, x, y) {
    // Normalize destination coordinates to [0,1]
    float u = x / w;
    float v = y / h;

    // Convert integer points to float for calculations
    float p0x = quad.corners[0].x, p0y = quad.corners[0].y;  // Top-left
    float p1x = quad.corners[1].x, p1y = quad.corners[1].y;  // Top-right
    float p2x = quad.corners[2].x, p2y = quad.corners[2].y;  // Bottom-right
    float p3x = quad.corners[3].x, p3y = quad.corners[3].y;  // Bottom-left

    // Bilinear interpolation within the quad
    float top_x = p0x * (1 - u) + p1x * u;
    float top_y = p0y * (1 - u) + p1y * u;
    float bot_x = p3x * (1 - u) + p2x * u;
    float bot_y = p3y * (1 - u) + p2y * u;

    float src_x = top_x * (1 - v) + bot_x * v;
    float src_y = top_y * (1 - v) + bot_y * v;

    // Clamp to source image bounds
    src_x = GS_MAX(0.0f, GS_MIN(src_x, src.w - 1.0f));
    src_y = GS_MAX(0.0f, GS_MIN(src_y, src.h - 1.0f));

    // Bilinear sampling from source
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
