/* Verilator harness: the real PPU RTL, driven from C++.
 *
 * Why this exists: everything else in the task list is either pure logic (host
 * suites) or a platform layer that cannot run without the board.  This is the
 * only place the pipeline meets the actual silicon behaviour -- arbitration,
 * nibble masking, VSYNC palette latching, and above all whether the "8-byte
 * frame" trick works.
 *
 * The SPI master here sends frames using ppu_frame_w0/w1 from ppu.h, exactly as
 * ppu_qspi.c will on the board.  If the encoding is wrong these tests fail.
 *
 * The sequence is modelled on the RTL testbench's FramePpuTb so the two agree:
 * select the design through a payload-shaped io vector, do a register
 * read/write round trip, then draw a red and a green rectangle and capture a
 * scanline. */

#include <cstdio>
#include <cstdint>

#include "VPpu.h"
#include "ppu.h"
#include "ppu_core.h"

/* ---- payload bit map (rtl/Ppu.sv) ---- */
static const int B_SCLK = 0;
static const int B_CSN  = 1;
static const int B_MOSI = 2;
static const int B_MISO = 3;
static const int B_IRQ  = 4;
static const int B_VSYNC = 5;
static const int B_HSYNC = 6;
static const int B_RGB  = 7;   /* [12:7] */

/* Timing, matching FramePpuTb: PPU period 10 ns, SPI period 100 ns, i.e. 10
 * PPU clocks per SPI bit.  That is the "10x oversampling" the RTL asks for. */
static const int PPU_PERIOD_NS = 10;
static const int SCK_HALF_NS   = 50;      /* 5 PPU clocks */
static const int PPU_PER_SCK_HALF = SCK_HALF_NS / PPU_PERIOD_NS;

static int g_fail = 0;
static int g_ok = 0;

#define TCHECK(cond)                                                     \
  do {                                                                   \
    if (cond) { g_ok++; }                                                \
    else {                                                               \
      g_fail++;                                                          \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
    }                                                                    \
  } while (0)

static void check_eq(uint32_t a, uint32_t b, int line, const char *sa, const char *sb) {
  if (a == b) { g_ok++; return; }
  g_fail++;
  std::printf("  FAIL line %d  %s == %s  (got 0x%x, want 0x%x)\n", line, sa, sb, a, b);
}

#define TCHECK_EQ(a, b) check_eq((uint32_t)(a), (uint32_t)(b), __LINE__, #a, #b)

struct Ppu {
  VPpu *top;
  /* io_in/io_out are 66-bit VlWide<3>.  Everything this platform uses lives in
   * bits 0..12, so the first storage word is enough.  (VlWide exposes its words
   * through m_storage; there is no shift operator for the wide type.) */
  uint32_t in_word() const { return top->io_in.m_storage[0]; }
  uint32_t out_word() const { return top->io_out.m_storage[0]; }

  void eval() { top->eval(); }

  /* Advance one PPU clock. */
  void clk() {
    top->clock = 0;
    eval();
    top->clock = 1;
    eval();
  }
  void clk(int n) { for (int i = 0; i < n; i++) clk(); }

  void set_in(int bit, int v) {
    uint32_t w = in_word();
    if (v) w |= (1u << bit);
    else   w &= ~(1u << bit);
    top->io_in.m_storage[0] = w;
    top->io_in.m_storage[1] = 0;
    top->io_in.m_storage[2] = 0;
    eval();
  }

  int out(int bit) const { return (int)((out_word() >> bit) & 1u); }
  int rgb() const { return (int)((out_word() >> B_RGB) & 0x3fu); }

  /* ---- SPI master, mirroring FramePpuTb's tasks ---- */

  void sbit(uint8_t b) {
    set_in(B_SCLK, 0);
    set_in(B_MOSI, b & 1u);
    clk(PPU_PER_SCK_HALF);
    set_in(B_SCLK, 1);
    clk(PPU_PER_SCK_HALF);
  }

  /* Like sbit(), but samples MISO on the way.  A read frame still has to clock
   * out its header bits on MOSI -- including the rw bit -- so the bit to send is
   * a parameter, not a constant 0. */
  uint8_t sbit_rd(uint8_t b) {
    set_in(B_SCLK, 0);
    set_in(B_MOSI, b & 1u);
    clk(PPU_PER_SCK_HALF);
    set_in(B_SCLK, 1);
    eval();                     /* let the edge settle before sampling */
    uint8_t got = (uint8_t)out(B_MISO);
    clk(PPU_PER_SCK_HALF);
    return got;
  }

  /* Raising CS is not enough: SyncSpi samples cs_n through two flops, so CS must
   * stay high for a few PPU clocks or the synchronizer never latches it and the
   * next frame's falling edge is invisible -- bitcnt then never resets and the
   * whole frame is discarded.  Always clock after deasserting. */
  void cs_low()  { set_in(B_CSN, 0); clk(4); }
  void cs_high() { clk(4); set_in(B_CSN, 1); clk(3); }

