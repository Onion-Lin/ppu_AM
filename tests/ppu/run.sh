#!/usr/bin/env bash
# PPU-AM regression gate.
#
#   ./run.sh                         host suites + Verilator RTL suite
#   ./run.sh host                    host only (fast, no Verilator, no mpc-frame)
#   ./run.sh -v                      verbose build output
#
# The RTL suite needs a mpc-frame checkout.  Override the default with PPU_RTL:
#   PPU_RTL=/path/to/mpc-frame/designs/ppu ./run.sh
set -u
cd "$(dirname "$0")"

VERBOSE=""
[ "${1:-}" = "-v" ] && VERBOSE="V=1"

PPU_RTL="${PPU_RTL:-/home/ysl/OSOC_study/project/Hackintosh/mpc-frame/designs/ppu}"

fail() { echo; echo "!!! $*"; echo "!!! REGRESSION FAILED"; exit 1; }

echo "### host suites (pure logic, no RTL)"
make host ${VERBOSE} >/tmp/ppu_host.log 2>&1 || {
  tail -40 /tmp/ppu_host.log; fail "host suites failed to build/run"; }
grep -q "0 failed" /tmp/ppu_host.log || {
  cat /tmp/ppu_host.log; fail "host suites reported failures"; }
tail -2 /tmp/ppu_host.log

echo
if [ -f "$PPU_RTL/rtl/Ppu.sv" ]; then
  if command -v verilator >/dev/null; then
    echo "### Verilator suite (real PPU RTL from $PPU_RTL)"
    make rtl ${VERBOSE} PPU_RTL="$PPU_RTL" >/tmp/ppu_rtl.log 2>&1 || {
      tail -40 /tmp/ppu_rtl.log; fail "Verilator suite failed to build/run"; }
    tail -5 /tmp/ppu_rtl.log
  else
    echo "### Verilator suite SKIPPED (verilator not on PATH)"
  fi
else
  echo "### Verilator suite SKIPPED"
  echo "    $PPU_RTL/rtl/Ppu.sv not found."
  echo "    Set PPU_RTL to your mpc-frame checkout to enable it:"
  echo "      PPU_RTL=/path/to/mpc-frame/designs/ppu ./run.sh"
fi

echo
echo "=== PPU-AM regression: PASS ==="
