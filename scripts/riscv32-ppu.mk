include $(AM_HOME)/scripts/isa/riscv.mk
include $(AM_HOME)/scripts/platform/ppu.mk
# zicsr is required: cte.c writes mtvec/mstatus.  The board's PicoRV32 is
# documented as RV32IMAC, so whether the core actually has the CSR extension is
# UNVERIFIED -- if it does not, csrw traps and there is no handler yet, which is
# the one failure mode that cannot be debugged over SYS_UART.  The board probe
# settles it.  The fallback is platform/dummy/cte.c, which uses no CSRs.
COMMON_CFLAGS += -march=rv32im_zicsr -mabi=ilp32   # overwrite
LDFLAGS       += -melf32lriscv                     # overwrite

AM_SRCS += riscv/ppu/start.S \
           riscv/ppu/cte.c \
           riscv/ppu/trap.S \
           platform/dummy/vme.c \
           platform/dummy/mpe.c
