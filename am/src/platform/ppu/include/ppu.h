/* ppu.h -- software side of the PPU register interface.
 *
 * The PPU is reached through a 40-bit SPI frame:
 *     header[7:0] = {rw, 3'b000, reg[3:0]}   then  data[31:0], MSB first.
 * On the StarrySky board the SPI master is the QSPI controller, and the only
 * documented way to push bytes is a transfer whose length is a whole number of
 * bytes.  40 bits is 5 bytes, but every length the shipped driver ever uses is
 * a power of two, so we ship 8 bytes and rely on the PPU ignoring the extra
 * clocks (SyncSpi stops counting at bitcnt 63 and latches reg_we at bit 39).
 */
#ifndef PPU_H
#define PPU_H

#include <stdint.h>

/* ---- PPU registers (the 4-bit index carried in the SPI header) ---- */
enum {
  PPU_R_CMD   = 0,   /* [1:0] 0=NOP 1=CLR 2=FILL */
  PPU_R_X     = 1,   /* start X, low 8 bits used */
  PPU_R_Y     = 2,   /* start Y, low 8 bits used */
  PPU_R_WH    = 3,   /* [17:9]=W [8:0]=H */
  PPU_R_COLOR = 4,   /* low 4 bits = palette index */
  PPU_R_STATUS= 5,   /* read: {frame_ctr[15:0], 14'b0, busy, irq} */
  PPU_R_PSET  = 8,   /* [9:4]=RGB666 [3:0]=slot */
  PPU_R_VSET  = 9,
  PPU_R_IRQE  = 10,
};

/* ---- framebuffer geometry (the real silicon, not a choice) ---- */
#define PPU_FB_W      256
#define PPU_FB_H      240
#define PPU_FB_BPP    4                      /* indexed colour */
#define PPU_FB_VMEMSZ (PPU_FB_W * PPU_FB_H * PPU_FB_BPP / 8)

/* ---- commands ---- */
#define PPU_CMD_NOP  0
#define PPU_CMD_CLR  1
#define PPU_CMD_FILL 2

/* ---- VSET fields ---- */
#define PPU_VSET_STRETCH   (1u << 0)   /* 1 = horizontal x2.5, else integer scale */
#define PPU_VSET_SCALE_S   1          /* [2:1] 0=x1 1=x2 2=x4 3=x8 */
#define PPU_VSET_XOFF_S    3          /* [12:3] */
#define PPU_VSET_YOFF_S    13         /* [21:13] */
#define PPU_VSET_TESTPAT   (1u << 31)

/* stretch + x2 lands exactly on 640x480: 256*2.5=640, 240*2=480, no borders. */
#define PPU_VSET_DEFAULT (PPU_VSET_STRETCH | (1u << PPU_VSET_SCALE_S))

/* ---- IRQE ---- */
#define PPU_IRQE_DONE  1u
#define PPU_IRQE_VSYNC 2u

/* ---- QSPI framing facts ----
 * How one register write becomes one transfer.  The board-side numbers
 * (register base, CPU clock, divider) belong in ppu_board.h, not here. */
#define PPU_QSPI_LEN_8B    (8u * 0x80000u)   /* LEN = num_bytes * 0x80000 */
#define PPU_QSPI_START     258u              /* 0x102, per the SDK driver */

/* ---- palette: VGA/EGA default 16, quantised to the PPU's 6-bit 2-2-2 ---- */
#define PPU_PAL_SLOTS 16
#define PPU_RGB6_MAX  64          /* 4 levels per channel */

/* Pack an R8 (PSET) write: data[9:4] = RGB666, data[3:0] = slot. */
#define PPU_PSET_VAL(rgb6, slot)  (((uint32_t)(rgb6) << 4) | (uint32_t)((slot) & 0xfu))

/* ---- pure helpers implemented in ppu_core.c ---- */
#ifdef __cplusplus
extern "C" {
#endif

/* Truncate an RGB888 pixel to the PPU's 6-bit 2-2-2 form. */
static inline uint32_t ppu_rgb6(uint32_t r, uint32_t g, uint32_t b) {
  return ((r >> 6) << 4) | ((g >> 6) << 2) | (b >> 6);
}

/* The 40-bit frame, split into the two 32-bit words a QSPI write_32x2 takes.
 * The controller shifts each word MSB first, so w0 carries the header byte and
 * the top 24 data bits, w1 carries the low 8 data bits plus 3 filler bytes. */
static inline uint32_t ppu_frame_w0(uint32_t reg, uint32_t data) {
  uint32_t header = (reg & 0xfu);          /* rw = 0 -> write */
  return (header << 24) | ((data >> 8) & 0x00ffffffu);
}
static inline uint32_t ppu_frame_w1(uint32_t data) {
  return (data << 24);                     /* low byte + 3 don't-care bytes */
}

#ifdef __cplusplus
}
#endif

#endif /* PPU_H */