  /* Ship the 8-byte frame that the QSPI controller would send: two 32-bit
   * words, each shifted MSB first, so 8 bytes on the wire.  The PPU only
   * consumes the first 5; the rest must be harmless. */
  void frame(uint32_t w0, uint32_t w1) {
    cs_low();
    for (int i = 31; i >= 0; i--) sbit((w0 >> i) & 1u);
    for (int i = 31; i >= 0; i--) sbit((w1 >> i) & 1u);
    clk(4);                        /* let reg_we land before CS rises */
    cs_high();
  }

  /* A register write using the platform's own encoder. */
  void wr(uint32_t reg, uint32_t data) {
    frame(ppu_frame_w0(reg, data), ppu_frame_w1(data));
  }

  /* A register read: same 8 bytes, but with rw=1 in the header.  MISO starts on
   * the falling edge after bit 8, so bytes 1..4 carry the 32-bit value and byte
   * 0 is noise. */
  uint32_t rd(uint32_t reg) {
    uint32_t w0 = ppu_frame_w0(reg, 0) | 0x80000000u;   /* set rw */
    cs_low();
    uint32_t v = 0;
    int bit_index = 0;                                  /* 0..63 on the wire */
    for (int word = 0; word < 2; word++) {
      uint32_t w = word ? ppu_frame_w1(0) : w0;
      for (int i = 31; i >= 0; i--, bit_index++) {
        uint8_t got = sbit_rd((uint8_t)((w >> i) & 1u));
        /* data bit k arrives during wire bit 8+k */
        if (bit_index >= 8 && bit_index < 40) {
          v = (v << 1) | got;
        }
      }
    }
    clk(4);
    cs_high();
    return v;
  }

  void reset_chip() {
    top->reset = 1;
    set_in(B_SCLK, 0);
    set_in(B_CSN, 1);      /* idle high */
    set_in(B_MOSI, 0);
    clk(20);
    top->reset = 0;
    clk(6);
  }
};

/* ------------------------------------------------------------------ tests */

static void test_reset_quiescent(Ppu &p) {
  std::printf("[ reset state            ] ");
  /* RGB black, no IRQ, and HSYNC must be alive */
  TCHECK_EQ(p.rgb(), 0);
  TCHECK_EQ(p.out(B_IRQ), 0);

  int toggles = 0, hp = p.out(B_HSYNC);
  for (int c = 0; c < 2000; c++) {
    p.clk();
    if (p.out(B_HSYNC) != hp) toggles++;
    hp = p.out(B_HSYNC);
  }
  TCHECK(toggles >= 2);
  if (toggles >= 2) std::printf("ok (hsync toggles %d times in 2000 clk)\n", toggles);
  else std::printf("FAILED\n");
}

static void test_roundtrip_8byte(Ppu &p) {
  std::printf("[ 8-byte frame roundtrip ] ");
  /* FramePpuTb's first assertion: write a magic to R9 and read it back.  Doing
   * it through the 8-byte path is what proves the trick. */
  p.wr(PPU_R_VSET, 0x5a5aa5a5u);
  uint32_t got = p.rd(PPU_R_VSET);
  TCHECK_EQ(got, 0x5a5aa5a5u);

  /* also check the low and high halves separately */
  p.wr(PPU_R_VSET, 0x00000001u);
  TCHECK_EQ(p.rd(PPU_R_VSET), 0x00000001u);
  p.wr(PPU_R_VSET, 0x80000000u);
  TCHECK_EQ(p.rd(PPU_R_VSET), 0x80000000u);
  p.wr(PPU_R_VSET, 0xffffffffu);
  TCHECK_EQ(p.rd(PPU_R_VSET), 0xffffffffu);
  std::printf(g_fail ? "FAILED\n" : "ok\n");
}

/* Extra clocks past bit 39 must be discarded.  Send 12 bytes instead of 8 and
 * confirm the same register still takes the same value. */
static void test_overlength_frame(Ppu &p) {
  std::printf("[ overlong frame ignored  ] ");
  p.wr(PPU_R_VSET, 0x00000000u);

  for (int extra_bytes = 3; extra_bytes <= 5; extra_bytes++) {
    uint32_t w0 = ppu_frame_w0(PPU_R_VSET, 0x12345678u);
    uint32_t w1 = ppu_frame_w1(0x12345678u);
    p.cs_low();
    for (int i = 31; i >= 0; i--) p.sbit((w0 >> i) & 1u);
    for (int i = 31; i >= 0; i--) p.sbit((w1 >> i) & 1u);
    for (int b = 0; b < extra_bytes; b++)
      for (int i = 7; i >= 0; i--) p.sbit((0xa5u >> i) & 1u);   /* filler */
    p.clk(4);
    p.cs_high();
    uint32_t got = p.rd(PPU_R_VSET);
    if (got != 0x12345678u) {
      g_fail++;
      std::printf("\n  FAIL +%d filler bytes: got 0x%x\n", extra_bytes, got);
    } else {
      g_ok++;
    }
  }
  std::printf(g_fail ? "FAILED\n" : "ok (up to 5 filler bytes tolerated)\n");
}

