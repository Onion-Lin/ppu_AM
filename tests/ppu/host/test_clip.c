/* Clipping tests.
 *
 * The cases are taken from rectangles the real clients actually submit, because
 * clipping is a correctness requirement (the PPU wraps its address rather than
 * clipping), not a nicety.
 *
 *   demo/include/io.h:51   screen_clear sends w = SCREEN_W = 320  (> 256)
 *   slider/main.c:10       sends 400x300                          (> both)
 *   am-tests/video.c:30    sends (w/N)x(h/N) blocks, always in range
 *   typing-game/game.c:84  8x16 character cells, in range
 */
#include "ppu_test.h"
#include "ppu.h"
#include "ppu_core.h"

static void expect_clip(int x, int y, int w, int h,
                        int ex, int ey, int ew, int eh) {
  ppu_rect_t r;
  int ok = ppu_clip_rect(x, y, w, h, &r);
  if (!ok) {
    ppu_t_fail++;
    printf("  FAIL clip(%d,%d,%d,%d): rejected, want %d,%d,%d,%d\n",
           x, y, w, h, ex, ey, ew, eh);
    return;
  }
  if (r.x != ex || r.y != ey || r.w != ew || r.h != eh) {
    ppu_t_fail++;
    printf("  FAIL clip(%d,%d,%d,%d) = %d,%d,%d,%d  want %d,%d,%d,%d\n",
           x, y, w, h, r.x, r.y, r.w, r.h, ex, ey, ew, eh);
    return;
  }
  ppu_t_ok++;
}

static void expect_reject(int x, int y, int w, int h) {
  ppu_rect_t r;
  if (ppu_clip_rect(x, y, w, h, &r)) {
    ppu_t_fail++;
    printf("  FAIL clip(%d,%d,%d,%d): accepted as %d,%d,%d,%d, want reject\n",
           x, y, w, h, r.x, r.y, r.w, r.h);
    return;
  }
  ppu_t_ok++;
}

/* Invariant that must hold for every accepted rectangle: it fits, and it is a
 * subset of what was asked for. */
static void check_invariant(int x, int y, int w, int h) {
  ppu_rect_t r;
  if (!ppu_clip_rect(x, y, w, h, &r)) return;
  CHECK(r.x >= 0 && r.y >= 0);
  CHECK(r.w > 0 && r.h > 0);
  CHECK(r.x + r.w <= PPU_FB_W);
  CHECK(r.y + r.h <= PPU_FB_H);
  /* the clipped rect must cover the part of the request that was on-screen */
  int ax0 = x > 0 ? x : 0, ay0 = y > 0 ? y : 0;
  int ax1 = x + w < PPU_FB_W ? x + w : PPU_FB_W;
  int ay1 = y + h < PPU_FB_H ? y + h : PPU_FB_H;
  if (ax0 < ax1 && ay0 < ay1) {
    CHECK_EQ_U(r.x, ax0);
    CHECK_EQ_U(r.y, ay0);
    CHECK_EQ_U(r.w, ax1 - ax0);
    CHECK_EQ_U(r.h, ay1 - ay0);
  }
}

void test_clip_suite(void) {
  /* ---- fully inside: unchanged ---- */
  expect_clip(0, 0, 256, 240, 0, 0, 256, 240);      /* whole screen */
  expect_clip(8, 16, 8, 8, 8, 16, 8, 8);            /* a snake tile */
  expect_clip(0, 0, 8, 16, 0, 0, 8, 16);            /* a typing char */
  expect_clip(0, 0, 1, 1, 0, 0, 1, 1);              /* single pixel */
  expect_clip(248, 232, 8, 8, 248, 232, 8, 8);      /* bottom-right corner */
  expect_clip(255, 239, 1, 1, 255, 239, 1, 1);      /* last pixel */

  /* ---- overhang on the right / bottom ---- */
  expect_clip(0, 0, 320, 1, 0, 0, 256, 1);          /* demo screen_clear line */
  expect_clip(0, 0, 400, 300, 0, 0, 256, 240);      /* slider's whole image */
  expect_clip(250, 230, 100, 100, 250, 230, 6, 10); /* corner overhang */
  expect_reject(256, 0, 10, 10);                    /* starts off-screen */
  expect_reject(0, 240, 10, 10);

  /* ---- negative origins ---- */
  expect_clip(-8, -8, 16, 16, 0, 0, 8, 8);
  expect_reject(-100, -100, 50, 50);                /* entirely off the top-left */
  expect_clip(-5, 0, 20, 10, 0, 0, 15, 10);

  /* ---- degenerate sizes ---- */
  expect_reject(0, 0, 0, 0);        /* this is AM's sync-only FBDRAW */
  expect_reject(10, 10, 0, 8);
  expect_reject(10, 10, 8, 0);
  expect_reject(10, 10, -4, 8);
  expect_reject(10, 10, 8, -4);

  /* ---- am-tests/video.c shape: 32x32 blocks over a 256x240 screen ---- */
  {
    int w = PPU_FB_W / 32, h = PPU_FB_H / 32;   /* 8 x 7 */
    for (int by = 0; by < 32; by++)
      for (int bx = 0; bx < 32; bx++) {
        ppu_rect_t r;
        int ok = ppu_clip_rect(bx * w, by * h, w, h, &r);
        if (bx * w >= PPU_FB_W || by * h >= PPU_FB_H) {
          CHECK(!ok);                            /* outside: must be dropped */
        } else {
          CHECK(ok);
          CHECK(r.x == bx * w && r.y == by * h);
          CHECK(r.x + r.w <= PPU_FB_W);
          CHECK(r.y + r.h <= PPU_FB_H);
        }
      }
  }

  /* ---- sweep: no input may ever produce an out-of-range result ---- */
  {
    const int xs[] = {-300, -256, -64, -8, -1, 0, 1, 8, 100, 248, 255, 256, 300};
    const int ws[] = {-4, 0, 1, 2, 8, 15, 16, 17, 64, 255, 256, 257, 400, 1024};
    for (unsigned i = 0; i < sizeof(xs)/sizeof(xs[0]); i++)
      for (unsigned j = 0; j < sizeof(ws)/sizeof(ws[0]); j++) {
        check_invariant(xs[i], -3, ws[j], 240);
        check_invariant(0, xs[i], 256, ws[j]);
        check_invariant(xs[i], 7, ws[j], ws[j]);
      }
  }
}
