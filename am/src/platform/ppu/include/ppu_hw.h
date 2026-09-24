/* Transport API: what the AM platform layer is allowed to call.
 *
 * ppu_core.* is pure logic (no I/O).  ppu_qspi.c implements these against the
 * board's QSPI controller.  Keeping the boundary explicit means the render path
 * can be unit-tested and re-targeted without touching the AM glue. */
#ifndef PPU_HW_H
#define PPU_HW_H

#include "ppu.h"

/* One-time bring-up: QSPI config, VSET, palette, clear.  Call before drawing. */
void ppu_hw_init(void);

/* Write one PPU register. */
void ppu_wr(uint32_t reg, uint32_t data);

/* Issue a FILL.  The rectangle must already be clipped to the framebuffer. */
void ppu_fill(int x, int y, int w, int h, int slot);

/* After a fill that is followed by more work: waits only if the engine is
 * slower than the next command's transfer time. */
void ppu_fill_settle(int w, int h);

/* After the last fill of a batch: always waits for completion. */
void ppu_fill_settle_full(int w, int h);

/* Align to a frame boundary (timed; see PLAN.md). */
void ppu_vblank(void);

#endif
