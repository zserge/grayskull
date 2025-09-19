#include <assert.h>

#include "grayskull.h"

static void test_crop(void) {
  uint8_t data[4 * 4] = {
      0, 0, 0, 0,  //
      0, 1, 0, 0,  //
      0, 1, 1, 0,  //
      0, 0, 0, 0   //
  };
  struct gs_image img = {4, 4, data};
  struct gs_rect rect = {1, 1, 3, 2};
  uint8_t cropped_data[3 * 2];
  struct gs_image cropped = {3, 2, cropped_data};
  gs_crop(cropped, img, rect);
  uint8_t expected[3 * 2] = {
      1, 0, 0,  //
      1, 1, 0   //
  };
  gs_for(cropped, x, y) assert(cropped.data[y * cropped.w + x] == expected[y * cropped.w + x]);
}

static void test_resize(void) {
  // Downscale: 4x4 -> 2x2
  uint8_t data_4x4[4 * 4] = {
      0,  50,  100, 150,  //
      25, 75,  125, 175,  //
      50, 100, 150, 200,  //
      75, 125, 175, 225   //
  };
  struct gs_image img_4x4 = {4, 4, data_4x4};
  uint8_t data_2x2[2 * 2];
  struct gs_image resized_2x2 = {2, 2, data_2x2};
  gs_resize(resized_2x2, img_4x4);
  uint8_t expected_2x2[2 * 2] = {
      37, 137,  // (0+50+25+75)/4=37.5, (100+150+125+175)/4=137.5
      87, 187   // (50+100+75+125)/4=62.5, (150+200+175+225)/4=162.5
  };
  gs_for(resized_2x2, x, y) {
    assert(resized_2x2.data[y * resized_2x2.w + x] == expected_2x2[y * resized_2x2.w + x]);
  }

  // Upscale: 2x2 -> 4x4
  struct gs_image img_2x2 = {2, 2, expected_2x2};
  uint8_t data_upscaled[4 * 4];
  struct gs_image upscaled_4x4 = {4, 4, data_upscaled};
  gs_resize(upscaled_4x4, img_2x2);
  uint8_t expected_4x4[4 * 4] = {
      37, 62,  112, 137,  //
      49, 74,  124, 149,  //
      74, 99,  149, 174,  //
      87, 112, 162, 187   //
  };
  gs_for(upscaled_4x4, x, y) {
    assert(upscaled_4x4.data[y * upscaled_4x4.w + x] == expected_4x4[y * upscaled_4x4.w + x]);
  }

  // Same size resize
  uint8_t data_same[2 * 2] = {10, 20, 30, 40};
  struct gs_image img_same = {2, 2, data_same};
  uint8_t data_same_result[2 * 2];
  struct gs_image same_result = {2, 2, data_same_result};
  gs_resize(same_result, img_same);
  gs_for(same_result, x, y) {
    assert(same_result.data[y * same_result.w + x] == data_same[y * img_same.w + x]);
  }
}

#define W 255  // use one-letter define to align with "0" for black

static void test_blur(void) {
  uint8_t data[3 * 3] = {
      0, 0, 0,  //
      0, W, 0,  //
      0, 0, 0   //
  };
  struct gs_image src = {3, 3, data};
  uint8_t blurred_data[3 * 3];
  struct gs_image dst = {3, 3, blurred_data};
  gs_blur(dst, src, 1);
  uint8_t center = dst.data[1 * 3 + 1];
  assert(center == 28);  // (0+0+0+0+2555+0+0+0+0)/9 = 28.33
  uint8_t corner = dst.data[0 * 3 + 0];
  assert(corner == 63);  // (0+0+0+255)/4
}

