# 任务计划与进度

## 设计约束（已确认）

- **只做整块同色渲染**：`AM_GPU_FBDRAW` 取块内首像素，发一条 FILL。忽略通用位图路径
- **调色板固定**：VGA/EGA 16 色，init 时装载，运行期只查表，init 后永不碰 R8
- **颜色降级接受**；`GPU_CONFIG` 报 PPU 实际的 256×240
- **QSPI 直连，只写不读**：busy 算出来而非轮询，vblank 用定时
- **RTL 冻结**：只能 Verilator 仿真，不能改 PPU RTL
- **超时不读 RXFIFO**：读是优化，且依赖板子未文档化的行为，暂不做

## 任务分解

每个任务一个分支，`--no-ff` 合回 `PPU-AM`，合入前跑 `./tests/ppu/run.sh`。

| # | 分支 | 内容 | 回归门禁 | 状态 |
|---|---|---|---|---|
| T0 | `ppu/T0-scaffold` | 测试基建、`ppu_core` 纯逻辑/I/O 分离 | 主机套件绿 | ✅ 完成 |
| T1 | `ppu/T1-frame` | `ppu.h` 帧编码 + 单测 | 帧编码用例全过 | ✅ 完成 |
| T2 | `ppu/T2-palette` | rgb6 量化 + VGA 16 色表 + 64 项 LUT | 已知色落点正确、64 项全有效 | ✅ 完成 |
| T3 | `ppu/T3-clip` | 矩形裁剪 | 覆盖 demo 320 宽 / slider 400×300 / 负值 / 1×1 | ✅ 完成 |
| T4 | `ppu/T4-verilator` | Verilator harness + 真实 PPU RTL | 扫描线断言通过 | ✅ 完成 |
| T5 | `ppu/T5-render` | `ioe/gpu.c` 翻译层，在 T4 harness 上跑 | VGA 捕获符合预期 | ⬜ 未开始 |
| T6 | `ppu/T6-scaffold-am` | `scripts/*.mk`、linker、`start.S`、`trm.c`、`ioe.c`、`mpe.c`、arch 头 | `hello` 能编出 ELF | ⬜ 未开始 |
| T7 | `ppu/T7-periph` | `timer.c`、`input.c`（PS2） | 键码表单测过 | ⬜ 未开始 |
| T8 | `ppu/T8-review` | 全量回归 + 代码 review + 文档 | 全绿 + review 记录 | ⬜ 未开始 |

## 每任务的流程

```
git checkout -b ppu/Tn-xxx PPU-AM
  → 实现
  → ./tests/ppu/run.sh        （回归）
  → 自查 / review
  → git commit
git checkout PPU-AM && git merge --no-ff ppu/Tn-xxx
```

## 已完成任务的实质内容

### T1 帧编码
所有期望值**转写自 `SyncSpi.sv` 的位序**，不是猜的。同时用 `FramePpuTb` 的实测值
钉死 WH 和 PSET 的 packing。

### T2 调色板
VGA 16 色截断到 2-2-2 后全部互不重复。64 项 LUT 用带亮度权重的距离度量
（`2ΔR² + 4ΔG² + 3ΔB²`）——只在 init 算 64×16 次，所以度量可以随便用最贵的。

**测试抓到一个真 bug**：light gray 被写成 `0x28 = 10_10_00`（那根本不是灰），
正确是 `0x2a = 10_10_10`。症状是 litenes 的 `0x808080` 落到了 cyan。

### T3 裁剪
169 组边界扫描，断言"结果一定装得下、且恰好覆盖请求中在屏的那部分"。
demo 的 320 宽和 slider 的 400×300 都是真实会出现的输入。

### T4 RTL 验证
Verilator 直接 Verilate 真实 `rtl/Ppu.sv`（不需要 FrameTop，因为 `Ppu` 的端口就是
`clock/reset/io_in/io_out/io_oe`）。C++ SPI 主机用 `ppu.h` 的编码发命令，
所以编码错了测试就红。

验证结果：
- 8 字节帧 trick 成立（寄存器正确写入）
- 多给 3~5 个 filler 字节被无害丢弃
- 红绿矩形捕获回 `red=128 green=128`，与 `FramePpuTb` 断言一致

两个 harness bug 值得记：
- `sbit_rd` 原来不驱动 MOSI，导致"读帧"实际是全 0 的写帧
- `cs_high()` 拉高 CS 后没给时钟，导致同步器锁存不到、下一帧被静默丢弃
  （这个同时是**平台约束**，见 `docs/findings.md` 第 2 条）

## 待确认（平台层落地前必须解决）

| 项 | 影响 | 现状 |
|---|---|---|
| `TIMER0` 的 `CONFIG/VALUE/DATA` 哪个是自由运行计数器 | `AM_TIMER_UPTIME` 的时基 | 未硬件验证 |
| PS2 在 `0x03005000` 的寄存器偏移和 scan code 集 | `AM_INPUT_KEYBRD` | 未硬件验证 |
| QSPI 两次传输间 CS 高电平是否够久 | 整个写路径 | 未硬件验证 |
| CPU 实际主频（64 还是 72 MHz） | `clkdiv` 取值、波特率 | 未硬件验证 |
| PicoRV32 是否开了 Zicsr | `cte_init` 要 `csrw mtvec`，不开则 CTE/VME 要重写 | 未硬件验证 |

## 已知妥协

1. **无 VSYNC 同步，靠定时 16.68 ms**。两块板各自晶振，相对漂移可能 ~50 ppm。
   干净解法是把 PPU 的 `vsync` 输出飞线到一个空闲 GPIO 轮询——需要硬件配合，留接口。
2. **busy 阈值是估算**：`words > 800` 才等。若仲裁比推算慢，会出现命令丢失导致局部花屏。
3. **clkdiv 依赖实际 CPU 时钟**，必须保证 SCLK ≤ 4.2 MHz。
4. **非纯色块退化为单色矩形**，已接受。
5. `TIMER0` / `PS2` 寄存器语义未验证，偏移用宏集中并标 TODO。
