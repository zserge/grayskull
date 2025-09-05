#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grayskull.h"

#define BLUR_RADIUS 1
#define ADAPTIVE_SIZE 15
#define ADAPTIVE_CONSTANT 5.0f
#define OUTPUT_WIDTH 595   // A4 width at 72dpi
#define OUTPUT_HEIGHT 842  // A4 height at 72dpi
#define MORPH_ITERATIONS 3
#define DEBUG 0

int main(int argc, char* argv[]) {
  if (argc != 3) {
    printf("USAGE: %s <input.pgm> <output.pgm>\n", argv[0]);
    return 1;
  }

  struct gs_image img = gs_read_pgm(argv[1]);
  if (!gs_valid(img)) {
    fprintf(stderr, "Error: Could not load %s\n", argv[1]);
    return 1;
  }
  printf("Loaded %ux%u image\n", img.w, img.h);

  printf("Step 1: Blur...\n");
  struct gs_image blurred = gs_alloc(img.w, img.h);
  gs_blur(blurred, img, BLUR_RADIUS);

#if DEBUG
  gs_write_pgm(blurred, "debug_01_blurred.pgm");
#endif

  printf("Step 2: Edge detection (Sobel)...\n");
  struct gs_image edges = gs_alloc(img.w, img.h);
  gs_sobel(edges, blurred);

#if DEBUG
  gs_write_pgm(edges, "debug_02_edges.pgm");
#endif

  printf("Step 3: Multi-strategy thresholding...\n");
  struct gs_image binary_edges = gs_alloc(img.w, img.h);
  struct gs_image binary_doc = gs_alloc(img.w, img.h);

  // Strategy 1: Traditional edge-based approach
  uint8_t otsu_thresh = gs_otsu_theshold(edges);
  gs_copy(binary_edges, edges);
  gs_threshold(binary_edges, otsu_thresh);

  // Strategy 2: Document segmentation approach
  // Apply adaptive threshold to original image to separate document from background
  gs_adaptive_threshold(binary_doc, blurred, 21, 15);

  // Find largest component in document segmentation
  struct gs_image labels = gs_alloc(img.w, img.h);
  struct gs_component components[256];
  gs_label table[256];
  unsigned count =
      gs_connected_components(binary_doc, (gs_label*)labels.data, components, 256, table, 256, 0);

  // If we found a large document region, use its bounding box
  unsigned max_area = 0;
  struct gs_rect doc_rect = {0, 0, 0, 0};
  for (unsigned i = 0; i < count; i++) {
    if (components[i].area > max_area) {
      max_area = components[i].area;
      doc_rect = components[i].box;
    }
  }

  // If document region is significant (>10% of image), use it
  if (max_area > (img.w * img.h) / 10) {
    printf("  Found document region: %dx%d at (%d,%d)\n", doc_rect.w, doc_rect.h, doc_rect.x,
           doc_rect.y);
    // Create binary edges from document boundary
    gs_for(binary_edges, x, y) binary_edges.data[y * binary_edges.w + x] = 0;
    // Draw document boundary
    for (unsigned x = doc_rect.x; x < doc_rect.x + doc_rect.w; x++) {
      if (doc_rect.y < binary_edges.h) binary_edges.data[doc_rect.y * binary_edges.w + x] = 255;
      if (doc_rect.y + doc_rect.h - 1 < binary_edges.h)
        binary_edges.data[(doc_rect.y + doc_rect.h - 1) * binary_edges.w + x] = 255;
    }
    for (unsigned y = doc_rect.y; y < doc_rect.y + doc_rect.h; y++) {
      if (doc_rect.x < binary_edges.w) binary_edges.data[y * binary_edges.w + doc_rect.x] = 255;
      if (doc_rect.x + doc_rect.w - 1 < binary_edges.w)
        binary_edges.data[y * binary_edges.w + (doc_rect.x + doc_rect.w - 1)] = 255;
    }
  } else {
    printf("  Using edge-based approach (threshold=%d)\n", otsu_thresh);
  }

  gs_free(binary_doc);
  gs_free(labels);

#if DEBUG
  gs_write_pgm(binary_edges, "debug_03_binary_edges.pgm");
#endif

  printf("Step 4: Closing edges...\n");
  struct gs_image temp = gs_alloc(img.w, img.h);
  gs_copy(temp, binary_edges);
  for (int i = 0; i < MORPH_ITERATIONS; i++) {
    gs_dilate(binary_edges, temp);
    gs_copy(temp, binary_edges);
  }
  for (int i = 0; i < MORPH_ITERATIONS; i++) {
    gs_erode(binary_edges, temp);
    gs_copy(temp, binary_edges);
  }

#if DEBUG
  gs_write_pgm(binary_edges, "debug_04_closed_edges.pgm");
#endif

  printf("Step 5: Finding corners...\n");
  struct gs_quad document_quad = gs_find_document_corners(binary_edges);

  int valid_corners = 1;
  for (int i = 1; i < 4; i++) {
    if (document_quad.corners[i].x == document_quad.corners[0].x &&
        document_quad.corners[i].y == document_quad.corners[0].y) {
      valid_corners = 0;
      break;
    }
  }

  if (!valid_corners) {
    printf("Warning: Using full image\n");
    struct gs_rect full_rect = {0, 0, img.w, img.h};
    document_quad = gs_rect_to_quad(full_rect);
  }

  printf("Step 6: Perspective correction to %ux%u...\n", OUTPUT_WIDTH, OUTPUT_HEIGHT);
  struct gs_image corrected = gs_alloc(OUTPUT_WIDTH, OUTPUT_HEIGHT);
  gs_perspective_correct(corrected, img, document_quad);

  printf("Saving result...\n");
  if (gs_write_pgm(corrected, argv[2]) != 0) {
    fprintf(stderr, "Error: Could not save %s\n", argv[2]);
    return 1;
  }

  printf("Done! %ux%u -> %ux%u\n", img.w, img.h, OUTPUT_WIDTH, OUTPUT_HEIGHT);

  gs_free(img);
  gs_free(blurred);
  gs_free(edges);
  gs_free(binary_edges);
  gs_free(temp);
  gs_free(corrected);

  return 0;
}
