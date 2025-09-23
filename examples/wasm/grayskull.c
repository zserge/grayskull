#include <stddef.h>

// A simple bump allocator for our WASM module since we don't have stdlib.
#define MEMORY_HEAP_SIZE (1024 * 1024 * 8)  // 8MB heap for a few buffers
static unsigned char memory_heap[MEMORY_HEAP_SIZE];
static size_t heap_ptr = 0;
void* gs_alloc(size_t size) {
  size_t aligned_size = (size + 7) & ~7;
  if (heap_ptr + aligned_size > MEMORY_HEAP_SIZE) return NULL;
  void* ptr = &memory_heap[heap_ptr];
  heap_ptr += aligned_size;
  return ptr;
}
void gs_free(void* ptr) { (void)ptr; /* No-op for bump allocator */ }
void gs_reset_allocator(void) { heap_ptr = 0; }

// Minimal standard library functions for WASM nostdlib build
void* memset(void* s, int c, size_t n) {
  unsigned char* p = (unsigned char*)s;
  for (size_t i = 0; i < n; i++) p[i] = (unsigned char)c;
  return s;
}

void* memcpy(void* dest, const void* src, size_t n) {
  unsigned char* d = (unsigned char*)dest;
  const unsigned char* s = (const unsigned char*)src;
  for (size_t i = 0; i < n; i++) d[i] = s[i];
  return dest;
}

#define gs_assert(cond)
#define GS_NO_STDLIB
#define GS_API
#include "../../grayskull.h"

#define NUM_BUFFERS 3
static struct gs_image images[NUM_BUFFERS];

// Functions to be exported to WASM
void gs_init_image(int idx, int w, int h) {
  if (idx < 0 || idx >= NUM_BUFFERS) return;
  // Simple bump allocation, so we just allocate once on first use.
  // gs_reset_allocator() must be called from JS if sizes change.
  if (images[idx].data == NULL) { images[idx].data = gs_alloc(w * h); }
  images[idx].w = w;
  images[idx].h = h;
}

uint8_t* gs_get_image_data(int idx) {
  if (idx < 0 || idx >= NUM_BUFFERS) return NULL;
  return images[idx].data;
}

void gs_copy_image(int dst_idx, int src_idx) {
  if (dst_idx < 0 || dst_idx >= NUM_BUFFERS || src_idx < 0 || src_idx >= NUM_BUFFERS) return;
  gs_copy(images[dst_idx], images[src_idx]);
}

void gs_blur_image(int dst_idx, int src_idx, int radius) {
  if (dst_idx < 0 || dst_idx >= NUM_BUFFERS || src_idx < 0 || src_idx >= NUM_BUFFERS) return;
  gs_blur(images[dst_idx], images[src_idx], radius);
}

uint8_t gs_otsu_threshold_image(int src_idx) {
  if (src_idx < 0 || src_idx >= NUM_BUFFERS) return 0;
  return gs_otsu_threshold(images[src_idx]);
}

void gs_threshold_image(int img_idx, uint8_t threshold) {
  if (img_idx < 0 || img_idx >= NUM_BUFFERS) return;
  gs_threshold(images[img_idx], threshold);
}

void gs_adaptive_threshold_image(int dst_idx, int src_idx, int block_size) {
  if (dst_idx < 0 || dst_idx >= NUM_BUFFERS || src_idx < 0 || src_idx >= NUM_BUFFERS) return;
  gs_adaptive_threshold(images[dst_idx], images[src_idx], block_size | 1, 2);
}

void gs_erode_image(int dst_idx, int src_idx) {
  if (dst_idx < 0 || dst_idx >= NUM_BUFFERS || src_idx < 0 || src_idx >= NUM_BUFFERS) return;
  gs_erode(images[dst_idx], images[src_idx]);
}

