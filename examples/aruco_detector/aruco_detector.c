#include <math.h>
#include <string.h>
#include <time.h>

#include "../../grayskull.h"

// ArUco 4x4 dictionary (simplified - just a few markers)
// Each marker is a 4x4 bit pattern (16 bits total)
static const uint16_t aruco_4x4_dict[] = {
    0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080, 0x0100, 0x0200, 0x0400, 0x0800,
    0x1000, 0x2000, 0x4000, 0x8000, 0x0003, 0x0005, 0x0009, 0x0011, 0x0021, 0x0041, 0x0081, 0x0101};
#define ARUCO_DICT_SIZE (sizeof(aruco_4x4_dict) / sizeof(aruco_4x4_dict[0]))

// Configuration
#define MIN_MARKER_RATIO 0.02f  // Minimum marker size as fraction of image diagonal (2%)
#define MAX_MARKER_RATIO 0.30f  // Maximum marker size as fraction of image diagonal (30%)
#define MARKER_RESOLUTION 8     // Extract 8x8 pixels for 4x4 + border analysis

// Calculate adaptive size limits based on image dimensions
static void get_marker_size_limits(unsigned img_w, unsigned img_h, unsigned *min_size,
                                   unsigned *max_size) {
  // Use image diagonal as reference
  float diagonal = sqrtf(img_w * img_w + img_h * img_h);
  *min_size = (unsigned)(diagonal * MIN_MARKER_RATIO);
  *max_size = (unsigned)(diagonal * MAX_MARKER_RATIO);

  // Ensure minimum reasonable limits
  if (*min_size < 10) *min_size = 10;
  if (*max_size < *min_size * 2) *max_size = *min_size * 2;
}

// Extract marker pattern from a square region
static uint16_t extract_marker_pattern(struct gs_image img, struct gs_rect roi, unsigned min_size,
                                       unsigned max_size) {
  if (roi.w < min_size || roi.h < min_size || roi.w > max_size || roi.h > max_size) {
    return 0xFFFF;  // Invalid
  }

  // Create normalized 8x8 extraction of the marker region
  struct gs_image marker = gs_alloc(MARKER_RESOLUTION, MARKER_RESOLUTION);
  if (!gs_valid(marker)) return 0xFFFF;

  // Extract and resize to 8x8
  struct gs_image roi_img = gs_alloc(roi.w, roi.h);
  if (!gs_valid(roi_img)) {
    gs_free(marker);
    return 0xFFFF;
  }

  gs_crop(roi_img, img, roi);
  gs_resize(marker, roi_img);
  gs_free(roi_img);

  // Threshold the 8x8 marker
  uint8_t threshold = gs_otsu_theshold(marker);
  gs_threshold(marker, threshold);

  // Extract 4x4 inner pattern (skip 2-pixel border)
  uint16_t pattern = 0;
  for (unsigned y = 2; y < 6; y++) {
    for (unsigned x = 2; x < 6; x++) {
      if (marker.data[y * MARKER_RESOLUTION + x] > 128) {
        unsigned bit_index = (y - 2) * 4 + (x - 2);
        pattern |= (1 << bit_index);
      }
    }
  }

  gs_free(marker);
  return pattern;
}

// Check if a pattern matches any ArUco marker (with rotation)
static int match_aruco_pattern(uint16_t pattern, unsigned *marker_id, unsigned *rotation) {
  for (unsigned id = 0; id < ARUCO_DICT_SIZE; id++) {
    uint16_t dict_pattern = aruco_4x4_dict[id];

    // Try all 4 rotations
    for (unsigned rot = 0; rot < 4; rot++) {
      if (pattern == dict_pattern) {
        *marker_id = id;
        *rotation = rot;
        return 1;
      }

      // Rotate pattern 90 degrees clockwise
      uint16_t rotated = 0;
      for (unsigned i = 0; i < 16; i++) {
        unsigned src_row = i / 4;
        unsigned src_col = i % 4;
        unsigned dst_row = src_col;
        unsigned dst_col = 3 - src_row;
        unsigned dst_bit = dst_row * 4 + dst_col;

        if (dict_pattern & (1 << i)) { rotated |= (1 << dst_bit); }
      }
      dict_pattern = rotated;
    }
  }
  return 0;
}

