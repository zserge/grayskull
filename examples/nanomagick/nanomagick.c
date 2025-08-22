#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "grayskull.h"

// Terminal width detection
static int get_terminal_width(void) {
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) { return w.ws_col; }
  return 80;  // default fallback
}

// ASCII art rendering with Unicode block characters for better quality
static void render_ascii_art(const struct gs_image img) {
  int term_width = get_terminal_width();
  int display_width = term_width - 2;  // Leave some margin

  // Calculate aspect ratio preserving height
  int display_height =
      (img.h * display_width) / (img.w * 2);  // *2 because chars are roughly 2x taller than wide

  if (display_height > 50) {  // Limit height for terminal
    display_height = 50;
    display_width = (img.w * display_height * 2) / img.h;
  }

  printf("Displaying %ux%u image as %dx%d ASCII art:\n\n", img.w, img.h, display_width,
         display_height);

  // Unicode block characters for 4 gray levels (empty, light, medium, dark, full)
  const char* blocks[] = {" ", "░", "▒", "▓", "█"};

  for (int y = 0; y < display_height; y++) {
    for (int x = 0; x < display_width; x++) {
      // Map display coordinates to image coordinates
      int img_x = (x * img.w) / display_width;
      int img_y = (y * img.h) / display_height;

      // Get pixel value and map to block character
      uint8_t pixel = img.data[img_y * img.w + img_x];
      int block_index = (pixel * 4) / 255;  // Map 0-255 to 0-4
      if (block_index > 4) block_index = 4;

      printf("%s", blocks[block_index]);
    }
    printf("\n");
  }
  printf("\n");
}

