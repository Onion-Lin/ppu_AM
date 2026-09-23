# Convenience entry point.  The real build lives in tests/ppu.
#
#   make test        host suites + Verilator RTL suite (the regression gate)
#   make test-host   host suites only (nothing to install)
#   make clean

.PHONY: test test-host clean

test: ; @$(MAKE) -C tests/ppu test

test-host: ; @$(MAKE) -C tests/ppu host

clean: ; @$(MAKE) -C tests/ppu clean
