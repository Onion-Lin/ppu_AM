/* Tiny test framework: one .c file per suite, a handful of CHECK macros.
   Kept dependency-free so suites compile with plain cc on the host. */
#ifndef PPU_TEST_H
#define PPU_TEST_H

#include <stdio.h>

extern int ppu_t_fail;
extern int ppu_t_ok;
extern const char *ppu_t_suite;

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (cond) {                                                          \
      ppu_t_ok++;                                                        \
    } else {                                                             \
      ppu_t_fail++;                                                      \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
    }                                                                    \
  } while (0)

#define CHECK_EQ_U(a, b)                                                 \
  do {                                                                   \
    unsigned long _a = (unsigned long)(a), _b = (unsigned long)(b);       \
    if (_a == _b) {                                                      \
      ppu_t_ok++;                                                        \
    } else {                                                             \
      ppu_t_fail++;                                                      \
      printf("  FAIL %s:%d  %s == %s  (got %lu, want %lu)\n",            \
             __FILE__, __LINE__, #a, #b, _a, _b);                        \
    }                                                                    \
  } while (0)

/* Run a suite function and report.  Suites are declared as void f(void). */
#define SUITE(fn)                                                        \
  do {                                                                   \
    int _f0 = ppu_t_fail;                                                \
    const char *_s0 = ppu_t_suite;                                       \
    ppu_t_suite = #fn;                                                   \
    printf("[ %-22s ] ", #fn);                                           \
    fflush(stdout);                                                      \
    fn();                                                                \
    if (ppu_t_fail == _f0) printf("ok\n");                               \
    else printf("FAILED\n");                                             \
    ppu_t_suite = _s0;                                                   \
  } while (0)

#endif