// Check if a bounding box is roughly square
static int is_roughly_square(struct gs_rect box, float tolerance) {
  if (box.w == 0 || box.h == 0) return 0;
  float aspect_ratio = (float)box.w / box.h;
  return (aspect_ratio >= (1.0f - tolerance) && aspect_ratio <= (1.0f + tolerance));
}

// Write a simple PPM (color) image for visualization
static void write_ppm_colored_components(const char *filename, struct gs_image img,
                                         gs_label *labels, unsigned num_components, unsigned w,
                                         unsigned h) {
  FILE *f = fopen(filename, "wb");
  if (!f) return;

  // PPM header
  fprintf(f, "P6\n%u %u\n255\n", w, h);

  // Generate colors for each component (better pseudo-random coloring)
  uint8_t colors[1024][3];  // RGB colors for up to 1024 components

  // Simple linear congruential generator for pseudo-random colors
  unsigned seed = time(NULL);

  for (unsigned i = 0; i < 1024; i++) {
    if (i == 0) {
      colors[i][0] = colors[i][1] = colors[i][2] = 0;  // Black background
    } else {
      // Generate pseudo-random values using different multipliers for R, G, B
      seed = (seed * 1103515245 + 12345) & 0x7fffffff;
      colors[i][0] = (uint8_t)(80 + (seed % 176));  // Red: 80-255

      seed = (seed * 1664525 + 1013904223) & 0x7fffffff;
      colors[i][1] = (uint8_t)(80 + (seed % 176));  // Green: 80-255

      seed = (seed * 214013 + 2531011) & 0x7fffffff;
      colors[i][2] = (uint8_t)(80 + (seed % 176));  // Blue: 80-255

      // Ensure we don't get too dark colors by adding the component index influence
      colors[i][0] = (colors[i][0] + i * 37) % 176 + 80;
      colors[i][1] = (colors[i][1] + i * 71) % 176 + 80;
      colors[i][2] = (colors[i][2] + i * 113) % 176 + 80;
    }
  }

  // Write pixel data
  for (unsigned y = 0; y < h; y++) {
    for (unsigned x = 0; x < w; x++) {
      unsigned label = labels[y * w + x];
      if (label < 1024) {
        fwrite(colors[label], 1, 3, f);
      } else {
        uint8_t white[3] = {255, 255, 255};
        fwrite(white, 1, 3, f);
      }
    }
  }

  fclose(f);
}

