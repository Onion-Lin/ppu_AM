/* Smoke test: prove the harness itself works.  Real suites arrive with T1+. */
#include "ppu_test.h"
#include "ppu.h"
#include "ppu_core.h"

void test_sanity_suite(void) {
  /* geometry constants must match the real silicon */
  CHECK_EQ_U(PPU_FB_W, 256);
  CHECK_EQ_U(PPU_FB_H, 240);
  CHECK_EQ_U(PPU_FB_VMEMSZ, 30720);

  /* the 16 palette entries must be distinct after 2-2-2 truncation */
  for (unsigned i = 0; i < PPU_PAL_SLOTS; i++)
    for (unsigned j = i + 1; j < PPU_PAL_SLOTS; j++)
      CHECK(ppu_pal_rgb6[i] != ppu_pal_rgb6[j]);

  /* every palette entry must be representable in 6 bits */
  for (unsigned i = 0; i < PPU_PAL_SLOTS; i++)
    CHECK(ppu_pal_rgb6[i] <= 0x3fu);

  /* the LUT must map all 64 inputs to a valid slot */
  uint8_t lut[PPU_RGB6_MAX];
  ppu_pal_lut_build(lut);
  for (unsigned v = 0; v < PPU_RGB6_MAX; v++)
    CHECK(lut[v] < PPU_PAL_SLOTS);
}
