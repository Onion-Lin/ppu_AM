/* Frame encoding tests.
 *
 * The expected bytes below are transcribed from the RTL, not invented:
 *   SyncSpi shifts hdr in MSB first and hdr[7] ends up as the rw bit
 *     (SyncSpi.sv:83  hdr <= {hdr[6:0], mosi_d})
 *   wsh is filled MSB first and reg_we latches {wsh[30:0], mosi_d}
 *     (SyncSpi.sv:85-88)
 * so the wire order after the header byte is data[31], data[30], ... data[0].
 *
 * The QSPI side is the other half: hal_qspi_write_32 shifts its word MSB first
 * (the shipped driver left-justifies even a single byte: TXFIFO = data << 24),
 * so w0 must start with the header byte. */
#include "ppu_test.h"
#include "ppu.h"

#include <string.h>

/* Rebuild the 8 bytes the transport will hand the QSPI, so the test can look at
 * exactly what goes on the wire. */
static void frame_bytes(uint32_t reg, uint32_t data, uint8_t out[8]) {
  uint32_t w0 = ppu_frame_w0(reg, data);
  uint32_t w1 = ppu_frame_w1(data);
  for (int i = 0; i < 4; i++) out[i]     = (uint8_t)(w0 >> (24 - 8 * i));
  for (int i = 0; i < 4; i++) out[4 + i] = (uint8_t)(w1 >> (24 - 8 * i));
}

void test_frame_suite(void) {
  /* ---- header byte layout: {rw, 3'b000, reg[3:0]}, rw=0 for a write ---- */
  {
    uint8_t b[8];
    frame_bytes(PPU_R_VSET, 0, b);
    CHECK_EQ_U(b[0], 0x09);           /* rw=0, reg=9  */
    frame_bytes(PPU_R_CMD, 0, b);
    CHECK_EQ_U(b[0], 0x00);           /* rw=0, reg=0  */
    frame_bytes(PPU_R_PSET, 0, b);
    CHECK_EQ_U(b[0], 0x08);           /* rw=0, reg=8  */
    frame_bytes(PPU_R_IRQE, 0, b);
    CHECK_EQ_U(b[0], 0x0a);           /* rw=0, reg=10 */

    /* rw must never leak into the low nibble, and reg must stay in 4 bits */
    frame_bytes(PPU_R_X, 0xffffffffu, b);
    CHECK_EQ_U(b[0] & 0x0fu, 0x01u);
    CHECK_EQ_U(b[0] & 0x80u, 0x00u);
  }

  /* ---- data must come out MSB first, in 4 whole bytes ---- */
  {
    uint8_t b[8];
    frame_bytes(PPU_R_VSET, 0x89abcde0u, b);
    CHECK_EQ_U(b[1], 0x89);
    CHECK_EQ_U(b[2], 0xab);
    CHECK_EQ_U(b[3], 0xcd);
    CHECK_EQ_U(b[4], 0xe0);

    frame_bytes(PPU_R_VSET, 0x00000001u, b);
    CHECK_EQ_U(b[1], 0x00);
    CHECK_EQ_U(b[2], 0x00);
    CHECK_EQ_U(b[3], 0x00);
    CHECK_EQ_U(b[4], 0x01);

    frame_bytes(PPU_R_VSET, 0x5a5aa5a5u, b);   /* the FramePpuTb pattern */
    CHECK_EQ_U(b[1], 0x5a);
    CHECK_EQ_U(b[2], 0x5a);
    CHECK_EQ_U(b[3], 0xa5);
    CHECK_EQ_U(b[4], 0xa5);
  }

  /* ---- only the first 5 bytes matter; 3..8 are filler ---- */
  {
    uint8_t b[8];
    frame_bytes(PPU_R_CMD, 0x00000002u, b);
    /* w1 = data << 24 puts data[7:0] in byte 4 and zeroes bytes 5..7 */
    CHECK_EQ_U(b[4], 0x02);
    CHECK_EQ_U(b[5], 0x00);
    CHECK_EQ_U(b[6], 0x00);
    CHECK_EQ_U(b[7], 0x00);
  }

  /* ---- the split must concatenate: 24 bits in w0 + 8 in w1 ---- */
  for (uint32_t d = 0; d < 8; d++) {
    uint32_t pattern = 0x01010101u << d;
    uint8_t b[8];
    frame_bytes(PPU_R_WH, pattern, b);
    uint32_t back = ((uint32_t)b[1] << 24) | ((uint32_t)b[2] << 16) |
                    ((uint32_t)b[3] << 8) | (uint32_t)b[4];
    CHECK_EQ_U(back, pattern);
  }

  /* ---- every register index survives a round trip through the encoder ---- */
  for (uint32_t r = 0; r < 16; r++) {
    uint8_t b[8];
    frame_bytes(r, 0x12345678u, b);
    CHECK_EQ_U(b[0] & 0x0fu, r);
  }

  /* ---- CLR / FILL command values ---- */
  {
    uint8_t b[8];
    frame_bytes(PPU_R_CMD, PPU_CMD_CLR, b);
    CHECK_EQ_U(b[4], PPU_CMD_CLR);
    frame_bytes(PPU_R_CMD, PPU_CMD_FILL, b);
    CHECK_EQ_U(b[4], PPU_CMD_FILL);
    frame_bytes(PPU_R_CMD, PPU_CMD_NOP, b);
    CHECK_EQ_U(b[4], PPU_CMD_NOP);
  }

  /* ---- WH packing: data[17:9]=W, data[8:0]=H ---- */
  {
    uint32_t w = 64, h = 8;
    uint32_t wh = (w << 9) | h;
    CHECK_EQ_U(wh, (64u << 9) | 8u);
    uint8_t b[8];
    frame_bytes(PPU_R_WH, wh, b);
    /* wh = 0x00008008 -> bytes 00 00 80 08 */
    CHECK_EQ_U(b[1], 0x00);
    CHECK_EQ_U(b[2], 0x00);
    CHECK_EQ_U(b[3], 0x80);
    CHECK_EQ_U(b[4], 0x08);
  }

  /* ---- PSET packing: data[9:4]=RGB666, data[3:0]=slot ---- */
  {
    /* FramePpuTb writes 32'h301 for slot 1 = red 11_00_00 */
    uint32_t v = PPU_PSET_VAL(0x30u, 1u);
    CHECK_EQ_U(v, 0x301u);
    uint32_t v2 = PPU_PSET_VAL(0x0cu, 2u);   /* slot 2 = green 00_11_00 */
    CHECK_EQ_U(v2, 0x0c2u);
  }

  /* ---- VSET default must be stretch + x2 = 0x3 ---- */
  CHECK_EQ_U(PPU_VSET_DEFAULT, 0x00000003u);
}
