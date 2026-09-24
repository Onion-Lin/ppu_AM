/* TRM: putch, halt, and the heap.
 *
 * putch goes to the board's SYS_UART, the same console the boot banner uses.
 * That register has no status word, so each byte has to be spaced out in
 * software -- the shifter is fast enough that a short spin is plenty at 115200. */

#include <am.h>
#include <klib-macros.h>
#include <ppu_board.h>

extern char _heap_start;
extern char _psram_end;
int main(const char *args);

/* .data/.bss/heap live in PSRAM; the stack is in on-chip SRAM. */
Area heap = RANGE(&_heap_start, &_psram_end);

static const char mainargs[MAINARGS_MAX_LEN] = TOSTRING(MAINARGS_PLACEHOLDER);

void putch(char ch) {
  SYS_UART_DATA = (uint32_t)(uint8_t)ch;
  for (volatile int i = 0; i < 400; i++) ;
}

void halt(int code) {
  /* Same trap NEMU uses; the simulator or debugger catches the ebreak. */
  asm volatile("mv a0, %0; ebreak" : : "r"(code));
  while (1) ;
}

void _trm_init(void) {
  int ret = main(mainargs);
  halt(ret);
}
