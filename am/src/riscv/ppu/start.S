/* Entry for the riscv32-ppu platform.
 *
 * The board boots from SPI Flash at 0x30000000 (XIP), and .data/.bss live in
 * PSRAM, so the usual AM start.S -- which assumes a single contiguous image --
 * is not enough: .data has to be copied out of Flash and .bss cleared before
 * main runs.  This mirrors what the board's own start.s does. */

.section .text.entry, "ax"
.globl _start
.type  _start, @function

_start:
  mv   s0, zero
  la   sp, _stack_top

  /* ---- copy .data : flash LMA -> PSRAM VMA ---- */
  la   a0, _sidata
  la   a1, _sdata
  la   a2, _edata
1:
  bgeu a1, a2, 2f
  lw   a3, 0(a0)
  sw   a3, 0(a1)
  addi a0, a0, 4
  addi a1, a1, 4
  j    1b
2:

  /* ---- zero .bss ---- */
  la   a0, _sbss
  la   a1, _ebss
3:
  bgeu a0, a1, 4f
  sw   zero, 0(a0)
  addi a0, a0, 4
  j    3b
4:

  call _trm_init
5:
  j    5b

.size _start, . - _start