static void test_palette_latch(Ppu &p) {
  std::printf("[ palette + VSYNC latch  ] ");
  /* x2 centered, pattern off, done IRQ on -- same as FramePpuTb */
  p.wr(PPU_R_VSET, 0x00000202u);
  p.wr(PPU_R_IRQE, PPU_IRQE_DONE);
  p.wr(PPU_R_PSET, PPU_PSET_VAL(0x30u, 1u));   /* slot 1 = red   11_00_00 */
  p.wr(PPU_R_PSET, PPU_PSET_VAL(0x0cu, 2u));   /* slot 2 = green 00_11_00 */

  /* wait for a VSYNC so the palette goes live (README 11: pal_live latches on
   * the raw falling edge of VSYNC) */
  int guard = 0;
  while (p.out(B_VSYNC) != 0 && guard++ < 600000) p.clk();
  TCHECK(guard < 600000);       /* found a VSYNC */
  std::printf("vsync after %d clk; ", guard);
}

/* Draw two 64x8 rectangles and capture a scanline, exactly as FramePpuTb does.
 * Returns the number of red pixels seen (should be 128 at x2 scaling). */
static int draw_and_capture(Ppu &p) {
  /* red rect (0,0,64,8) */
  p.wr(PPU_R_X, 0);
  p.wr(PPU_R_Y, 0);
  p.wr(PPU_R_WH, (64u << 9) | 8u);
  p.wr(PPU_R_COLOR, 1);
  p.wr(PPU_R_CMD, PPU_CMD_FILL);

  /* green rect (64,0,64,8): X and COLOR change, Y and WH do not */
  p.wr(PPU_R_X, 64);
  p.wr(PPU_R_COLOR, 2);
  p.wr(PPU_R_CMD, PPU_CMD_FILL);

  /* the fills are ~64 word writes; FramePpuTb waits 200 clocks here */
  p.clk(200);
  TCHECK_EQ(p.out(B_IRQ), 1);      /* done IRQ must be asserted */

  /* wait for the VSYNC that latches VSET + palette */
  int guard = 0;
  while (p.out(B_VSYNC) != 0 && guard++ < 600000) p.clk();

  /* count 39 HSYNC falling edges to land inside the rectangle's rows */
  int falls = 0, hp = p.out(B_HSYNC);
  while (falls < 39) {
    p.clk();
    if (p.out(B_HSYNC) == 0 && hp) falls++;
    hp = p.out(B_HSYNC);
  }

  int red = 0, green = 0, other = 0, firstnz = -1;
  for (int c = 0; c < 800; c++) {
    p.clk();
    int px = p.rgb();
    if (px != 0 && firstnz < 0) firstnz = c;
    if (firstnz >= 0) {
      if (px == 0x30) red++;
      else if (px == 0x0c) green++;
      else other++;
    }
  }
  std::printf("firstnz=%d red=%d green=%d other=%d\n", firstnz, red, green, other);
  return red;
}

static void test_fill_and_capture(Ppu &p) {
  std::printf("[ fill + scanline        ] ");
  int red = draw_and_capture(p);
  /* x2 scaling turns 64 framebuffer pixels into 128 VGA pixels */
  TCHECK_EQ(red, 128);
  std::printf(g_fail ? "FAILED\n" : "ok\n");
}

/* The path __am_gpu_fbdraw will actually take: clip, then one FILL. */
static void test_fbdraw_path(Ppu &p) {
  std::printf("[ fbdraw clip+fill path  ] ");
  /* demo's screen_clear submits 320 wide against a 256 framebuffer */
  ppu_rect_t r;
  int ok = ppu_clip_rect(0, 10, 320, 1, &r);
  TCHECK(ok);
  TCHECK_EQ(r.w, 256);

  p.wr(PPU_R_X, r.x);
  p.wr(PPU_R_Y, r.y);
  p.wr(PPU_R_WH, ((uint32_t)r.w << 9) | (uint32_t)r.h);
  p.wr(PPU_R_COLOR, 15);                 /* white */
  p.wr(PPU_R_CMD, PPU_CMD_FILL);
  p.clk(400);

  /* and a fully-off-screen rect must be dropped, not sent */
  TCHECK(!ppu_clip_rect(256, 0, 8, 8, &r));
  std::printf(g_fail ? "FAILED\n" : "ok\n");
}

int main(int argc, char **argv) {
  VPpu *top = new VPpu;
  Ppu p;
  p.top = top;

  std::printf("== PPU RTL harness (real rtl/Ppu.sv under Verilator) ==\n");
  p.reset_chip();

  test_reset_quiescent(p);
  test_roundtrip_8byte(p);
  test_overlength_frame(p);
  test_palette_latch(p);
  test_fill_and_capture(p);
  test_fbdraw_path(p);

  std::printf("\n%d checks passed, %d failed\n", g_ok, g_fail);
  std::printf(g_fail ? "=== RTL SUITE: FAIL ===\n" : "=== RTL SUITE: PASS ===\n");
  delete top;
  return g_fail ? 1 : 0;
}
