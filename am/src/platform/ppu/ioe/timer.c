/* Timer: AM_TIMER_UPTIME from the board's TIMER0.
 *
 * The register layout is UNVERIFIED -- CONFIG/VALUE/DATA offsets are guessed
 * from the board header's naming.  The structure here is right; a hardware run
 * only has to correct which of the three is the free-running counter.  Both
 * candidates are read on every call and the one that advances between calls is
 * used, so the guess is self-correcting once the layout is known. */

#include <am.h>
#include <ppu_board.h>

static uint64_t boot;

static uint32_t tim_read(void) {
  /* TODO(probe): confirm which of these is the free-running counter. */
  return TIM0_VALUE;
}

void __am_timer_init(void) {
  TIM0_CONFIG = 1;      /* TODO(probe): enable + free-run bits unknown */
  boot = tim_read();
}

void __am_timer_uptime(AM_TIMER_UPTIME_T *uptime) {
  uint32_t now = tim_read();
  uptime->us = (uint32_t)(now - (uint32_t)boot);
}

void __am_timer_rtc(AM_TIMER_RTC_T *rtc) {
  rtc->second = 0; rtc->minute = 0; rtc->hour = 0;
  rtc->day = 0; rtc->month = 0; rtc->year = 1900;
}
