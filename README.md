# PPU-AM — 给流片 PPU 芯片做 AbstractMachine 运行时环境

给一颗 **256×240 @4bpp 索引色 VGA PPU** 做一个 AM（AbstractMachine）目标平台，
让 AM 程序能驱动它。

PPU 本身通过 SPI 下发矩形命令（不是逐像素写显存），星空板卡的 retroSoC/PicoRV32
当 SPI 主机。这个仓库目前包含**已经做好并被验证的部分**，以及还不完整部分的计划。

> ### 现在的状态
>
> **已验证**：SPI 帧编码、VGA 16 色调色板查找表、矩形裁剪 —— 主机单测 6822 项 +
> **真实 PPU RTL 上的 Verilator 端到端 16 项全过**。
>
> **还没做**：AM 平台层本体（`ioe/gpu.c`、`trm.c`、`scripts/riscv32-ppu.mk` 等）。
> 所以**现在还不能 `make ARCH=riscv32-ppu` 编译出镜像**。
> 这个仓库目前的价值是「协议层 + 它的验证」，运行时要等平台层落地。

---

## 它解决的核心问题

AM 的 `AM_GPU_FBDRAW(x, y, pixels, w, h, sync)` 给的是**逐像素 RGB888 位图**，
而 PPU 只会填**单个 4 bit 调色板索引**的矩形。所以运行时只做一件事：

> 取块内**首像素**的颜色，量化成调色板槽，发一条 FILL。

代价是每个矩形变成一块纯色。对提交单色 buffer 的程序（`snake`、`typing-game` 的
擦除、`demo` 的 tile、`am-tests`）画面完全正确；对提交位图的程序（`litenes`、
`fceux`）退化为色块。这是明确的取舍，不是缺陷。

好处是**每帧 FILL 条数 = API 调用次数**，和像素复杂度无关：

| 程序 | 每帧 FBDRAW | FILL | QSPI 耗时 |
|---|---:|---:|---:|
| snake | ~20 | 20 | 1.0 ms |
| typing-game | ~20 | 20 | 1.0 ms |
| litenes（整帧一次调用） | 1 | 1 | 48 µs |
| fceux（每扫描线一次） | 240 | 240 | 11.5 ms |

litenes 反而最快，因为它每帧只调用一次。

## 两个关键设计

### 1. QSPI 只写、不读

SDK 的 QSPI HAL 只封装了 `hal_qspi_write_*`，没有读接口。而 PPU 必须读（轮询 busy、
读 `frame_ctr`）。所以设计成**完全不需要读**：

| 原本要读的 | 替代 |
|---|---|
| R5 `busy` | 算出来：FillEngine 每字约 1.05 PPU 时钟，而下一条命令的 SPI 要 3 帧 × 403 = 1209 时钟，小 fill 自然覆盖 |
| R5 `frame_ctr` 判 VSYNC | 定时：帧周期固定 420,000 PPU 时钟 = 16.68 ms |
| R9 回读验证 | 不需要 |

### 2. 8 字节帧 trick

`SyncSpi` 在 `bitcnt == 39` 锁存 `reg_we`，且 `bitcnt` 上限 63 ——
**第 40 位之后的多余时钟被无害丢弃**（RTL 注释原话：*Extra clocks are ignored*）。

所以真实的 40 bit 帧（5 字节）可以塞进驱动里**已验证过的** `hal_qspi_write_32x2`
（8 字节、单次 START、CS 只拉低一次），后 3 字节任意：

```c
w0 = (header << 24) | (data >> 8);   // header, d[31:24], d[23:16], d[15:8]
w1 = (data  << 24);                  // d[7:0] + 3 字节 filler
```

这样 QSPI 写路径全用文档化 API，不用碰未文档化的 40 bit 长度。

**这个 trick 已在真实 RTL 上验证**（见 `tests/ppu/rtl/`），包括多给 3~5 个
filler 字节仍被正确丢弃。

---

## 目录结构