static void test_morph(void) {
  uint8_t data_erode[5 * 5] = {
      0, 0, 0, 0, 0,  //
      0, W, W, W, 0,  //
      0, W, W, W, 0,  //
      0, W, W, W, 0,  //
      0, 0, 0, 0, 0   //
  };
  struct gs_image src_erode = {5, 5, data_erode};
  uint8_t eroded_data[5 * 5];
  struct gs_image dst_erode = {5, 5, eroded_data};
  gs_erode(dst_erode, src_erode);
  assert(dst_erode.data[2 * 5 + 2] == 255);  // center pixel should remain white
  assert(dst_erode.data[1 * 5 + 1] == 0);    // edge pixel should become black

  uint8_t data_dilate[5 * 5] = {
      0, 0, 0, 0, 0,  //
      0, 0, 0, 0, 0,  //
      0, 0, W, 0, 0,  //
      0, 0, 0, 0, 0,  //
      0, 0, 0, 0, 0   //
  };
  struct gs_image src_dilate = {5, 5, data_dilate};
  uint8_t dilated_data[5 * 5];
  struct gs_image dst_dilate = {5, 5, dilated_data};
  gs_dilate(dst_dilate, src_dilate);
  // center pixel should remain white, as well as top/bottom/left/right
  assert(dst_dilate.data[2 * 5 + 2] == 255);
  assert(dst_dilate.data[1 * 5 + 2] == 255 && dst_dilate.data[3 * 5 + 2] == 255 &&
         dst_dilate.data[2 * 5 + 1] == 255 && dst_dilate.data[2 * 5 + 3] == 255);
  assert(dst_dilate.data[0 * 5 + 0] == 0);  // corner pixel should remain black
}

static void test_sobel(void) {
  uint8_t data[5 * 5] = {
      0, 0, W, W, W,  // vertical edge
      0, 0, W, W, W,  //
      0, 0, W, W, W,  //
      0, 0, W, W, W,  //
      0, 0, W, W, W   //
  };
  struct gs_image src = {5, 5, data};
  uint8_t sobel_data[5 * 5] = {0};
  struct gs_image dst = {5, 5, sobel_data};
  gs_sobel(dst, src);
  assert(dst.data[2 * 5 + 2] > 100 && dst.data[3 * 5 + 2] > 100);  // edge at column 2
  assert(dst.data[2 * 5 + 0] == 0);                                // away from edge should be 0

  uint8_t data_horizontal[5 * 5] = {
      0, 0, 0, 0, 0,  // horizontal edge
      0, 0, 0, 0, 0,  //
      W, W, W, W, W,  //
      W, W, W, W, W,  //
      W, W, W, W, W   //
  };
  struct gs_image src_h = {5, 5, data_horizontal};
  uint8_t sobel_data_h[5 * 5] = {0};
  struct gs_image dst_h = {5, 5, sobel_data_h};
  gs_sobel(dst_h, src_h);
  assert(dst_h.data[2 * 5 + 2] > 100 && dst_h.data[2 * 5 + 3] > 100);  // edge at row 2
  assert(dst_h.data[0 * 5 + 2] == 0);                                  // away from edge should be 0
}

static void test_histogram(void) {
  uint8_t data[3 * 3] = {
      0,   50,  100,  //
      50,  100, 150,  //
      100, 150, 200   //
  };
  struct gs_image img = {3, 3, data};
  unsigned hist[256];
  gs_histogram(img, hist);

  assert(hist[0] == 1 && hist[50] == 2 && hist[100] == 3 && hist[150] == 2 && hist[200] == 1);
  unsigned total = 0;
  for (int i = 0; i < 256; i++) total += hist[i];
  assert(total == 9);
}

static void test_threshold(void) {
  uint8_t data[2 * 2] = {
      50, 150,  //
      75, 200   //
  };
  struct gs_image img = {2, 2, data};
  gs_threshold(img, 100);
  assert(data[0] == 0 && data[1] == 255 && data[2] == 0 && data[3] == 255);
}

static void test_otsu(void) {
  uint8_t data[3 * 3] = {
      40,  50,  60,  // dark cluster
      45,  55,  50,  // dark cluster
      190, 200, 210  // bright cluster
  };
  struct gs_image img = {3, 3, data};
  uint8_t otsu_thresh = gs_otsu_threshold(img);
  assert(otsu_thresh == 60);  // anything above 60 = white, below = black

  uint8_t uniform_data[4] = {0, 85, 170, 255};
  struct gs_image uniform_img = {2, 2, uniform_data};
  uint8_t uniform_thresh = gs_otsu_threshold(uniform_img);
  assert(uniform_thresh == 85);  // above 85 = white, below = black

  uint8_t same_data[4] = {128, 128, 128, 128};
  struct gs_image same_img = {2, 2, same_data};
  uint8_t same_thresh = gs_otsu_threshold(same_img);
  assert(same_thresh == 0);  // no variation, should return 0
}

