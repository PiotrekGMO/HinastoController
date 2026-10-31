// Small text helpers without any hardware dependency, so they can be tested on a PC (tools/smart-test.cpp).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Copies a text into dst (n bytes with the terminator) and, when it does not fit, cuts it on a character boundary so the
// result is still valid UTF-8. A text that fits is copied unchanged.
static inline void utf8Copy(char* dst, const char* src, size_t n) {
  if (n == 0) return;
  size_t srcLen = strlen(src);
  if (srcLen < n) {
    memcpy(dst, src, srcLen + 1);
    return;
  }
  size_t len = n - 1;
  memcpy(dst, src, len);
  size_t i = len;
  while (i > 0 && ((uint8_t)dst[i - 1] & 0xC0) == 0x80) i--;  // continuation bytes at the end
  if (i > 0 && (uint8_t)dst[i - 1] >= 0xC0) {                  // the character they belong to
    uint8_t lead = (uint8_t)dst[i - 1];
    size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
    if (len - (i - 1) < need) len = i - 1;                    // its tail was cut off: drop it whole
  }
  dst[len] = 0;
}
