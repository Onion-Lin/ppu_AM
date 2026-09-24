/* QSPI transport: the only code that touches the SPI master.
 *
 * Everything above this file speaks in PPU registers; this file turns one
 * register write into one 8-byte QSPI transfer.  Two details matter:
 *
 *   1. The 8-byte frame.  SyncSpi latches reg_we at bit 39 and stops counting
 *      at 63, so the real 40-bit frame rides inside the 8-byte write_32x2 path
 *      the shipped driver already proves works, and the trailing 3 bytes are
 *      discarded.  See docs/findings.md.
 *
 *   2. CS must stay high between transfers.  SyncSpi samples cs_n through two
 *      flops; if CS drops too soon the synchronizer misses it, bitcnt never
 *      resets, and the frame is silently dropped.  hal_qspi_write_32x2's
 *      single START + wait_idle() is assumed to satisfy this -- it is one of the
 *      items the board probe is meant to confirm.
 *
 * Delays are computed, not measured: FillEngine takes ~1.05 PPU clocks per
 * framebuffer word, and a 3-frame SPI transfer already burns ~1200 PPU clocks,
 * so a small fill finishes long before the next command can be issued.  Only a
 * big rectangle needs an explicit wait. */

#include "ppu.h"
#include "ppu_core.h"
#include "ppu_board.h"
#include "ppu_hw.h"

/* PPU pixel clock, fixed by the PPU board's own crystal. */
#define PPU_CLK_HZ 25175000u

/* Frames per second the VGA timing generator produces. */
#define PPU_FRAME_CLKS (525u * 800u)

/* A rough cost of one iteration of the delay loop, in instructions.  Used only
 * to size waits that are already generous, so it does not need to be exact. */
#define DELAY_LOOP_COST 4u

static void delay_loop(volatile uint32_t n) {
  while (n--) ;
}

/* Wait for a number of PPU clocks, using the CPU as the time base. */
static void ppu_delay_ppu_clocks(uint32_t clks) {
  /* ns = clks / PPU_CLK_HZ * 1e9 ; cpu cycles = ns * PPU_CPU_HZ / 1e9 */
  uint64_t cyc = (uint64_t)clks * PPU_CPU_HZ / PPU_CLK_HZ;
  delay_loop((uint32_t)(cyc / DELAY_LOOP_COST + 1u));
}

static void qspi_init(void) {
  QSPI_STATUS = 0x10;        /* mirror of hal_qspi_init() */
  QSPI_STATUS = 0x00;
  QSPI_INTCFG = 0;
  QSPI_DUM    = 0;
  QSPI_LEN    = 0;
  QSPI_CLKDIV = PPU_QSPI_CLKDIV;
}

static void qspi_wait_idle(void) {
  while ((QSPI_STATUS & 0xffffu) != 1u) ;
}

/* One 8-byte transfer: two words, MSB first each, CS asserted once. */
static void qspi_write8(uint32_t w0, uint32_t w1) {
  QSPI_LEN     = PPU_QSPI_LEN_8B;
  QSPI_TXFIFO  = w0;
  QSPI_TXFIFO  = w1;
  QSPI_STATUS  = PPU_QSPI_START;
  qspi_wait_idle();
}

/* Burn PPU-clock time by clocking dummy frames at a reserved register.
 * R6/R7 are documented as "ignored", so this cannot disturb PPU state. */
static void ppu_burn_frames(uint32_t n) {
  while (n--) qspi_write8(ppu_frame_w0(6, 0), ppu_frame_w1(0));
}

/* Words FillEngine will write for a rect: each row touches (w/16 + 2) words
 * (the +2 covers the partial words at both ends). */
static uint32_t fill_words(int w, int h) {
  return (uint32_t)h * (((uint32_t)w >> 4) + 2u);
}

/* After issuing a fill, wait unless the next command's own transfer time
 * already covers the engine's work.  A 3-frame transfer is ~1200 PPU clocks. */
#define PPU_HIDDEN_CLKS 1200u

static void ppu_settle(int w, int h) {
  uint32_t words = fill_words(w, h);
  uint32_t need  = words + (words >> 3);          /* ~1.05x for scan priority */
  if (need > PPU_HIDDEN_CLKS) ppu_delay_ppu_clocks(need - PPU_HIDDEN_CLKS);
}

/* Wait for a fill with no following command to hide behind. */
static void ppu_settle_full(int w, int h) {
  uint32_t words = fill_words(w, h);
  ppu_delay_ppu_clocks(words + (words >> 3));
}

/* ---------------------------------------------------------------- public API */

void ppu_hw_init(void) {
  qspi_init();

  /* VSET must land before the first VSYNC: r_vset resets to 0 while ScanPath's
   * internal reset config is x2 with x_off=64, and the first VSYNC would latch
   * the wrong geometry (README 15.7).  stretch + x2 fills 640x480 exactly. */
  ppu_wr(PPU_R_VSET, PPU_VSET_DEFAULT);
  ppu_wr(PPU_R_IRQE, 0);

  /* Palette shadow, all 16 slots.  Takes effect at the next VSYNC. */
  for (unsigned s = 0; s < PPU_PAL_SLOTS; s++) {
    ppu_wr(PPU_R_PSET, PPU_PSET_VAL(ppu_pal_rgb6[s], s));
  }

  /* Wait out one frame period so that VSYNC latches VSET and the palette. */
  ppu_delay_ppu_clocks(PPU_FRAME_CLKS * 2);

  /* SRAM powers up with undefined contents (README 15.9). */
  ppu_wr(PPU_R_COLOR, 0);
  ppu_wr(PPU_R_CMD, PPU_CMD_CLR);
  ppu_settle_full(PPU_FB_W, PPU_FB_H);

  /* Silence the bus before the first real draw. */
  ppu_burn_frames(2);
}

void ppu_wr(uint32_t reg, uint32_t data) {
  qspi_write8(ppu_frame_w0(reg, data), ppu_frame_w1(data));
}

void ppu_fill(int x, int y, int w, int h, int slot) {
  ppu_wr(PPU_R_X, (uint32_t)x);
  ppu_wr(PPU_R_Y, (uint32_t)y);
  ppu_wr(PPU_R_WH, ((uint32_t)w << 9) | (uint32_t)h);
  ppu_wr(PPU_R_COLOR, (uint32_t)slot);
  ppu_wr(PPU_R_CMD, PPU_CMD_FILL);
}

/* Align to a frame boundary.  Without a readable frame counter this is a timed
 * wait for one frame period, which drifts against the PPU's own crystal; see
 * PLAN.md for the GPIO-vsync alternative. */
void ppu_vblank(void) {
  ppu_delay_ppu_clocks(PPU_FRAME_CLKS);
}

void ppu_fill_settle(int w, int h)  { ppu_settle(w, h); }
void ppu_fill_settle_full(int w, int h) { ppu_settle_full(w, h); }