static void test_adaptive_threshold(void) {
  uint8_t data[5 * 5] = {
      50,  50,  200, 50,  50,   //
      50,  50,  200, 50,  50,   //
      50,  50,  200, 50,  50,   //
      200, 200, 100, 200, 200,  //
      200, 200, 100, 200, 200   //
  };
  uint8_t threshold[5 * 5] = {
      0, 0, W, 0, 0,  //
      0, 0, W, 0, 0,  //
      0, 0, W, 0, 0,  //
      W, W, 0, W, W,  //
      0, W, 0, W, 0   //
  };
  uint8_t threshold_5[5 * 5] = {
      W, 0, W, 0, W,  //
      W, 0, W, 0, W,  //
      0, 0, W, 0, 0,  //
      W, W, 0, W, W,  //
      W, W, 0, W, W   //
  };
  struct gs_image src = {5, 5, data};
  uint8_t adaptive_data[5 * 5];
  struct gs_image dst = {5, 5, adaptive_data};

  gs_adaptive_threshold(dst, src, 1, 0);
  for (unsigned i = 0; i < 25; i++) assert(dst.data[i] == threshold[i]);

  gs_adaptive_threshold(dst, src, 1, 5);
  for (unsigned i = 0; i < 25; i++) assert(dst.data[i] == threshold_5[i]);
}

static void test_blobs(void) {
  uint8_t data[6 * 5] = {
      W, W, 0, 0, W, 0,  //
      W, 0, 0, W, W, 0,  //
      0, 0, W, W, 0, 0,  //
      W, W, W, 0, 0, W,  //
      0, W, 0, 0, 0, W   //
  };
  struct gs_image img = {6, 5, data};

  gs_label labels[6 * 5] = {0};
  struct gs_blob blobs[10] = {0};
  unsigned n = gs_blobs(img, labels, blobs, 10);
  assert(n == 3);
  struct gs_blob expected[] = {
      {1, 3, {0, 0, 2, 2}, {0, 0}},  //
      {2, 9, {0, 0, 5, 5}, {2, 2}},  //
      {6, 2, {5, 3, 1, 2}, {5, 3}}   //
  };
  (void)expected;
  for (unsigned i = 0; i < n; i++) {
    assert(blobs[i].label == expected[i].label);
    assert(blobs[i].area == expected[i].area);
    assert(blobs[i].box.x == expected[i].box.x && blobs[i].box.y == expected[i].box.y);
    assert(blobs[i].box.w == expected[i].box.w && blobs[i].box.h == expected[i].box.h);
    assert(blobs[i].centroid.x == expected[i].centroid.x &&
           blobs[i].centroid.y == expected[i].centroid.y);
  }
}

static void test_trace_contour(void) {
  uint8_t data[5 * 5] = {
      0, W, W, W, 0,  //
      0, W, W, W, 0,  //
      0, W, 0, W, W,  //
      0, W, W, W, 0,  //
      0, 0, W, 0, W   //
  };
  uint8_t visited_data[5 * 5] = {0};
  uint8_t expected_visisted_data[5 * 5] = {
      0, W, W, W, 0,  //
      0, W, 0, W, 0,  //
      0, W, 0, 0, W,  //
      0, W, 0, W, 0,  //
      0, 0, W, 0, 0   //
  };
  struct gs_image img = {5, 5, data};
  struct gs_image visited = {5, 5, visited_data};
  struct gs_contour contour;
  contour.start = (struct gs_point){1, 0};
  gs_trace_contour(img, visited, &contour);
  assert(contour.length == 10);
  assert(contour.box.x == 1 && contour.box.y == 0 && contour.box.w == 4 && contour.box.h == 5);
  gs_for(visited, x, y) {
    assert(visited.data[y * visited.w + x] == expected_visisted_data[y * visited.w + x]);
  }
}

int main(void) {
  test_crop();
  test_resize();
  test_blur();
  test_histogram();
  test_threshold();
  test_adaptive_threshold();
  test_otsu();
  test_morph();
  test_sobel();
  test_blobs();
  test_trace_contour();
  return 0;
}
