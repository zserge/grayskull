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

  printf("Step 3: Thresholding edges (Otsu)...\n");
  uint8_t edge_threshold = gs_otsu_theshold(edges);
  struct gs_image binary_edges = gs_alloc(img.w, img.h);
  gs_copy(binary_edges, edges);
  gs_threshold(binary_edges, edge_threshold);

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