static void print_usage(const char* program_name) {
  printf("nanomagick - Simple PGM image processing tool\n");
  printf("Usage: %s <command> [params] <input.pgm> <output.pgm>\n\n", program_name);
  printf("Commands:\n");
  printf("  identify <image.pgm>        Show image information\n");
  printf("  view <image.pgm>            Display image as ASCII art\n");
  printf("  resize <width> <height>     Resize image to width x height\n");
  printf("  blur <radius>               Blur image with given radius\n");
  printf("  threshold <value>           Apply global threshold (0-255)\n");
  printf("  adaptive <size> <constant>  Apply adaptive threshold\n");
  printf("  otsu                        Apply Otsu automatic thresholding\n");
  printf("  sobel                       Apply Sobel edge detection\n");
  printf("  crop <x> <y> <w> <h>        Crop image to specified rectangle\n");
  printf("  erode                       Apply erosion morphology\n");
  printf("  dilate                      Apply dilation morphology\n");
  printf("  help                        Show this help message\n\n");
  printf("Examples:\n");
  printf("  %s identify image.pgm\n", program_name);
  printf("  %s view image.pgm\n", program_name);
  printf("  %s resize 640 480 input.pgm resized.pgm\n", program_name);
  printf("  %s blur 3 input.pgm blurred.pgm\n", program_name);
  printf("  %s adaptive 15 5 input.pgm adaptive.pgm\n", program_name);
  printf("  %s otsu input.pgm thresholded.pgm\n", program_name);
  printf("  %s sobel input.pgm edges.pgm\n", program_name);
  printf("  %s crop 100 100 200 200 input.pgm cropped.pgm\n", program_name);
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  const char* command = argv[1];

  if (strcmp(command, "help") == 0) {
    print_usage(argv[0]);
    return 0;
  }

  // Determine expected arguments based on command
  int expected_args;
  if (strcmp(command, "identify") == 0 || strcmp(command, "view") == 0) {
    expected_args = 3;  // program, command, input
  } else if (strcmp(command, "resize") == 0) {
    expected_args = 6;  // program, command, width, height, input, output
  } else if (strcmp(command, "crop") == 0) {
    expected_args = 8;  // program, command, x, y, width, height, input, output
  } else if (strcmp(command, "blur") == 0 || strcmp(command, "threshold") == 0) {
    expected_args = 5;  // program, command, param, input, output
  } else if (strcmp(command, "adaptive") == 0) {
    expected_args = 6;  // program, command, size, constant, input, output
  } else if (strcmp(command, "otsu") == 0 || strcmp(command, "sobel") == 0 ||
             strcmp(command, "erode") == 0 || strcmp(command, "dilate") == 0) {
    expected_args = 4;  // program, command, input, output
  } else {
    fprintf(stderr, "Error: Unknown command '%s'\n", command);
    print_usage(argv[0]);
    return 1;
  }

  if (argc != expected_args) {
    fprintf(stderr, "Error: Wrong number of arguments for '%s'\n", command);
    print_usage(argv[0]);
    return 1;
  }

  // Get input and output files
  const char* input_file;
  const char* output_file = NULL;

  if (strcmp(command, "identify") == 0 || strcmp(command, "view") == 0) {
    input_file = argv[2];
  } else {
    input_file = argv[argc - 2];
    output_file = argv[argc - 1];
  }

  // Load input image
  printf("Loading %s...\n", input_file);
  struct gs_image img = gs_read_pgm(input_file);
  if (!gs_valid(img)) {
    fprintf(stderr, "Error: Could not load %s\n", input_file);
    return 1;
  }
  printf("Loaded %ux%u image\n", img.w, img.h);

  // Handle identify and view commands (no output file needed)
  if (strcmp(command, "identify") == 0) {
    printf("Image: %s\n", input_file);
    printf("Format: PGM (Portable Graymap)\n");
    printf("Dimensions: %ux%u pixels\n", img.w, img.h);
    printf("Type: Grayscale\n");
    printf("File size: ");
    FILE* f = fopen(input_file, "rb");
    if (f) {
      fseek(f, 0, SEEK_END);
      long size = ftell(f);
      fclose(f);
      if (size < 1024) {
        printf("%ld bytes\n", size);
      } else if (size < 1024 * 1024) {
        printf("%.1f KB\n", size / 1024.0);
      } else {
        printf("%.1f MB\n", size / (1024.0 * 1024.0));
      }
    } else {
      printf("unknown\n");
    }
    gs_free(img);
    return 0;
  }

  if (strcmp(command, "view") == 0) {
    render_ascii_art(img);
    gs_free(img);
    return 0;
  }

  struct gs_image result = {0, 0, NULL};

  // Process based on command
  if (strcmp(command, "resize") == 0) {
    unsigned width = atoi(argv[2]);
    unsigned height = atoi(argv[3]);
    if (width == 0 || height == 0) {
      fprintf(stderr, "Error: Invalid dimensions\n");
      gs_free(img);
      return 1;
    }
    printf("Resizing to %ux%u...\n", width, height);
    result = gs_alloc(width, height);
    if (!gs_valid(result)) {
      fprintf(stderr, "Error: Could not allocate memory\n");
      gs_free(img);
      return 1;
    }
    gs_resize(result, img);

  } else if (strcmp(command, "crop") == 0) {
    unsigned x = atoi(argv[2]);
    unsigned y = atoi(argv[3]);
    unsigned width = atoi(argv[4]);
    unsigned height = atoi(argv[5]);

    if (x + width > img.w || y + height > img.h) {
      fprintf(stderr, "Error: Crop rectangle exceeds image bounds\n");
      gs_free(img);
      return 1;
    }
    if (width == 0 || height == 0) {
      fprintf(stderr, "Error: Crop dimensions must be positive\n");
      gs_free(img);
      return 1;
    }

    printf("Cropping to %ux%u at (%u,%u)...\n", width, height, x, y);
    result = gs_alloc(width, height);
    if (!gs_valid(result)) {
      fprintf(stderr, "Error: Could not allocate memory\n");
      gs_free(img);
      return 1;
    }
    struct gs_rect roi = {x, y, width, height};
    gs_crop(result, img, roi);

  } else if (strcmp(command, "blur") == 0) {
    int radius = atoi(argv[2]);
    if (radius <= 0) {
      fprintf(stderr, "Error: Blur radius must be positive\n");
      gs_free(img);
      return 1;
    }
    printf("Applying blur with radius %d...\n", radius);
    result = gs_alloc(img.w, img.h);
    if (!gs_valid(result)) {
      fprintf(stderr, "Error: Could not allocate memory\n");
      gs_free(img);
      return 1;
    }
    gs_blur(result, img, radius);

  } else if (strcmp(command, "threshold") == 0) {
    int threshold_val = atoi(argv[2]);
    if (threshold_val < 0 || threshold_val > 255) {
      fprintf(stderr, "Error: Threshold must be 0-255\n");
      gs_free(img);
      return 1;
    }
    printf("Applying global threshold %d...\n", threshold_val);
    result = img;  // Threshold operates in-place
    gs_threshold(result, threshold_val);

  } else if (strcmp(command, "adaptive") == 0) {
    unsigned block_size = atoi(argv[2]);
    float c = atof(argv[3]);
    if (block_size == 0 || block_size % 2 == 0) {
      fprintf(stderr, "Error: Block size must be odd and positive\n");
      gs_free(img);
      return 1;
    }
    printf("Applying adaptive threshold (block_size=%u, c=%.1f)...\n", block_size, c);
    result = gs_alloc(img.w, img.h);
    if (!gs_valid(result)) {
      fprintf(stderr, "Error: Could not allocate memory\n");
      gs_free(img);
      return 1;
    }
    gs_adaptive_threshold(result, img, block_size, c);

  } else if (strcmp(command, "otsu") == 0) {
    uint8_t otsu_thresh = gs_otsu_theshold(img);
    printf("Applying Otsu thresholding (threshold=%u)...\n", otsu_thresh);
    result = img;  // Threshold operates in-place
    gs_threshold(result, otsu_thresh);

  } else if (strcmp(command, "sobel") == 0) {
    printf("Applying Sobel edge detection...\n");
    result = gs_alloc(img.w, img.h);
    if (!gs_valid(result)) {
      fprintf(stderr, "Error: Could not allocate memory\n");
      gs_free(img);
      return 1;
    }
    gs_sobel(result, img);

  } else if (strcmp(command, "erode") == 0) {
    printf("Applying erosion...\n");
    result = gs_alloc(img.w, img.h);
    if (!gs_valid(result)) {
      fprintf(stderr, "Error: Could not allocate memory\n");
      gs_free(img);
      return 1;
    }
    gs_erode(result, img);

  } else if (strcmp(command, "dilate") == 0) {
    printf("Applying dilation...\n");
    result = gs_alloc(img.w, img.h);
    if (!gs_valid(result)) {
      fprintf(stderr, "Error: Could not allocate memory\n");
      gs_free(img);
      return 1;
    }
    gs_dilate(result, img);
  }

  // Save result
  printf("Saving to %s...\n", output_file);
  if (gs_write_pgm(result, output_file) != 0) {
    fprintf(stderr, "Error: Could not save %s\n", output_file);
    gs_free(img);
    if (result.data != img.data) gs_free(result);
    return 1;
  }

  printf("Done!\n");

  // Cleanup
  gs_free(img);
  if (result.data != img.data) gs_free(result);

  return 0;
}
