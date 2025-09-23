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
