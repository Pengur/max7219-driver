#ifndef FONT_T
#define FONT_T

#include <stdint.h>

// ai generated

typedef struct {
  uint8_t first_char;
  uint8_t last_char;

  uint8_t width;
  uint8_t height;

  const uint8_t *font_array;
} font_t;

extern const font_t font_def;

#endif