// Create debug SVG showing detected components
static void create_debug_svg(struct gs_image img, struct gs_component *components,
                             unsigned num_components, const char *filename) {
  FILE *f = fopen(filename, "w");
  if (!f) return;

  fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
  fprintf(f, "<svg width=\"%u\" height=\"%u\" xmlns=\"http://www.w3.org/2000/svg\">\n", img.w,
          img.h);
  fprintf(f, "  <style>\n");
  fprintf(f, "    .component { fill: none; stroke-width: 2; opacity: 0.8; }\n");
  fprintf(f, "    .text { font-family: Arial; font-size: 12px; fill: red; }\n");
  fprintf(f, "  </style>\n");

  // Draw simplified background
  fprintf(f, "  <rect width=\"%u\" height=\"%u\" fill=\"white\"/>\n", img.w, img.h);
  for (unsigned y = 0; y < img.h; y += 8) {
    for (unsigned x = 0; x < img.w; x += 8) {
      unsigned val = img.data[y * img.w + x];
      if (val < 200) {
        unsigned gray = 255 - val;
        fprintf(f,
                "    <rect x=\"%u\" y=\"%u\" width=\"8\" height=\"8\" fill=\"rgb(%u,%u,%u)\" "
                "opacity=\"0.3\"/>\n",
                x, y, gray, gray, gray);
      }
    }
  }

  // Draw components
  for (unsigned i = 0; i < num_components; i++) {
    struct gs_component *comp = &components[i];
    const char *color = is_roughly_square(comp->box, 0.15f) ? "lime" : "orange";

    fprintf(f,
            "  <rect x=\"%u\" y=\"%u\" width=\"%u\" height=\"%u\" class=\"component\" "
            "stroke=\"%s\"/>\n",
            comp->box.x, comp->box.y, comp->box.w, comp->box.h, color);

    fprintf(f, "  <text x=\"%u\" y=\"%u\" class=\"text\">%u</text>\n", comp->box.x + 2,
            comp->box.y + 14, i + 1);

    fprintf(f, "  <text x=\"%u\" y=\"%u\" class=\"text\" font-size=\"10px\">%ux%u</text>\n",
            comp->box.x + 2, comp->box.y + comp->box.h - 2, comp->box.w, comp->box.h);
  }

  fprintf(f, "</svg>\n");
  fclose(f);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("Usage: %s <input.pgm>\n", argv[0]);
    printf("Detects ArUco 4x4 markers in PGM images\n");
    return 1;
  }

  // Load image
  struct gs_image img = gs_read_pgm(argv[1]);
  if (!gs_valid(img)) {
    printf("Error: Could not load image %s\n", argv[1]);
    return 1;
  }

  printf("Loaded image: %ux%u\n", img.w, img.h);

  // Calculate adaptive marker size limits
  unsigned min_marker_size, max_marker_size;
  get_marker_size_limits(img.w, img.h, &min_marker_size, &max_marker_size);
  printf("Marker size limits: %u - %u pixels (%.1f%% - %.1f%% of diagonal)\n", min_marker_size,
         max_marker_size, MIN_MARKER_RATIO * 100, MAX_MARKER_RATIO * 100);

  // Preprocessing pipeline
  struct gs_image blurred = gs_alloc(img.w, img.h);
  struct gs_image edges = gs_alloc(img.w, img.h);
  struct gs_image binary = gs_alloc(img.w, img.h);
  struct gs_image marker_binary = {0, 0, NULL};  // Will be allocated later

  if (!gs_valid(blurred) || !gs_valid(edges) || !gs_valid(binary)) {
    printf("Error: Memory allocation failed\n");
    goto cleanup;
  }

  // 1. Blur to reduce noise
  gs_blur(blurred, img, 1);
  printf("Step 1: Blurred image\n");
  gs_write_pgm(blurred, "debug_01_blurred.pgm");

  // 2. Edge detection
  gs_sobel(edges, blurred);
  printf("Step 2: Edge detection\n");
  gs_write_pgm(edges, "debug_02_edges.pgm");

  // 3. Threshold edges
  uint8_t edge_threshold = gs_otsu_theshold(edges);
  gs_copy(binary, edges);
  gs_threshold(binary, edge_threshold);
  printf("Step 3: Binary threshold (t=%u)\n", edge_threshold);
  gs_write_pgm(binary, "debug_03_edges_binary.pgm");

  // 4. Morphological closing to connect broken edges
  struct gs_image temp = gs_alloc(img.w, img.h);
  if (gs_valid(temp)) {
    gs_dilate(temp, binary);
    gs_erode(binary, temp);
    gs_free(temp);
    printf("Step 4: Morphological closing\n");
    gs_write_pgm(binary, "debug_04_closed.pgm");
  }

  // 4b. For ArUco detection, we need to find black marker regions, not white edges
  // Use adaptive threshold with smaller block size and more conservative offset
  marker_binary = gs_alloc(img.w, img.h);
  if (gs_valid(marker_binary)) {
    gs_adaptive_threshold(marker_binary, blurred, 11, 15);  // Smaller block, higher offset
    printf("Step 4b: Adaptive threshold for marker detection\n");
    gs_write_pgm(marker_binary, "debug_04b_marker_binary.pgm");

    // Clean up with morphological operations to separate touching regions
    struct gs_image temp2 = gs_alloc(img.w, img.h);
    if (gs_valid(temp2)) {
      // Erode to separate touching components
      gs_erode(temp2, marker_binary);
      gs_write_pgm(temp2, "debug_04d_eroded.pgm");

      // Then dilate back to restore size
      gs_dilate(marker_binary, temp2);
      gs_free(temp2);
      gs_write_pgm(marker_binary, "debug_04e_cleaned.pgm");
    }

    // Invert so markers (black) become white for connected components
    for (unsigned i = 0; i < marker_binary.w * marker_binary.h; i++) {
      marker_binary.data[i] = 255 - marker_binary.data[i];
    }
    gs_write_pgm(marker_binary, "debug_04c_marker_inverted.pgm");
  }

  // 5. Connected component analysis on marker regions
  gs_label *labels = calloc(1, img.w * img.h * sizeof(gs_label));
  struct gs_component components[1024];  // Increase from 256 to 1024

  // Use marker_binary for connected components if available, otherwise fall back to binary
  struct gs_image *analysis_img = gs_valid(marker_binary) ? &marker_binary : &binary;
  gs_label table[4096] = {0};
  unsigned num_components =
      gs_connected_components(*analysis_img, labels, components, 1024, table, 4096, 0);
  printf("Step 5: Found %u connected components\n", num_components);

  struct gs_image label_1 = gs_alloc(img.w, img.h);
  for (int label_num = 1; label_num <= 10; label_num++) {
    gs_for(img, x, y) {
      label_1.data[y * img.w + x] = (labels[y * img.w + x] == label_num ? 255 : 0);
    }
    char filename[64];
    snprintf(filename, sizeof(filename), "debug_label_%d.pgm", label_num);
    gs_write_pgm(label_1, filename);
  }
  // Create colored visualization of labeled components
  write_ppm_colored_components("debug_05_components_colored.ppm", img, labels, num_components,
                               img.w, img.h);
  printf("Step 5c: Saved colored components visualization to debug_05_components_colored.ppm\n");

  // Debug: Check label distribution
  unsigned label_counts[1024] = {0};
  for (unsigned i = 0; i < img.w * img.h; i++) {
    unsigned label = labels[i];
    if (label < 1024) { label_counts[label]++; }
  }

  printf("Label distribution (first 20 labels):\n");
  for (unsigned i = 0; i < 20 && i < num_components + 1; i++) {
    if (label_counts[i] > 0) { printf("  Label %u: %u pixels\n", i, label_counts[i]); }
  }

  // Create debug visualization
  create_debug_svg(img, components, num_components, "debug_components.svg");
  printf("Step 5b: Created debug_components.svg\n");

  // Print detailed component information
  printf("\nDetailed component analysis:\n");
  for (unsigned i = 0; i < num_components && i < 20; i++) {  // Limit to first 20 for readability
    gs_label label = i;
    struct gs_component *comp = &components[i];
    printf("Component %u: label=%u, area=%u, box=(%u,%u,%ux%u)\n", i + 1, label, comp->area,
           comp->box.x, comp->box.y, comp->box.w, comp->box.h);

    // Check if the component's label appears in the correct number of pixels
    unsigned actual_pixels = 0;
    for (unsigned y = 0; y < img.h; y++) {
      for (unsigned x = 0; x < img.w; x++) {
        if (labels[y * img.w + x] == label) { actual_pixels++; }
      }
    }

    if (actual_pixels != comp->area) {
      printf(
          "  WARNING: Component area mismatch! Component says %u pixels, but found %u pixels with "
          "label %u\n",
          comp->area, actual_pixels, label);
    }

    // Show first few pixels of each component for debugging
    printf("  Sample pixels: ");
    unsigned count = 0;
    for (unsigned y = comp->box.y; y < comp->box.y + comp->box.h && count < 10; y++) {
      for (unsigned x = comp->box.x; x < comp->box.x + comp->box.w && count < 10; x++) {
        // if (labels[y * img.w + x] == comp->label) {
        // printf("(%u,%u) ", x, y);
        // count++;
        // }
      }
    }
    if (comp->area > count) printf("... (%u more)", comp->area - count);
    printf("\n");
  }
  if (num_components > 20) { printf("... and %u more components\n", num_components - 20); }

  // 6. Analyze each component for ArUco markers
  unsigned markers_found = 0;

  printf("\nAnalyzing %u connected components:\n", num_components);
  for (unsigned i = 0; i < num_components; i++) {
    struct gs_component *comp = &components[i];

    printf("Component %u: area=%u, box=(%u,%u,%ux%u)", i + 1, comp->area, comp->box.x, comp->box.y,
           comp->box.w, comp->box.h);

    // Filter by size and aspect ratio
    if (comp->area < min_marker_size * min_marker_size ||
        comp->area > max_marker_size * max_marker_size) {
      printf(" -> REJECTED: area outside range [%u, %u]\n", min_marker_size * min_marker_size,
             max_marker_size * max_marker_size);
      continue;
    }

    // Reject extremely large components that are likely the whole page
    unsigned img_area = img.w * img.h;
    if (comp->area > img_area / 4) {  // Reject if larger than 25% of image
      printf(" -> REJECTED: too large (%.1f%% of image)\n", (float)comp->area * 100 / img_area);
      continue;
    }

    if (!is_roughly_square(comp->box, 0.15f)) {  // 15% tolerance - much stricter
      float aspect = (float)comp->box.w / comp->box.h;
      printf(" -> REJECTED: not square (aspect=%.2f, need 0.85-1.15)\n", aspect);
      continue;
    }

    printf(" -> CANDIDATE");

    // Additional filtering: component should fill a reasonable portion of its bounding box
    float fill_ratio = (float)comp->area / (comp->box.w * comp->box.h);
    if (fill_ratio < 0.3f) {  // Component should fill at least 30% of its bounding box
      printf(" -> REJECTED: low fill ratio (%.2f, need >0.3)\n", fill_ratio);
      continue;
    }

    printf(" -> fill=%.2f", fill_ratio);

    // Extract marker pattern
    uint16_t pattern = extract_marker_pattern(img, comp->box, min_marker_size, max_marker_size);
    if (pattern == 0xFFFF) {
      printf(" -> REJECTED: pattern extraction failed\n");
      continue;
    }

    printf(" -> pattern=0x%04X", pattern);

    // Check if it matches an ArUco marker
    unsigned marker_id, rotation;
    if (match_aruco_pattern(pattern, &marker_id, &rotation)) {
      printf(" -> MATCH: ID=%u, rot=%u\n", marker_id, rotation);
      printf("ArUco marker found!\n");
      printf("  ID: %u\n", marker_id);
      printf("  Rotation: %u (90° steps)\n", rotation);
      printf("  Position: (%u, %u)\n", comp->box.x, comp->box.y);
      printf("  Size: %ux%u\n", comp->box.w, comp->box.h);
      printf("  Area: %u pixels\n", comp->area);
      printf("  Pattern: 0x%04X\n", pattern);
      printf("\n");
      markers_found++;
    } else {
      printf(" -> NO MATCH\n");
    }
  }

  if (markers_found == 0) {
    printf("No ArUco markers detected in image.\n");
    printf("Tips:\n");
    printf("- Ensure markers are clearly visible and well-lit\n");
    printf("- Markers should be roughly square in the image\n");
    printf("- Size should be between %u and %u pixels\n", min_marker_size, max_marker_size);
  } else {
    printf("Total ArUco markers detected: %u\n", markers_found);
  }

cleanup:
  free(labels);
  if (gs_valid(marker_binary)) gs_free(marker_binary);
  gs_free(img);
  gs_free(blurred);
  gs_free(edges);
  gs_free(binary);

  return 0;
}
