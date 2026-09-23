/* Palette tests.
 *
 * The interesting assertions are the exact-match ones: the colours the real
 * client programs use are transcribed from am-kernels, so if a future palette
 * edit silently loses one of them, this fails.
 *
 *   typing-game/game.c:9-12   0xeeeeee white, 0xff0033 red, 0x00cc33 green,
 *                             0x2a0a29 purple background
 *   snake/snake.c:45          0x0000ff00 board green
 *   litenes/src/fce.h:17-26   the 64-entry NES palette; the entries quoted
 *                             below are the ones a frame actually lands on
 */
#include "ppu_test.h"
#include "ppu.h"
#include "ppu_core.h"

static int lut_slot_for(uint8_t lut[PPU_RGB6_MAX], uint32_t rgb888) {
  uint32_t v = ppu_rgb6((rgb888 >> 16) & 0xffu,
                        (rgb888 >> 8) & 0xffu,
                        rgb888 & 0xffu);
  return lut[v];
}

/* Which palette slot a colour lands on, by hex, for readable failure output. */
static void show(uint32_t rgb888, int slot) {
  static const char *names[PPU_PAL_SLOTS] = {
    "black", "blue", "green", "cyan", "red", "magenta", "brown", "ltgray",
    "dkgray", "ltblue", "ltgreen", "ltcyan", "ltred", "ltmagenta", "yellow",
    "white"
  };
  printf("            0x%06x -> slot %2d (%s)\n", rgb888, slot, names[slot]);
}

