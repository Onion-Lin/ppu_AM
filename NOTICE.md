# 附加说明

本仓库包含三个来源的代码，各自的许可如下。

## 1. 本仓库自己的代码

`am/src/platform/ppu/` 下的 `ppu.h`、`ppu_core.h`、`ppu_core.c`，以及 `tests/ppu/`
下的全部测试代码，按 AbstractMachine 的 MIT 许可分发（见下方 LICENSE 正文）。

## 2. AbstractMachine（必需的上游依赖）

这些文件是 AbstractMachine 的 `riscv32-nemu` 平台的移植改写，而 AM 本身是：

> The AbstractMachine software is:
> Copyright (c) 2018-2021 Yanyan Jiang and Zihao Yu
> （MIT，全文见下方）

仓库地址：https://github.com/NJU-ProjectN/abstract-machine

**你要把这个仓库的 `am/` 和 `tests/` 拷进一份 AM 才能用**，所以 AM 的 MIT 许可
和版权声明必须随之一并提供。

## 3. PPU RTL（仅跑 RTL 测试时才需要）

`tests/ppu/rtl/sim_main.cpp` 会 Verilate 一份真实的 PPU RTL。那部分 RTL 不在本仓库内，
需要你自己从 mpc-frame 里取，它的来源是：

- `rtl/vendor/simple_480p.sv` — Project F（Will Green），**MIT**
- `rtl/vendor/sram_4096x64_model.sv` — ECOS/ICSprout ICS55 配套发布
- `rtl/Ppu.sv`、`FillEngine.sv`、`ScanPath.sv`、`FbSram.sv`、`SyncSpi.sv`
  — 本 PPU 项目自己的实现

## 4. retroSoC（仅涉及星空板卡时）

星空板卡的 SoC 是 retroSoC（MulanPSL-2.0），但**本仓库不含它的代码**，
只在文档和地址定义里引用它。

---

## LICENSE 正文（AbstractMachine，MIT）

The AbstractMachine software is:

Copyright (c) 2018-2021 Yanyan Jiang and Zihao Yu

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
