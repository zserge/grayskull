#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "grayskull.h"

// face detection data from opencv lbpcascade_frontalface.xml cascade
#include "frontalface.h"

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

static void draw_line(struct gs_image img, unsigned x1, unsigned y1, unsigned x2, unsigned y2,
                      uint8_t color) {
  int dx = abs((int)x2 - (int)x1), dy = abs((int)y2 - (int)y1);
  int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1, err = dx - dy;
  int x = x1, y = y1;
  while (1) {
    if (x >= 0 && x < (int)img.w && y >= 0 && y < (int)img.h) img.data[y * img.w + x] = color;
    if (x == (int)x2 && y == (int)y2) break;
    int e2 = 2 * err;
    if (e2 > -dy) err -= dy, x += sx;
    if (e2 < dx) err += dx, y += sy;
  }
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
static int sort_keypoints(const void *a, const void *b) {
  const struct gs_keypoint *kp1 = (const struct gs_keypoint *)a,
                           *kp2 = (const struct gs_keypoint *)b;
  return kp2->response - kp1->response;
}

static void keypoints(struct gs_image img, struct gs_image *out, char *argv[]) {
  int n = atoi(argv[0]), t = atoi(argv[1]);
  if (n <= 0 || t < 0) {
    fprintf(stderr, "Error: Invalid number of keypoints or threshold\n");
    return;
  }
  struct gs_keypoint *kps = (struct gs_keypoint *)calloc(5000, sizeof(struct gs_keypoint));
  if (!kps) {
    fprintf(stderr, "Error: Memory allocation failed\n");
    return;
  }
  struct gs_image tmp = gs_alloc(img.w, img.h);
  unsigned nkps = gs_fast(img, tmp, kps, 5000, t);
  // sort keypoints by response (score)
  qsort(kps, nkps, sizeof(struct gs_keypoint), sort_keypoints);

  *out = gs_alloc(img.w, img.h);
  gs_copy(*out, img);
  for (unsigned i = 0; i < GS_MIN((unsigned)n, nkps); i++) {
    unsigned x = kps[i].pt.x, y = kps[i].pt.y, r = 2;
    for (int dy = -r; dy <= (int)r; dy++) gs_set(*out, x, y + dy, 255);
    for (int dx = -r; dx <= (int)r; dx++) gs_set(*out, x + dx, y, 255);
  }
  gs_free(tmp);
  free(kps);
}

// Pyramid ORB extraction for nanomagick
static unsigned extract_pyramid_orb_nm(struct gs_image img, struct gs_keypoint *kps, unsigned nkps,
                                       unsigned threshold, uint8_t *buffer, unsigned n_levels) {
  if (n_levels > 4) n_levels = 4;
  struct gs_image pyramid[4];
  uint8_t *scoremaps[4];
  unsigned total_kps = 0, buffer_offset = 0;

  pyramid[0] = img;

  // Generate downsampled levels
  for (unsigned level = 1; level < n_levels; level++) {
    unsigned w = pyramid[level - 1].w / 2, h = pyramid[level - 1].h / 2;
    if (w < 32 || h < 32) {
      n_levels = level;
      break;
    }
    pyramid[level] = (struct gs_image){w, h, buffer + buffer_offset};
    buffer_offset += w * h;
    gs_downsample(pyramid[level], pyramid[level - 1]);
  }

  // Allocate scoremap buffers
  for (unsigned level = 0; level < n_levels; level++) {
    scoremaps[level] = buffer + buffer_offset;
    buffer_offset += pyramid[level].w * pyramid[level].h;
  }

  // Extract features from each level
  for (unsigned level = 0; level < n_levels; level++) {
    unsigned level_nkps = nkps / n_levels;
    if (level == n_levels - 1) level_nkps = nkps - total_kps;
    if (level_nkps == 0) continue;

    unsigned level_kps =
        gs_orb_extract(pyramid[level], &kps[total_kps], level_nkps, threshold, scoremaps[level]);

    // Scale coordinates back to original image size
    unsigned scale = 1 << level;
    for (unsigned i = total_kps; i < total_kps + level_kps; i++) {
      kps[i].pt.x *= scale;
      kps[i].pt.y *= scale;
    }
    total_kps += level_kps;
  }
  return total_kps;
}

static void orb(struct gs_image img, struct gs_image *out, char *argv[]) {
  struct gs_image template = gs_read_pgm(argv[0]);
  if (!gs_valid(template)) {
    printf("Error: Cannot load template image %s\n", argv[0]);
    return;
  }

  static uint8_t buffer[1024 * 1024];
  static struct gs_keypoint template_kps[5000], scene_kps[5000];
  static struct gs_match matches[300];

  // Extract pyramid ORB features
  unsigned n_template = extract_pyramid_orb_nm(template, template_kps, 2500, 20, buffer, 3);
  unsigned n_scene = extract_pyramid_orb_nm(img, scene_kps, 2500, 20, buffer, 3);

  unsigned n_matches =
      gs_match_orb(template_kps, n_template, scene_kps, n_scene, matches, 300, 60.0f);

  printf("Template: %u keypoints, Scene: %u keypoints, Matches: %u\n", n_template, n_scene,
         n_matches);

  if (n_matches > 0) {
    // Sort matches by distance
    for (unsigned i = 0; i < n_matches - 1; i++)
      for (unsigned j = i + 1; j < n_matches; j++)
        if (matches[j].distance < matches[i].distance) {
          struct gs_match temp = matches[i];
          matches[i] = matches[j];
          matches[j] = temp;
        }

    // Create stitched output
    *out = gs_alloc(template.w + img.w, GS_MAX(template.h, img.h));
    for (unsigned i = 0; i < out->w * out->h; i++) out->data[i] = 0;

    // Stitch images
    for (unsigned y = 0; y < template.h; y++)
      for (unsigned x = 0; x < template.w; x++)
        out->data[y * out->w + x] = template.data[y * template.w + x];
    for (unsigned y = 0; y < img.h; y++)
      for (unsigned x = 0; x < img.w; x++)
        out->data[y * out->w + (template.w + x)] = img.data[y * img.w + x];

    // Draw top matches
    unsigned draw_matches = GS_MIN(15, n_matches);
    for (unsigned i = 0; i < draw_matches; i++) {
      unsigned x1 = template_kps[matches[i].idx1].pt.x, y1 = template_kps[matches[i].idx1].pt.y;
      unsigned x2 = scene_kps[matches[i].idx2].pt.x + template.w,
               y2 = scene_kps[matches[i].idx2].pt.y;
      draw_line(*out, x1, y1, x2, y2, 255);
    }
  }
  gs_free(template);
}

static void faces(struct gs_image img, struct gs_image *out, char *argv[]) {
  static uint32_t integral[640 * 480];  // Max typical image size
  static struct gs_rect rects[100];

  int min_neighbors = argv[0] ? atoi(argv[0]) : 1;
  if (min_neighbors <= 0) {
    fprintf(stderr, "Error: minimum neighbors must be positive\n");
    return;
  }

  if (img.w * img.h > 640 * 480) {
    fprintf(stderr, "Error: Image too large for face detection (max 640x480)\n");
    return;
  }

  gs_integral(img, integral);
  unsigned nrects = gs_lbp_detect(&frontalface, integral, img.w, img.h, rects, 100, 1.2f, 1.0f,
                                  4.0f, min_neighbors);

  *out = gs_alloc(img.w, img.h);
  gs_copy(*out, img);

  for (unsigned i = 0; i < nrects; i++) {
    struct gs_rect r = rects[i];
    draw_line(*out, r.x, r.y, r.x + r.w, r.y, 255);
    draw_line(*out, r.x, r.y + r.h, r.x + r.w, r.y + r.h, 255);
    draw_line(*out, r.x, r.y, r.x, r.y + r.h, 255);
    draw_line(*out, r.x + r.w, r.y, r.x + r.w, r.y + r.h, 255);
  }
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
    {"keypoints", "<n> <t>     Detect N keypoints with threshold T", 2, 1, keypoints},
    {"orb", "<template.pgm>    Find template in scene using ORB features", 1, 1, orb},
    {"faces", "<n>             Detect faces using LBP cascade with N minNeighbors", 1, 1, faces},
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
