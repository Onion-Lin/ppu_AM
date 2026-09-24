/* Board-side definitions for the riscv32-ppu platform.
 *
 * ppu.h holds the PPU *protocol* (frame layout, registers, palette) and is
 * publishable on its own.  This header holds the StarrySky board's MMIO map,
 * which is what turns that protocol into actual bus accesses.
 *
 * Everything here comes from the retroSoC v1.0 boot log, except the TIMER0 and
 * PS2 layouts, which are still unverified -- those are isolated in one block at
 * the bottom so a hardware run only has to correct numbers in one place. */

#ifndef PPU_BOARD_H
#define PPU_BOARD_H

#include <stdint.h>

/* ---- memory map (retroSoC v1.0) ---- */
#define BOARD_SRAM_BASE    0x00000000u
#define BOARD_SRAM_TOP     0x00020000u   /* 128 KB; the stack lives here */
#define BOARD_PSRAM_BASE   0x04000000u
#define BOARD_PSRAM_TOP    0x04800000u   /* 8 MB; .data/.bss/heap */
#define BOARD_FLASH_BASE   0x30000000u   /* 16 MB, XIP, reset PC */

/* ---- peripheral bases ---- */
#define BOARD_GPIO_BASE    0x03000000u
#define BOARD_SYSUART_BASE 0x03000010u
#define BOARD_TIM0_BASE    0x0300005cu
#define BOARD_ARCHINFO     0x03001000u
#define BOARD_RNG          0x03002000u
#define BOARD_HPUART_BASE  0x03003000u
#define BOARD_PS2_BASE     0x03005000u
#define BOARD_QSPI_BASE    0x03007000u

/* ---- QSPI registers (the SPI master that talks to the PPU) ---- */
#define QSPI_STATUS  (*(volatile uint32_t *)(BOARD_QSPI_BASE + 0x00))
#define QSPI_CLKDIV  (*(volatile uint32_t *)(BOARD_QSPI_BASE + 0x04))
#define QSPI_CMD     (*(volatile uint32_t *)(BOARD_QSPI_BASE + 0x08))
#define QSPI_ADR     (*(volatile uint32_t *)(BOARD_QSPI_BASE + 0x0c))
#define QSPI_LEN     (*(volatile uint32_t *)(BOARD_QSPI_BASE + 0x10))
#define QSPI_DUM     (*(volatile uint32_t *)(BOARD_QSPI_BASE + 0x14))
#define QSPI_TXFIFO  (*(volatile uint32_t *)(BOARD_QSPI_BASE + 0x18))
#define QSPI_RXFIFO  (*(volatile uint32_t *)(BOARD_QSPI_BASE + 0x20))
#define QSPI_INTCFG  (*(volatile uint32_t *)(BOARD_QSPI_BASE + 0x24))
#define QSPI_INTSTA  (*(volatile uint32_t *)(BOARD_QSPI_BASE + 0x28))

/* ---- SYS_UART: the debug console, also where AM putch goes ---- */
#define SYS_UART_CLKDIV (*(volatile uint32_t *)(BOARD_SYSUART_BASE + 0x00))
#define SYS_UART_DATA   (*(volatile uint32_t *)(BOARD_SYSUART_BASE + 0x04))
/* No status register exists, so every write has to be spaced out in software. */

/* ---- CPU clock ----
 * MUST match the board.  It decides QSPI_CLKDIV (and therefore whether SCLK
 * stays under the ~4.2 MHz ceiling SyncSpi imposes) and every software delay.
 * The boot log from another StarrySky board read 64 MHz; this board's crystal
 * is silkscreened 72 MHz.  Run tools/ppu-probe to find out for certain. */
#ifndef PPU_CPU_HZ
#define PPU_CPU_HZ 64000000u
#endif

/* SCLK = CPU_HZ / (2*(clkdiv+1)).  Pick the divider that lands closest to a
 * 4.0 MHz target, which is 6.28 PPU clocks per bit at 25.175 MHz. */
#define PPU_QSPI_TARGET_HZ 4000000u
#define PPU_QSPI_CLKDIV \
  ((PPU_CPU_HZ / (2u * PPU_QSPI_TARGET_HZ)) > 0u \
     ? (PPU_CPU_HZ / (2u * PPU_QSPI_TARGET_HZ)) - 1u : 0u)

/* UART baud divider: the datasheet formula is baud = CPU_HZ / clkdiv. */
#define PPU_UART_CLKDIV (PPU_CPU_HZ / 115200u)

/* ---- TIMER0 / PS2: layouts UNVERIFIED ----
 * Offsets guessed from the naming in the board header; a hardware run has to
 * confirm them.  Kept together so only this block needs editing. */
#define TIM0_CONFIG (*(volatile uint32_t *)(BOARD_TIM0_BASE + 0x00))
#define TIM0_VALUE  (*(volatile uint32_t *)(BOARD_TIM0_BASE + 0x04))
#define TIM0_DATA   (*(volatile uint32_t *)(BOARD_TIM0_BASE + 0x08))

#define PS2_DATA    (*(volatile uint32_t *)(BOARD_PS2_BASE + 0x00))
#define PS2_STATUS  (*(volatile uint32_t *)(BOARD_PS2_BASE + 0x04))

#endif /* PPU_BOARD_H */
