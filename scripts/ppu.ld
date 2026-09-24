/* Link script for the riscv32-ppu platform (StarrySky-C2 Pico / retroSoC v1.0).
 *
 * The board is not a flat memory: it boots by executing in place out of SPI
 * Flash, while .data/.bss/heap live in external PSRAM and the stack is in
 * on-chip SRAM.  AM's default linker.ld assumes one contiguous image, so this
 * one splits the three regions and leaves the .data load image behind .text in
 * Flash for start.S to copy. */

OUTPUT_FORMAT("elf32-littleriscv")
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x30000000, LENGTH = 16M   /* XIP, reset PC */
  PSRAM (rwx) : ORIGIN = 0x04000000, LENGTH = 8M
}

/* Top of the 128 KB on-chip SRAM. */
_stack_top = 0x00020000;

SECTIONS
{
  .text :
  {
    . = ALIGN(4);
    *(.text.entry)
    *(.text)
    *(.text.*)
    *(.rodata)
    *(.rodata.*)
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  /* Load image for .data sits right behind .text in Flash. */
  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data.*)
    *(.sdata)
    *(.sdata.*)
    . = ALIGN(4);
    _edata = .;
  } > PSRAM AT > FLASH

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss.*)
    *(.sbss)
    *(.sbss.*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > PSRAM

  . = ALIGN(16);
  _heap_start = .;

  /* AM's heap runs to the end of PSRAM. */
  _psram_end = ORIGIN(PSRAM) + LENGTH(PSRAM);

  /DISCARD/ :
  {
    *(.comment)
    *(.eh_frame)
    *(.note*)
  }
}
