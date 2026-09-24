AM_SRCS := platform/ppu/trm.c \
           platform/ppu/ioe/ioe.c \
           platform/ppu/ioe/timer.c \
           platform/ppu/ioe/input.c \
           platform/ppu/ioe/gpu.c \
           platform/ppu/ppu_core.c \
           platform/ppu/ppu_qspi.c \

CFLAGS    += -fdata-sections -ffunction-sections
CFLAGS    += -I$(AM_HOME)/am/src/platform/ppu/include
LDSCRIPTS += $(AM_HOME)/scripts/ppu.ld
LDFLAGS   += --defsym=_pmem_start=0x30000000 --defsym=_entry_offset=0x0
LDFLAGS   += --gc-sections -e _start

MAINARGS_MAX_LEN = 64
MAINARGS_PLACEHOLDER = the_insert-arg_rule_in_Makefile_will_insert_mainargs_here
CFLAGS += -DMAINARGS_MAX_LEN=$(MAINARGS_MAX_LEN) -DMAINARGS_PLACEHOLDER=$(MAINARGS_PLACEHOLDER)

image: image-dep
	@$(OBJDUMP) -d $(IMAGE).elf > $(IMAGE).txt
	@echo + OBJCOPY "->" $(IMAGE_REL).bin
	@$(OBJCOPY) -O binary $(IMAGE).elf $(IMAGE).bin

# Flash build/$(NAME)-$(ARCH).bin to SPI Flash at 0x30000000 through the board's
# HFP-LINK U-disk, then set FLASH_SEL to CHIP and press reset.
run: image
	@echo "+ flash $(IMAGE_REL).bin at 0x30000000, then FLASH_SEL=CHIP + reset"

gdb: image
	@echo "+ no gdb stub on this platform yet; use the SYS_UART console"

.PHONY: run gdb
