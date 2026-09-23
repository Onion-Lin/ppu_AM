# 从真实 RTL 里挖出来的硬事实

这些是写运行时**之前**必须先知道、而且只能从 RTL 或实测得到的约束。
它们要么直接决定代码怎么写，要么是踩过坑之后才发现的。

---

## 1. MOSI 没有同步器，SCLK 上限因此被卡死

`rtl/SyncSpi.sv`：

```systemverilog
sck_d <= {sck_d[0], sclk};   // :41  两级同步
cs_d  <= {cs_d[0],  cs_n };  // :42  两级同步
mosi_d <= mosi;              // :44  只有一级寄存器！
```

`sclk`/`cs_n` 走两级同步，`mosi` 只过一级。推算实际采样点：
`w_rise` 在真实上升沿后第 2 个 PPU 时钟被检出，而 `mosi_d` 只延迟 1 拍，
所以 MOSI 实际上在上升沿后第 1 个 PPU 时钟被采到。

主机在下降沿改 MOSI，所以建立余量 = **`P/2 + 1` 个 PPU 时钟**（P = SCLK 周期，
单位是 PPU 时钟）。

**结论：SCLK 必须 ≥ 4 个 PPU 时钟，稳妥取 6~8。**

64 MHz / `clkdiv=7` → SCLK 4.0 MHz → 250 ns/位 → **6.28 个 PPU 时钟/位**，正好够。

这一条是性能天花板：它决定每帧能塞多少条 FILL。

## 2. CS 拉高后必须保持若干个 PPU 时钟

`w_cs_fall` / `w_cs_rise` 都从两级同步的 `cs_d` 检出。如果 CS 拉高后立刻拉低：

- 同步器锁存不到高电平
- 下一帧的下降沿消失
- `bitcnt` 永不清零（它上限 63，会一直加满）
- 整帧被静默丢弃，`reg_we` 不产生

这个 bug 在 harness 里的表现极具误导性：**每一次读看起来都"污染"了后面的写**。
实际原因是读函数把 CS 拉高后没给时钟。定位方法是用 `--public-flat-rw` 直接看
`sck_d` / `cs_d` / `bitcnt` / `hdr`。

**对运行时的影响**：QSPI 控制器必须保证两次传输之间 CS 高电平足够久。
`hal_qspi_write_32x2` 是"一次 LEN + 一次 START + `wait_idle()`"，大概率满足，
但这是要实测确认的点。

## 3. 读帧也必须把 MOSI 的位全部发出去

`SyncSpi` 的 `miso` 只在 `hdr[7]`（rw 位）为 1 时才启动：

```systemverilog
if (w_fall && hdr[7] && bitcnt >= 6'd8) begin
    if (bitcnt == 6'd8) begin
        miso <= reg_rdata[31];
```

所以 harness 里如果只用 GPIO 翻转时钟而不驱动 MOSI，`hdr` 会是全 0 ——
那实际是一个**写 R0（CMD）且数据为 0 的写帧**，根本不是读。
harness 第一版就犯了这个错，表现为"读永远返回 0"。

## 4. 帧编码：位序是确定的，但要逐位对

从 `SyncSpi.sv` 转写（不是猜的）：

```systemverilog
hdr <= {hdr[6:0], mosi_d};              // :83  MSB first，8 位后 hdr[7]=rw
wsh <= {wsh[30:0], mosi_d};             // :85  MSB first
if (bitcnt == 39 && !hdr[7]) begin      // :86  第 40 位锁存
    reg_we <= 1'b1;
    reg_wdata <= {wsh[30:0], mosi_d};   // :88
end
if (bitcnt < 6'd63) bitcnt <= bitcnt + 1;   // :91  上限 63
```

- header 字节位序：`{rw, 3'b000, reg[3:0]}`，`rw` 在 MSB
- header 之后线上是 `data[31], data[30], ... data[0]`
- 第 40 位之后的多余时钟被丢弃（这就是 8 字节帧 trick 的依据）
- `reg_we` 是**一个 PPU 时钟宽**的脉冲，在真实第 40 个上升沿后第 3 个时钟变高

