/* Host test driver: links every suite and runs them.
 *
 * These suites exercise ppu_core.c, which is deliberately free of I/O so the
 * frame encoding, palette LUT and rect clipping can be checked on the host
 * without the PPU, the QSPI controller, or Verilator.
 *
 * Suites are declared weak so a task can add a test_*.c file without having to
 * edit this one; a suite that has not landed yet simply does not run. */

#include "ppu_test.h"

int ppu_t_fail = 0;
int ppu_t_ok = 0;
const char *ppu_t_suite = "(none)";

__attribute__((weak)) void test_sanity_suite(void);
__attribute__((weak)) void test_frame_suite(void);
__attribute__((weak)) void test_palette_suite(void);
__attribute__((weak)) void test_clip_suite(void);
__attribute__((weak)) void test_scancode_suite(void);

#define MAYBE(fn) do { if (fn) SUITE(fn); } while (0)

int main(void) {
  MAYBE(test_sanity_suite);
  MAYBE(test_frame_suite);
  MAYBE(test_palette_suite);
  MAYBE(test_clip_suite);
  MAYBE(test_scancode_suite);

  printf("\n%d checks passed, %d failed\n", ppu_t_ok, ppu_t_fail);
  return ppu_t_fail ? 1 : 0;
}
