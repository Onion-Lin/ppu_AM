# PPU-AM — 给流片 PPU 芯片做 AbstractMachine 目标平台

给一颗 **256×240 @4bpp 索引色 VGA PPU** 做的 AM（AbstractMachine）新增架构
`riscv32-ppu`，让 AM 程序能驱动它。

PPU 本身通过 SPI 下发矩形命令（不是逐像素写显存），主机是星空板卡
（retroSoC + PicoRV32 RV32IMAC），它的 QSPI 控制器当 SPI 主设备。

> ### 这是什么
>
> 这是一个 **AbstractMachine 的附加架构包**，不是独立的 AM。
> 它只包含新增 `riscv32-ppu` 架构所需的文件，其余全部依赖你已有的 AM 树。
>
> 装进 AM 之后 `make ARCH=riscv32-ppu` 可用，`hello`/`snake`/`typing-game`/
> `slider`/`demo`/`yield-os`/`thread-os` 都能编出镜像。
>
> **但还没有上过板。** 三个硬件未验证项见文末，任一为真都会出问题。

---

## 安装

```sh
# $AM_HOME 指向你的 AbstractMachine 仓库根目录
export AM_HOME=/path/to/abstract-machine

cd ppu_AM
cp -r am/src/platform/ppu  $AM_HOME/am/src/platform/
cp -r am/src/riscv/ppu     $AM_HOME/am/src/riscv/
cp scripts/riscv32-ppu.mk  $AM_HOME/scripts/
cp scripts/ppu.ld          $AM_HOME/scripts/
cp scripts/platform/ppu.mk $AM_HOME/scripts/platform/
```

就这些。`am/src/platform/ppu/` 和 `am/src/riscv/ppu/` 都是新目录，不会覆盖 AM
原有文件；`scripts/` 下的三个也是新增。

### 验证安装

```sh
cd $AM_HOME/../am-kernels/kernels/hello   # 或任何 AM 程序
make ARCH=riscv32-ppu
# + LD -> build/hello-riscv32-ppu.elf
# + OBJCOPY -> build/hello-riscv32-ppu.bin
```

### 依赖的 AM 原有文件

安装后的架构会用到这些 **AM 自带的** 文件，本仓库不提供（因为它们本来就该在）：

| 文件 | 用途 |
|---|---|
| `scripts/isa/riscv.mk` | RISC-V 工具链和 `ARCH_H` 覆盖 |
| `am/include/arch/riscv.h` | `Context` 结构（经 `isa/riscv.mk` 的 `ARCH_H` 引入） |
| `am/src/platform/dummy/vme.c` | VME 占位——PicoRV32 没有 MMU |
| `am/src/platform/dummy/mpe.c` | MPE 占位——单核 |
| `am/include/{am.h,amdev.h}`、`klib/` | AM 运行时本身 |

如果你的 AM 树缺少 `scripts/isa/riscv.mk`，说明版本太旧，需要先更新 AM。

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
**已在真实 RTL 上验证**，包括多给 3~5 个 filler 字节仍被正确丢弃。

---

## 目录结构

```
ppu_AM/
├── am/                        ← 整个拷进 $AM_HOME/am/
│   ├── src/platform/ppu/
│   │   ├── include/ppu.h        协议：40bit 帧、寄存器、8字节帧、16 色调色板
│   │   ├── include/ppu_core.h   纯逻辑声明
│   │   ├── include/ppu_board.h  星空板 MMIO 映射（改这里配板子）
│   │   ├── include/ppu_hw.h     传输层 API
│   │   ├── ppu_core.c           rgb6 量化、64 项 LUT、矩形裁剪
│   │   ├── ppu_qspi.c           QSPI 传输 + 计算的 busy 等待
│   │   ├── trm.c                putch / halt / heap
│   │   ├── ioe/gpu.c            ★ FBDRAW → FILL
│   │   ├── ioe/ioe.c            LUT 分发器
│   │   ├── ioe/timer.c          TIMER0 → uptime
│   │   ├── ioe/input.c          PS2 → AM_KEY_*
│   │   └── mpe.c                （已改用 platform/dummy，见下）
│   └── src/riscv/ppu/
│       ├── start.S              拷 .data、清 .bss
│       ├── cte.c                mtvec + kcontext + yield
│       └── trap.S               移植自 riscv/nemu
├── scripts/                   ← 拷进 $AM_HOME/scripts/
│   ├── riscv32-ppu.mk
│   ├── ppu.ld                  三段式链接脚本
│   └── platform/ppu.mk
├── tests/ppu/                 协议层测试（可选装）
├── docs/findings.md           12 条从 RTL 挖出来的硬约束
├── PLAN.md                    设计与进度
└── NOTICE.md                  许可归属
```

