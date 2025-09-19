#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "grayskull.h"

static void identify(struct gs_image img, struct gs_image *out, char *argv[]) {
  (void)out, (void)argv;
  printf("Portable Graymap, %ux%u (%u) pixels\n", img.w, img.h, img.w * img.h);
}

static void view(struct gs_image img, struct gs_image *out, char *argv[]) {
  (void)out, (void)argv;

  char *term = getenv("TERM");
  int use_256 = term && strstr(term, "256color");

  int term_width = 80;
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) { term_width = w.ws_col; }
  int display_width = term_width - 2;  // some margin
  int display_height = (img.h * display_width) / (img.w * (use_256 ? 1 : 2));
  if (use_256) {
    for (int y = 0; y < display_height; y += 2) {
      for (int x = 0; x < display_width; x++) {
        int ix = (x * img.w) / display_width;
        int iy1 = (y * img.h) / display_height;
        int iy2 = ((y + 1) * img.h) / display_height;
        uint8_t p1 = img.data[iy1 * img.w + ix];
        uint8_t p2 = (iy2 < (int)img.h) ? img.data[iy2 * img.w + ix] : p1;
        int c1 = 232 + (p1 * 23) / 255;
        int c2 = 232 + (p2 * 23) / 255;
        printf("\x1b[38;5;%d;48;5;%dm▀", c1, c2);
      }
      printf("\x1b[0m\n");
    }
  } else {
    const char *blocks[] = {" ", "░", "▒", "▓", "█"};
    for (int y = 0; y < display_height; y++) {
      for (int x = 0; x < display_width; x++) {
        int img_x = (x * img.w) / display_width;
        int img_y = (y * img.h) / display_height;
        uint8_t pixel = img.data[img_y * img.w + img_x];
        int block_index = (pixel * 4) / 255;  // Map 0-255 to 0-4
        if (block_index > 4) block_index = 4;
        printf("%s", blocks[block_index]);
      }
      printf("\n");
    }
  }
  printf("\n");
}

static void resize(struct gs_image img, struct gs_image *out, char *argv[]) {
  int w = atoi(argv[0]), h = atoi(argv[1]);
  if (w <= 0 || h <= 0) {
    fprintf(stderr, "Error: Invalid width or height\n");
    return;
  }
  *out = gs_alloc(w, h);
  gs_resize(*out, img);
}

static void crop(struct gs_image img, struct gs_image *out, char *argv[]) {
  int x = atoi(argv[0]), y = atoi(argv[1]), w = atoi(argv[2]), h = atoi(argv[3]);
  if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > (int)img.w || y + h > (int)img.h) {
    fprintf(stderr, "Error: Invalid crop rectangle\n");
    return;
  }
  *out = gs_alloc(w, h);
  gs_crop(*out, img, (struct gs_rect){x, y, w, h});
}

static void blur(struct gs_image img, struct gs_image *out, char *argv[]) {
  int r = atoi(argv[0]);
  if (r <= 0) {
    fprintf(stderr, "Error: Invalid radius: %s\n", argv[0]);
    return;
  }
  *out = gs_alloc(img.w, img.h);
  gs_blur(*out, img, r);
}

static void threshold(struct gs_image img, struct gs_image *out, char *argv[]) {
  int t = strcmp(argv[0], "otsu") == 0 ? gs_otsu_threshold(img) : atoi(argv[0]);
  if (t <= 0) {
    fprintf(stderr, "Error: Invalid threshold: %s\n", argv[0]);
    return;
  }
  *out = gs_alloc(img.w, img.h);
  gs_copy(*out, img);
  gs_threshold(*out, t);
}

static void adaptive(struct gs_image img, struct gs_image *out, char *argv[]) {
  int r = atoi(argv[0]), c = atoi(argv[1]);
  if (r <= 0 || c < 0) {
    fprintf(stderr, "Error: Invalid radius or constant\n");
    return;
  }
  *out = gs_alloc(img.w, img.h);
  gs_adaptive_threshold(*out, img, r, c);
}

static void morph(struct gs_image img, struct gs_image *out, char *argv[]) {
  const char *op = argv[0];
  int n = atoi(argv[1]);
  if ((strcmp(op, "erode") != 0 && strcmp(op, "dilate") != 0) || n <= 0) {
    fprintf(stderr, "Error: Invalid morphological operation or iterations\n");
    return;
  }
  *out = gs_alloc(img.w, img.h);
  struct gs_image temp = gs_alloc(img.w, img.h);
  gs_copy(*out, img);
  for (int i = 0; i < n; i++) {
    if (strcmp(op, "erode") == 0) {
      gs_erode(temp, *out);
    } else if (strcmp(op, "dilate") == 0) {
      gs_dilate(temp, *out);
    } else {
      fprintf(stderr, "Error: Unknown morphological operation: %s\n", op);
      gs_free(temp);
      gs_free(*out);
      *out = (struct gs_image){0, 0, NULL};
      return;
    }
    gs_copy(*out, temp);
  }
  gs_free(temp);
}

static void sobel(struct gs_image img, struct gs_image *out, char *argv[]) {
  (void)argv;
  *out = gs_alloc(img.w, img.h);
  gs_sobel(*out, img);
}

