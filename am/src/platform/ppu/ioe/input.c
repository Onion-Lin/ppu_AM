/* Keyboard: AM_INPUT_KEYBRD from the board's PS2 controller at 0x03005000.
 *
 * The register offsets and the scan-code set are UNVERIFIED -- the retroSoC PS2
 * IP's interface is not documented in the SDK pages available.  What is here is
 * the shape AM expects plus an AT Set 2 make/break table, so correcting the
 * offsets is a one-line change.
 *
 * Using PS2 rather than the UART matters: a serial terminal only reports key
 * presses, while PS2 make/break codes give real keyup, which is what
 * AM_INPUT_KEYBRD promises. */

#include <am.h>
#include <ppu_board.h>

#define PS2_KEYDOWN 0x8000u

/* AT Set 2 make codes for the keys AM names.  0 means "not mapped". */
static const uint16_t set2_make[256] = {
  ['A'] = AM_KEY_A, ['B'] = AM_KEY_B, ['C'] = AM_KEY_C, ['D'] = AM_KEY_D,
  ['E'] = AM_KEY_E, ['F'] = AM_KEY_F, ['G'] = AM_KEY_G, ['H'] = AM_KEY_H,
  ['I'] = AM_KEY_I, ['J'] = AM_KEY_J, ['K'] = AM_KEY_K, ['L'] = AM_KEY_L,
  ['M'] = AM_KEY_M, ['N'] = AM_KEY_N, ['O'] = AM_KEY_O, ['P'] = AM_KEY_P,
  ['Q'] = AM_KEY_Q, ['R'] = AM_KEY_R, ['S'] = AM_KEY_S, ['T'] = AM_KEY_T,
  ['U'] = AM_KEY_U, ['V'] = AM_KEY_V, ['W'] = AM_KEY_W, ['X'] = AM_KEY_X,
  ['Y'] = AM_KEY_Y, ['Z'] = AM_KEY_Z,
  ['0'] = AM_KEY_0, ['1'] = AM_KEY_1, ['2'] = AM_KEY_2, ['3'] = AM_KEY_3,
  ['4'] = AM_KEY_4, ['5'] = AM_KEY_5, ['6'] = AM_KEY_6, ['7'] = AM_KEY_7,
  ['8'] = AM_KEY_8, ['9'] = AM_KEY_9,
  [' '] = AM_KEY_SPACE,
  ['\r'] = AM_KEY_RETURN, ['\n'] = AM_KEY_RETURN,
  ['\b'] = AM_KEY_BACKSPACE, [0x7f] = AM_KEY_BACKSPACE,
  ['\t'] = AM_KEY_TAB, [0x1b] = AM_KEY_ESCAPE,
};

void __am_input_keybrd(AM_INPUT_KEYBRD_T *kbd) {
  /* TODO(probe): confirm PS2_DATA/PS2_STATUS offsets and the code set. */
  uint32_t raw = PS2_DATA;
  uint32_t code = raw & 0xffu;
  uint32_t brk  = (raw >> 8) & 1u;    /* break prefix, if the IP exposes it */

  kbd->keydown = brk ? 0 : 1;
  kbd->keycode = code < 256 ? set2_make[code] : 0;
  if (kbd->keycode == 0) kbd->keydown = 0;
}
