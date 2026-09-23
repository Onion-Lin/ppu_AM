/* ppu_core.c -- the I/O-free half of the PPU platform layer.
 *
 * Everything here is pure: it turns an AM_GPU_FBDRAW request into PPU register
 * writes, but it never touches a bus.  That is what lets the host test suite
 * exercise the frame encoding, the palette lookup and the rect clipping without
 * a board, and lets the Verilator suite drive the real RTL from the same code.
 *
 * The MMIO transport lives in ppu_qspi.c and is compiled only for the target. */

#include "ppu.h"

#include "ppu_core.h"

/* ------------------------------------------------------------------ palette */

/* VGA/EGA default 16 colours, each truncated to the PPU's 2-2-2 form.
 * All 16 are distinct after truncation; the pair that is easy to get wrong is
 * light green (01_11_01) vs light cyan (01_11_11), which differ in blue. */
const uint32_t ppu_pal_rgb6[PPU_PAL_SLOTS] = {
  0x00,  /*  0 black        */
  0x02,  /*  1 blue         */
  0x08,  /*  2 green        */
  0x0a,  /*  3 cyan         */
  0x20,  /*  4 red          */
  0x22,  /*  5 magenta      */
  0x24,  /*  6 brown        */
  0x2a,  /*  7 light gray   */
  0x15,  /*  8 dark gray    */
  0x17,  /*  9 light blue   */
  0x1d,  /* 10 light green  */
  0x1f,  /* 11 light cyan   */
  0x35,  /* 12 light red    */
  0x37,  /* 13 light magenta*/
  0x3d,  /* 14 yellow       */
  0x3f,  /* 15 white        */
};

/* Perceptual-ish channel weights.  Only ever used for 64*16 comparisons at
 * init, so it can afford to be careful. */
#define W_R 2u
#define W_G 4u
#define W_B 3u

static uint32_t chan(unsigned level) { return level * 85u; }  /* 0/1/2/3 -> 0..255 */

void ppu_pal_lut_build(uint8_t lut[PPU_RGB6_MAX]) {
  for (unsigned v = 0; v < PPU_RGB6_MAX; v++) {
    unsigned r = (v >> 4) & 3u, g = (v >> 2) & 3u, b = v & 3u;
    unsigned vr = chan(r), vg = chan(g), vb = chan(b);
    unsigned best = 0, bestd = 0xffffffffu;
    for (unsigned s = 0; s < PPU_PAL_SLOTS; s++) {
      unsigned p = ppu_pal_rgb6[s];
      unsigned dr = vr - chan((p >> 4) & 3u);
      unsigned dg = vg - chan((p >> 2) & 3u);
      unsigned db = vb - chan(p & 3u);
      unsigned d = W_R * dr * dr + W_G * dg * dg + W_B * db * db;
      if (d < bestd) { bestd = d; best = s; }
    }
    lut[v] = (uint8_t)best;
  }
}

/* ------------------------------------------------------------------- clip */

/* The PPU does not clip: an out-of-range rectangle wraps its address and
 * coordinates, so the driver must not send one.  AM clients do send oversized
 * rects in practice -- demo/include/io.h screen_clear() submits a SCREEN_W=320
 * wide line, and slider/main.c submits 400x300 -- so this is load-bearing, not
 * defensive polish.
 *
 * Returns 0 and leaves *out untouched when nothing survives. */
int ppu_clip_rect(int x, int y, int w, int h, ppu_rect_t *out) {
  if (w <= 0 || h <= 0) return 0;

  int x0 = x, y0 = y, x1 = x + w, y1 = y + h;   /* half-open */
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > PPU_FB_W) x1 = PPU_FB_W;
  if (y1 > PPU_FB_H) y1 = PPU_FB_H;
  if (x0 >= x1 || y0 >= y1) return 0;

  out->x = x0;
  out->y = y0;
  out->w = x1 - x0;
  out->h = y1 - y0;
  return 1;
}