void gs_dilate_image(int dst_idx, int src_idx) {
  if (dst_idx < 0 || dst_idx >= NUM_BUFFERS || src_idx < 0 || src_idx >= NUM_BUFFERS) return;
  gs_dilate(images[dst_idx], images[src_idx]);
}

void gs_sobel_image(int dst_idx, int src_idx) {
  if (dst_idx < 0 || dst_idx >= NUM_BUFFERS || src_idx < 0 || src_idx >= NUM_BUFFERS) return;
  gs_sobel(images[dst_idx], images[src_idx]);
}

// Blob detection functions
static gs_label labels_buffer[640 * 480];  // Max image size buffer for labels
static struct gs_blob blobs_buffer[200];   // Buffer for blob storage

unsigned gs_detect_blobs(int src_idx, unsigned max_blobs) {
  if (src_idx < 0 || src_idx >= NUM_BUFFERS) return 0;
  if (max_blobs > 200) max_blobs = 200;  // Limit to buffer size

  unsigned size = images[src_idx].w * images[src_idx].h;
  for (unsigned i = 0; i < size; i++) labels_buffer[i] = 0;
  for (unsigned i = 0; i < max_blobs; i++) blobs_buffer[i] = (struct gs_blob){0};

  return gs_blobs(images[src_idx], labels_buffer, blobs_buffer, max_blobs);
}

struct gs_blob* gs_get_blob(unsigned idx) {
  if (idx >= 200) return NULL;
  return &blobs_buffer[idx];
}

gs_label* gs_get_labels_buffer(void) { return labels_buffer; }

// FAST keypoint detection
static struct gs_keypoint keypoints_buffer[500];
static uint8_t scoremap_buffer[640 * 480];

unsigned gs_detect_fast_keypoints(int src_idx, unsigned threshold, unsigned max_keypoints) {
  if (src_idx < 0 || src_idx >= NUM_BUFFERS) return 0;
  if (max_keypoints > 500) max_keypoints = 500;

  struct gs_image scoremap = {images[src_idx].w, images[src_idx].h, scoremap_buffer};
  return gs_fast(images[src_idx], scoremap, keypoints_buffer, max_keypoints, threshold);
}

struct gs_keypoint* gs_get_keypoint(unsigned idx) {
  if (idx >= 500) return NULL;
  return &keypoints_buffer[idx];
}

// ORB feature extraction
static struct gs_keypoint orb_keypoints_buffer[300];
static struct gs_keypoint template_keypoints_buffer[300];
static struct gs_match matches_buffer[200];

unsigned gs_extract_orb_features(int src_idx, unsigned threshold, unsigned max_keypoints) {
  if (src_idx < 0 || src_idx >= NUM_BUFFERS) return 0;
  if (max_keypoints > 300) max_keypoints = 300;

  return gs_orb_extract(images[src_idx], orb_keypoints_buffer, max_keypoints, threshold,
                        scoremap_buffer);
}

struct gs_keypoint* gs_get_orb_keypoint(unsigned idx) {
  if (idx >= 300) return NULL;
  return &orb_keypoints_buffer[idx];
}

void gs_store_template_keypoints(unsigned count) {
  if (count > 300) count = 300;
  for (unsigned i = 0; i < count; i++) { template_keypoints_buffer[i] = orb_keypoints_buffer[i]; }
}

unsigned gs_match_orb_features(unsigned template_count, unsigned scene_count, float max_distance) {
  if (template_count > 300) template_count = 300;
  if (scene_count > 300) scene_count = 300;

  return gs_match_orb(template_keypoints_buffer, template_count, orb_keypoints_buffer, scene_count,
                      matches_buffer, 200, max_distance);
}

struct gs_match* gs_get_match(unsigned idx) {
  if (idx >= 200) return NULL;
  return &matches_buffer[idx];
}

struct gs_keypoint* gs_get_template_keypoint(unsigned idx) {
  if (idx >= 300) return NULL;
  return &template_keypoints_buffer[idx];
}