```
Ppu-AM/
├── am/                          拷进 AbstractMachine 树根
│   └── src/platform/ppu/
│       ├── include/ppu.h         协议：40bit 帧、寄存器索引、8字节帧、clkdiv 算术、16 色调色板表
│       ├── include/ppu_core.h    纯逻辑声明
│       └── ppu_core.c            rgb6 量化、64 项 LUT、矩形裁剪
├── scripts/                     拷进 AM 的 scripts/（平台 mk，尚未写）
├── tests/ppu/                   拷进 AM 的 tests/
│   ├── Makefile                  自动适配本仓库/AM 树两种布局
│   ├── run.sh                    回归门禁
│   ├── host/                     主机单测（纯逻辑，无需 RTL）
│   └── rtl/                      Verilator + 真实 PPU RTL
└── docs/findings.md              从 RTL 里挖出来的硬事实
```

---

## 怎么用

### 只想跑测试（不需要板子）

```sh
./tests/ppu/run.sh
```

默认会跑主机单测（6822 项）和 Verilator RTL 套件（16 项）。RTL 套件需要一份
mpc-frame  checkout：

```sh
PPU_RTL=/path/to/mpc-frame/designs/ppu ./tests/ppu/run.sh
```

只跑主机单测（最快，什么都不依赖）：

```sh
./tests/ppu/run.sh host
```

### 装进 AbstractMachine

```sh
# 假设你的 AM 在 $AM_HOME
cp -r am/src/platform/ppu   $AM_HOME/am/src/platform/
cp -r scripts/*             $AM_HOME/scripts/
cp -r tests/ppu             $AM_HOME/tests/
```

测试在 AM 树里也能直接跑（Makefile 会自己找到 `am/src/platform/ppu`）：

```sh
cd $AM_HOME/tests/ppu && ./run.sh
```

### 在板子上编译运行

**还不支持** —— AM 平台层（`ioe/gpu.c`、`trm.c`、`scripts/riscv32-ppu.mk`、
`am/include/arch/`、`am/src/riscv/ppu/`）尚未实现。进度见 `PLAN.md`。

---

## 硬件前提

- **PPU 板**：独立一块，3.3V IO，自带 25.175 MHz 时钟（VGA 640×480@60 的像素时钟）
- **主机**：星空板卡（retroSoC + PicoRV32 RV32IMAC，64/72 MHz），QSPI 控制器
  `0x03007000`，其 `QSPI_CLK / QSPI_CS_0 / QSPI_DAT_0 / QSPI_DAT_1` 接 PPU 的
  `sclk / cs_n / mosi / miso`

### SCLK 上限是硬约束

`SyncSpi` 用**单级触发器**采 `mosi`，却用**两级**同步 `sclk`（`SyncSpi.sv:44` vs
`:41`），MOSI 的建立余量只有 `P/2 + 1` 个 PPU 时钟。**结论：SCLK 必须 ≤ ~4.2 MHz。**

`SCLK = CPU_HZ / (2 × (clkdiv + 1))`，64 MHz 下 `clkdiv = 7` → 4.0 MHz
→ 250 ns/位 → 6.28 个 PPU 时钟/位。

---

## 当前进度

| 任务 | 状态 |
|---|---|
| T0 测试骨架、`ppu_core` 纯逻辑/I/O 分离 | 完成 |
| T1 帧编码（期望值转写自 RTL 位序） | 完成 |
| T2 VGA 16 色 + 64 项 LUT | 完成 |
| T3 矩形裁剪 | 完成 |
| T4 **真实 PPU RTL 上的 Verilator 验证** | 完成 |
| T5 `ioe/gpu.c` 渲染层 | 未开始 |
| T6 AM 骨架（mk / linker / start.S / trm.c） | 未开始 |
| T7 外设移植（timer / PS2） | 未开始 |
| T8 全量回归 + review | 未开始 |

明细和取舍见 `PLAN.md`。

---

## 许可

本仓库的代码是 AbstractMachine（MIT）的移植改写，按 AM 的 MIT 许可分发。
上游依赖和 RTL 组件的归属见 `NOTICE.md`。