QSPI 侧：驱动对单个字节都做左对齐（`TXFIFO = data << 24`），且每 32 位字 MSB first。
所以 `w0` 必须以 header 字节开头。

## 5. 读回的字节布局

PPU 在 header 后**第一个下降沿**才启动 MISO，所以主机收到的 8 字节里：
- 第 0 字节 = header 期间的 MISO（噪声）
- 第 1~4 字节 = `reg_rdata[31:0]`，MSB first

即 `data = (b1<<24) | (b2<<16) | (b3<<8) | b4`。

## 6. WH 和 PSET 的位域 packing

与 RTL 逐位核对过，并用 `FramePpuTb` 的实测值钉死：

| 寄存器 | 位域 | FramePpuTb 的验证值 |
|---|---|---|
| R3 WH | `[17:9]=W`, `[8:0]=H` | `(64<<9)\|8 = 0x00008008` |
| R8 PSET | `[9:4]=RGB666`, `[3:0]=slot` | `0x301` = slot1 红, `0x0c2` = slot2 绿 |

## 7. 仲裁：扫描请求绝对优先

```systemverilog
wire fb_ce   = scan_req | wr_req;
wire fb_we   = ~scan_req & wr_req;
wire fb_addr = scan_req ? scan_addr : wr_addr;
assign wr_gnt = wr_req & ~scan_req;
```

`ScanPath` 只在 framebuffer word 地址变化时发一次读请求。×2 模式下一个 word
的 16 个 framebuffer 像素对应 32 个 VGA 像素，所以大约每 32 个像素周期占一次
读端口。

**推论（busy 不需轮询的依据）**：FillEngine 每写一个字约 1.05 个 PPU 时钟，
而下一条命令的 3 帧 SPI 要 3 × 403 = 1209 个 PPU 时钟。**小 fill 早在下一条命令
发完之前就执行完了**，只有大矩形（`h*(w/16+2) > ~800` 字）才需要等。

## 8. busy 期间的命令被丢弃，不排队

README §5.1 明说，RTL 也如此：`FillEngine` 只在 `F_IDLE` 时锁存参数。
所以驱动必须自己判断何时能发下一条。

## 9. VSET 必须在第一次 VSYNC 之前写完

README §15.7 + RTL：`r_vset` 复位为 0，而 `ScanPath` 内部复位配置是 ×2 / `x_off=64`。
**第一次 VSYNC 会把内部配置改成 ×1 / 偏移 0**。所以 R9 必须在首次 VSYNC 前写完，
否则会闪一帧错误缩放。同一次 VSYNC 等待也顺带让调色板从 `pal_next` 锁存到 `pal_live`。

## 10. 调色板是 2-2-2，16 项必须互不重复

调色板项是 6 bit `{R[1:0], G[1:0], B[1:0]}`，每通道只有 4 级
（0/85/170/255）。写 R8 时 `data[9:4]=RGB, data[3:0]=slot`。

VGA 16 色截断后要**逐一验算**——容易错的是 light green `01_11_01 = 0x1d`
和 light cyan `01_11_11 = 0x1f`，只差 B 通道。
（T2 的测试就抓到一个真 bug：light gray 被写成 `0x28 = 10_10_00`，
正确是 `0x2a = 10_10_10`。）

## 11. SRAM 上电内容未定义，必须先 CLR

README §15.9。init 序列里 CLR 不能省。

## 12. 硬件不裁剪，越界会回绕

README §15.5 + RTL：`FillEngine` 不做边界检查。而客户程序真的会越界 ——
`demo/io.h` 的 `screen_clear` 发 320 宽（帧缓冲只有 256），`slider` 发 400×300。
**所以裁剪是正确性要求，不是防御性编程。**