---

## 硬件前提

- **PPU 板**：独立一块，3.3V IO，自带 25.175 MHz 时钟（VGA 640×480@60 像素时钟）
- **主机**：星空板卡，QSPI 控制器 `0x03007000`，其
  `QSPI_CLK / QSPI_CS_0 / QSPI_DAT_0 / QSPI_DAT_1` 接 PPU 的
  `sclk / cs_n / mosi / miso`

### 内存布局（`scripts/ppu.ld`）

| 区域 | 地址 | 内容 |
|---|---|---|
| SPI Flash | `0x30000000` | `.text`/`.rodata`，XIP，复位 PC |
| PSRAM | `0x04000000` | `.data`/`.bss`/heap |
| 片上 SRAM | `0x00020000` | 栈顶 |

### 配板子要改哪里

全部集中在 `am/src/platform/ppu/include/ppu_board.h`：

| 宏 | 说明 |
|---|---|
| `PPU_CPU_HZ` | **必须正确**。决定 `clkdiv` 和所有软件延时。默认 64 MHz，板上晶振丝印 72 MHz，需实测 |
| `TIM0_*` | TIMER0 寄存器偏移，**未验证** |
| `PS2_*` | PS2 寄存器偏移和 scan code 集，**未验证** |

---

## 测试

```sh
./tests/ppu/run.sh host          # 6822 项，无需任何依赖
PPU_RTL=/path/to/mpc-frame/designs/ppu ./tests/ppu/run.sh   # 再加 16 项 RTL 端到端
```

测试装进 AM 树后也能跑（Makefile 自己找 `am/src/platform/ppu`）。

---

## 已知限制与未验证项

**还没上过板。** 以下三项只有硬件能回答：

| 项 | 失败表现 |
|---|---|
| **PicoRV32 有没有 Zicsr** | `csrw mtvec` 陷阱，而**此时没有 trap 处理程序**，程序跑飞且串口打不出东西。这是唯一无法通过串口调试的故障。退路：把 `riscv32-ppu.mk` 里的 `riscv/ppu/cte.c` 换成 `platform/dummy/cte.c` |
| QSPI 两次传输间 CS 高电平够不够 | 帧被静默丢弃，PPU 完全没反应 |
| `TIMER0` / `PS2` 寄存器布局 | `usleep`/动画、键盘输入失效 |

其他妥协：

1. **无 VSYNC 同步**，靠定时 16.68 ms。两块板各自晶振，~50 ppm 漂移 → 每分钟约半帧错位。干净解法是把 PPU 的 `vsync` 输出飞线到一个空闲 GPIO 轮询。
2. **busy 阈值是估算**：`words > 1200` 才等。若仲裁比推算慢，命令被丢弃 → 局部花屏。
3. **`PPU_CPU_HZ` 猜错会让 SCLK 越过 4.2 MHz 红线**（见 `docs/findings.md` 第 1 条），表现为偶发的位错误。
4. **VME 不可用**：PicoRV32 没有 MMU，`vme.c`/`mpe.c` 用 AM 的 `platform/dummy`。`nanos-lite` 这类需要分页的程序跑不了。
5. 非纯色块退化为单色矩形，已接受。

---

## 许可

本仓库是 AbstractMachine（MIT）的移植改写，按 AM 的 MIT 许可分发。
上游依赖和 RTL 组件的归属见 `NOTICE.md`。
