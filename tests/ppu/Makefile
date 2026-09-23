# Tests for the riscv32-ppu platform.
#
#   make            build + run the host suites (no RTL, no mpc-frame needed)
#   make rtl        build + run the Verilator suite against the real PPU RTL
#   ./run.sh        both -- this is the regression gate
#   make clean
#
# PPU_DIR is found automatically in either layout:
#   - inside this repo            -> ../am/src/platform/ppu
#   - copied into an AM tree      -> ../../am/src/platform/ppu
# Override with PPU_DIR=/path if neither matches.
#
# The RTL suite also needs a mpc-frame checkout; set PPU_RTL to it.

TESTS     = $(CURDIR)

PPU_DIR  ?= $(firstword $(wildcard \
             $(CURDIR)/../../am/src/platform/ppu \
             $(CURDIR)/../am/src/platform/ppu))

ifeq ($(PPU_DIR),)
$(error cannot find am/src/platform/ppu; set PPU_DIR=...)
endif

CC        ?= cc
CFLAGS    ?= -std=c11 -O1 -Wall -Wextra -Wno-unused-parameter
CFLAGS    += -I$(TESTS)/host -I$(PPU_DIR)/include

HOST_BIN   = $(TESTS)/build/host_tests
HOST_SRCS  = $(wildcard $(TESTS)/host/*.c) $(PPU_DIR)/ppu_core.c

# The real PPU RTL, frozen.  Verilated standalone: Ppu exposes plain
# clock/reset/io_in/io_out/io_oe ports, so FrameTop is not needed.
PPU_RTL   ?= /home/ysl/OSOC_study/project/Hackintosh/mpc-frame/designs/ppu
VERILATOR ?= verilator
RTL_BIN    = $(TESTS)/build/ppu_rtl_sim
RTL_BUILD  = $(TESTS)/build/obj_dir

# Fail early with something readable if the RTL is not where we think it is.
rtl: $(RTL_BIN)
	@$(RTL_BIN)

$(RTL_BIN): $(TESTS)/rtl/sim_main.cpp $(PPU_RTL)/rtl/Ppu.sv Makefile
	@mkdir -p $(TESTS)/build
	$(VERILATOR) --cc --exe --build -j 1 \
	  --top-module Ppu -Wno-fatal -Wno-UNOPTFLAT -Wno-WIDTH \
	  --Mdir $(RTL_BUILD) \
	  -CFLAGS "-std=c++17 -O1 -I$(PPU_DIR)/include" \
	  -o $(RTL_BIN) \
	  $(PPU_RTL)/rtl/Ppu.sv \
	  $(PPU_RTL)/rtl/FbSram.sv \
	  $(PPU_RTL)/rtl/FillEngine.sv \
	  $(PPU_RTL)/rtl/ScanPath.sv \
	  $(PPU_RTL)/rtl/SyncSpi.sv \
	  $(PPU_RTL)/rtl/vendor/simple_480p.sv \
	  $(PPU_RTL)/rtl/vendor/sram_4096x64_model.sv \
	  $(TESTS)/rtl/sim_main.cpp $(PPU_DIR)/ppu_core.c

host: $(HOST_BIN)
	@$(HOST_BIN)

$(HOST_BIN): $(HOST_SRCS) $(TESTS)/host/ppu_test.h
	@mkdir -p $(TESTS)/build
	$(CC) $(CFLAGS) -o $@ $(HOST_SRCS)

test: host rtl
	@echo
	@echo "=== PPU-AM regression: PASS ==="

clean:
	rm -rf $(TESTS)/build

.PHONY: all host rtl test clean