void test_palette_suite(void) {
  uint8_t lut[PPU_RGB6_MAX];
  ppu_pal_lut_build(lut);

  /* ---- every input maps to a valid slot, and the map is total ---- */
  for (unsigned v = 0; v < PPU_RGB6_MAX; v++) {
    CHECK(lut[v] < PPU_PAL_SLOTS);
    /* rebuild an RGB888 pixel that truncates back to v, and confirm it does */
    uint32_t r8 = ((v >> 4) & 3u) * 85u;
    uint32_t g8 = ((v >> 2) & 3u) * 85u;
    uint32_t b8 = (v & 3u) * 85u;
    uint32_t px = (r8 << 16) | (g8 << 8) | b8;
    CHECK_EQ_U(ppu_rgb6((px >> 16) & 0xffu, (px >> 8) & 0xffu, px & 0xffu), v);
  }

  /* ---- every palette slot is reachable by at least one input ---- */
  {
    int seen[PPU_PAL_SLOTS] = {0};
    for (unsigned v = 0; v < PPU_RGB6_MAX; v++) seen[lut[v]] = 1;
    for (unsigned s = 0; s < PPU_PAL_SLOTS; s++)
      CHECK(seen[s]);   /* a dead slot means the palette wastes an entry */
  }

  /* ---- rgb6 truncation itself ---- */
  CHECK_EQ_U(ppu_rgb6(0, 0, 0), 0x00);
  CHECK_EQ_U(ppu_rgb6(255, 255, 255), 0x3f);
  CHECK_EQ_U(ppu_rgb6(255, 0, 0), 0x30);      /* pure red -> 11_00_00 */
  CHECK_EQ_U(ppu_rgb6(0, 255, 0), 0x0c);      /* pure green -> 00_11_00 */
  CHECK_EQ_U(ppu_rgb6(0, 0, 255), 0x03);
  CHECK_EQ_U(ppu_rgb6(128, 128, 128), 0x2a);  /* litenes 0x808080 */
  CHECK_EQ_U(ppu_rgb6(187, 0, 0), 0x20);      /* litenes 0xbb0000 */
  /* the truncation boundary is at 64: 63 is level 0, 64 is level 1 */
  CHECK_EQ_U(ppu_rgb6(63, 63, 63), 0x00);
  CHECK_EQ_U(ppu_rgb6(64, 64, 64), 0x15);
  CHECK_EQ_U(ppu_rgb6(191, 191, 191), 0x2a);
  CHECK_EQ_U(ppu_rgb6(192, 192, 192), 0x3f);

  /* ---- exact matches that matter (transcribed from the real clients) ---- */
  CHECK_EQ_U(lut_slot_for(lut, 0xeeeeee), 15);   /* typing white  -> white  */
  CHECK_EQ_U(lut_slot_for(lut, 0x000000), 0);    /* typing purple bg -> black */
  CHECK_EQ_U(lut_slot_for(lut, 0x808080), 7);    /* litenes light gray */
  CHECK_EQ_U(lut_slot_for(lut, 0x0000bb), 1);    /* litenes blue */
  CHECK_EQ_U(lut_slot_for(lut, 0xb7001e), 4);    /* litenes dark red */
  CHECK_EQ_U(lut_slot_for(lut, 0xc8c8c8), 15);   /* litenes light gray */
  CHECK_EQ_U(lut_slot_for(lut, 0x666666), 8);    /* litenes mid gray -> dk gray */

  /* ---- near matches: require the right hue family, not an exact slot ---- */
  {
    /* typing red 0xff0033 truncates to 11_00_00, one red level above the
     * palette's red, so red is the only sensible landing. */
    CHECK_EQ_U(lut_slot_for(lut, 0xff0033), 4);

    /* typing green and snake's board green both truncate to 00_11_00 */
    int g1 = lut_slot_for(lut, 0x00cc33);
    int g2 = lut_slot_for(lut, 0x0000ff00);
    CHECK(g1 == 2 || g1 == 10);
    CHECK(g2 == 2 || g2 == 10);

    /* NES dark magenta ties between red and magenta under these weights;
     * either is an honest degradation of a 6-bit target. */
    int m = lut_slot_for(lut, 0xbb006a);
    CHECK(m == 4 || m == 5);
    if (m == 5) show(0xbb006a, m);

    /* dark brown and very dark green are genuinely closer to black than to
     * brown/green at 2-2-2 -- that is the accepted colour degradation. */
    CHECK(lut_slot_for(lut, 0x7b2b00) == 0);
    CHECK(lut_slot_for(lut, 0x003e00) == 0);

    /* teal should land in the cyan/blue family */
    int t = lut_slot_for(lut, 0x0084c4);
    CHECK(t == 3 || t == 1);
  }

  /* ---- PSET packing ---- */
  CHECK_EQ_U(PPU_PSET_VAL(ppu_pal_rgb6[0], 0), 0x000u);   /* black */
  CHECK_EQ_U(PPU_PSET_VAL(ppu_pal_rgb6[15], 15), 0x3ffu); /* white */
  CHECK_EQ_U(PPU_PSET_VAL(0x30, 1), 0x301u);              /* FramePpuTb red */
  CHECK_EQ_U(PPU_PSET_VAL(0x0c, 2), 0x0c2u);              /* FramePpuTb green */
  /* the slot field must not bleed into rgb6 and vice versa */
  CHECK_EQ_U(PPU_PSET_VAL(0x3f, 15) & 0x3f0u, 0x3f0u);
  CHECK_EQ_U(PPU_PSET_VAL(0x3f, 15) & 0x00fu, 0x00fu);

  /* ---- the 16 slots are exactly the VGA set, in order ---- */
  CHECK_EQ_U(ppu_pal_rgb6[0], 0x00);   /* black       00_00_00 */
  CHECK_EQ_U(ppu_pal_rgb6[1], 0x02);   /* blue        00_00_10 */
  CHECK_EQ_U(ppu_pal_rgb6[2], 0x08);   /* green       00_10_00 */
  CHECK_EQ_U(ppu_pal_rgb6[3], 0x0a);   /* cyan        00_10_10 */
  CHECK_EQ_U(ppu_pal_rgb6[4], 0x20);   /* red         10_00_00 */
  CHECK_EQ_U(ppu_pal_rgb6[5], 0x22);   /* magenta     10_00_10 */
  CHECK_EQ_U(ppu_pal_rgb6[6], 0x24);   /* brown       10_01_00 */
  CHECK_EQ_U(ppu_pal_rgb6[7], 0x2a);   /* light gray  10_10_10 */
  CHECK_EQ_U(ppu_pal_rgb6[8], 0x15);   /* dark gray   01_01_01 */
  CHECK_EQ_U(ppu_pal_rgb6[9], 0x17);   /* light blue  01_01_11 */
  CHECK_EQ_U(ppu_pal_rgb6[10], 0x1d);  /* light green 01_11_01 */
  CHECK_EQ_U(ppu_pal_rgb6[11], 0x1f);  /* light cyan  01_11_11 */
  CHECK_EQ_U(ppu_pal_rgb6[12], 0x35);  /* light red   11_01_01 */
  CHECK_EQ_U(ppu_pal_rgb6[13], 0x37);  /* lt magenta  11_01_11 */
  CHECK_EQ_U(ppu_pal_rgb6[14], 0x3d);  /* yellow      11_11_01 */
  CHECK_EQ_U(ppu_pal_rgb6[15], 0x3f);  /* white       11_11_11 */
}