static void blobs(struct gs_image img, struct gs_image *out, char *argv[]) {
  int n = atoi(argv[0]);
  if (n <= 0) {
    fprintf(stderr, "Error: Invalid number of blobs\n");
    return;
  }
  *out = gs_alloc(img.w, img.h);
  gs_label *labels = (gs_label *)calloc(img.w * img.h, sizeof(gs_label));
  struct gs_blob *blobs = (struct gs_blob *)calloc(n, sizeof(struct gs_blob));
  if (!labels || !blobs) {
    fprintf(stderr, "Error: Memory allocation failed\n");
    free(labels);
    free(blobs);
    gs_free(*out);
    *out = (struct gs_image){0, 0, NULL};
    return;
  }
  unsigned nblobs = gs_blobs(img, labels, blobs, n);
  for (unsigned i = 0; i < nblobs; i++) {
    unsigned x1 = GS_MAX(0, (int)blobs[i].box.x - 2), y1 = GS_MAX(0, (int)blobs[i].box.y - 2);
    unsigned x2 = GS_MIN(img.w, blobs[i].box.x + blobs[i].box.w + 2),
             y2 = GS_MIN(img.h, blobs[i].box.y + blobs[i].box.h + 2);
    for (unsigned y = y1; y <= y2; y++) {
      for (unsigned x = x1; x <= x2; x++) { out->data[y * out->w + x] = 128; }
    }
  }
  gs_for(img, x, y) if (img.data[y * out->w + x] > 128) out->data[y * img.w + x] = 255;
}

static void scan(struct gs_image img, struct gs_image *out, char *argv[]) {
  (void)argv;
  struct gs_image tmp = gs_alloc(img.w, img.h);
  // preprocess, remove noise, binarise
  gs_blur(tmp, img, 1);
  gs_threshold(tmp, gs_otsu_threshold(tmp) + 10);
  // find blobs
  gs_label *labels = calloc(img.w * img.h, sizeof(gs_label));
  struct gs_blob blobs[1000];
  unsigned n = gs_blobs(tmp, labels, blobs, sizeof(blobs) / sizeof(blobs[0]));
  // find largest blob
  unsigned largest = 0;
  for (unsigned i = 1; i < n; i++)
    if (blobs[i].area > blobs[largest].area) largest = i;
  // find corners
  struct gs_point corners[4];
  gs_blob_corners(tmp, labels, &blobs[largest], corners);
	// perspective correct
	const unsigned OUTPUT_WIDTH = 800, OUTPUT_HEIGHT = 1000;
	*out = gs_alloc(OUTPUT_WIDTH, OUTPUT_HEIGHT);
	gs_perspective_correct(*out, img, corners);

	gs_free(tmp);
	free(labels);
}

struct cmd {
  const char *name;
  const char *help;
  int argc;
  int hasout;
  void (*func)(struct gs_image img, struct gs_image *out, char *argv[]);
} commands[] = {
    {"identify", "             Show image information", 0, 0, identify},
    {"view", "                 Display image in terminal", 0, 0, view},
    {"resize", "<w> <h>        Resize image to WxH", 2, 1, resize},
    {"crop", "<x> <y> <w> <h>  Crop image to rectangle (x,y,w,h)", 4, 1, crop},
    {"blur", "<r>              Blur image with radius R", 1, 1, blur},
    {"threshold", "<t>         Apply threshold (0-255 or otsu)", 1, 1, threshold},
    {"adaptive", "<r> <c>      Apply adaptive threshold, radius R and constant C", 2, 1, adaptive},
    {"sobel", "                Edge detection (Sobel)", 0, 1, sobel},
    {"morph", "<op> <n>        Morphological operation (erode/dilate) N times", 2, 1, morph},
    {"blobs", "<n>             Find up to N blobs", 1, 1, blobs},
    {"scan", "                 Simple document scanner", 0, 1, scan},
    {NULL, NULL, 0, 0, NULL},
};

static void usage(const char *app) {
  printf("Usage: %s <command> [params] [input.pgm] [output.pgm]\n\n", app);
  printf("Commands:\n");
  for (struct cmd *cmd = commands; cmd->name != NULL; cmd++)
    printf("  %s %s\n", cmd->name, cmd->help);
}

int main(int argc, char *argv[]) {
  if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
    usage(argv[0]);
    return 1;
  }

  for (struct cmd *cmd = commands; cmd->name != NULL; cmd++) {
    if (strcmp(argv[1], cmd->name) != 0) continue;
    // nanomagic <cmd> [params...] <input.pgm> [output.pgm]
    if (argc != cmd->argc + cmd->hasout + 3) {
      fprintf(stderr, "Error: Wrong number of arguments for '%s'\n", argv[1]);
      usage(argv[0]);
      return 1;
    }
    struct gs_image img = gs_read_pgm(argv[cmd->argc + 2]);
    struct gs_image out = {0, 0, NULL};
    cmd->func(img, &out, argv + 2);
    if (cmd->hasout) {
      if (!out.data) {
        fprintf(stderr, "Error: Command '%s' did not produce output image\n", argv[1]);
        gs_free(img);
        return 1;
      }
      if (gs_write_pgm(out, argv[cmd->argc + 3]) != 0) {
        fprintf(stderr, "Error: Could not save %s\n", argv[cmd->argc + 3]);
        gs_free(img);
        gs_free(out);
        return 1;
      }
      gs_free(out);
    }
    gs_free(img);
    return 0;
  }
  printf("Error: Unknown command '%s'\n", argv[1]);
  return 1;
}
