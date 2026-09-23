/* ppu_core.h -- declarations for the I/O-free part of the PPU platform layer. */
#ifndef PPU_CORE_H
#define PPU_CORE_H

#include <stdint.h>

#include "ppu.h"

/* Build the rgb6 -> palette-slot table.  Pure, so the host suites can call it
 * directly.  Every one of the 64 entries is written. */
void ppu_pal_lut_build(uint8_t lut[PPU_RGB6_MAX]);

/* A framebuffer rectangle after clipping to the real 256x240. */
typedef struct {
  int x, y, w, h;
} ppu_rect_t;

/* Clip an AM_GPU_FBDRAW rectangle to the framebuffer.  Returns 0 when nothing
 * survives (fully outside, or zero-sized).  The PPU wraps instead of clipping,
 * so every path that issues a FILL must go through this. */
int ppu_clip_rect(int x, int y, int w, int h, ppu_rect_t *out);

/* Every caller of ppu_pal_lut_build needs the table; keep one definition. */
extern const uint32_t ppu_pal_rgb6[PPU_PAL_SLOTS];

#endif
