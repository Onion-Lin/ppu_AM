/* AM GPU device on top of the PPU.
 *
 * AM hands us a block of RGB888 pixels; the PPU only fills a rectangle with one
 * 4-bit palette index.  So this takes the block's first pixel, quantises it, and
 * issues one FILL.  The block becomes a flat colour.
 *
 * That is the accepted trade-off, and it has a useful side effect: the number of
 * FILLs per frame equals the number of FBDRAW calls, not the pixel count.  A
 * program that submits one full-frame bitmap costs one FILL; a program that
 * submits 240 scanlines costs 240.
 *
 * Clipping is not optional: the PPU wraps its address instead of clipping
 * (README 15.5), and clients really do overshoot -- demo's screen_clear sends
 * 320 pixels against a 256-wide framebuffer, slider sends 400x300. */

#include <am.h>
#include <ppu.h>
#include <ppu_core.h>
#include <ppu_hw.h>

static AM_GPU_CONFIG_T cfg;
static uint8_t pal_lut[PPU_RGB6_MAX];

void __am_gpu_init(void) {
  cfg.present   = true;
  cfg.has_accel = true;      /* the FILL engine really is an accelerator */
  cfg.width     = PPU_FB_W;
  cfg.height    = PPU_FB_H;
  cfg.vmemsz    = PPU_FB_VMEMSZ;

  ppu_pal_lut_build(pal_lut);
  ppu_hw_init();
}

void __am_gpu_config(AM_GPU_CONFIG_T *c) { *c = cfg; }

void __am_gpu_status(AM_GPU_STATUS_T *st) { st->ready = true; }

void __am_gpu_fbdraw(AM_GPU_FBDRAW_T *ctl) {
  /* w==0/h==0 with pixels==NULL is AM's "flush this frame" idiom (snake's
   * refresh(), demo's screen_refresh()).  Nothing to draw, just sync. */
  if (ctl->w <= 0 || ctl->h <= 0) {
    if (ctl->sync) ppu_vblank();
    return;
  }

  ppu_rect_t r;
  if (!ppu_clip_rect(ctl->x, ctl->y, ctl->w, ctl->h, &r)) {
    if (ctl->sync) ppu_vblank();
    return;
  }

  /* First pixel only.  One load, so it does not matter that pixels may live in
   * slow PSRAM. */
  uint32_t c = ((volatile uint32_t *)ctl->pixels)[0];
  uint32_t v = ppu_rgb6((c >> 16) & 0xffu, (c >> 8) & 0xffu, c & 0xffu);

  ppu_fill(r.x, r.y, r.w, r.h, pal_lut[v]);

  if (ctl->sync) {
    /* Last draw of the batch: nothing follows to hide the engine's work, so
     * wait for it, then align to the frame boundary. */
    ppu_fill_settle_full(r.w, r.h);
    ppu_vblank();
  } else {
    /* More draws are coming, and their own SPI time covers a small fill. */
    ppu_fill_settle(r.w, r.h);
  }
}
