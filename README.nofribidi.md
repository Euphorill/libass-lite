# libass-nofribidi

基于 libass 0.17.5 的分支，移除 FriBidi 依赖，只处理 LTR 文本。除此之外的渲染行为与上游一致。

## 为什么可以移除 FriBidi

FriBidi 提供的是 Unicode 双向文本算法（UAX #9）。对纯 LTR 文本，这套算法本来就是恒等操作——两条规则决定了这一点：

- **L2（重排）** 只反转"嵌入层级不低于某个奇数层级"的连续序列。纯 LTR 文本全为偶数层级，没有序列可反转。
- **L4（镜像）** 只在字符合方向为 R 且 `Bidi_Mirrored=Yes` 时换成镜像字形。

两条都不触发，所以跳过整套算法不改变输出。这是规范推论，不是"跑起来看着没问题"。

移除的动机是许可：FriBidi 是 LGPL-2.1，也是 libass 依赖树里唯一带 copyleft 的组件。去掉它之后，剩下的依赖全部是宽松型——libass 自身 ISC、HarfBuzz Old MIT、FreeType FTL。

## 与上游的区别

| 能力 | 上游 0.17.5 | 本分支 |
|---|---|---|
| RTL 语言（阿拉伯语、希伯来语等） | 正确 | 顺序错乱 |
| 混合 LTR/RTL 文本重排 | 支持 | 不支持 |
| `ASS_FEATURE_BIDI_BRACKETS` | 可用 | 调用返回 −1 |
| 阿拉伯语简单整形（`AssShapingLevel` 为 SIMPLE 时） | FriBidi 承担 | 一律走 HarfBuzz |
| LTR 语言（中英日韩等） | 正常 | 正常 |
| HarfBuzz OpenType 复杂整形 | 不受影响 | 不受影响 |
| ASS 标签与特效 | 不受影响 | 不受影响 |
| 对外 API / ABI | — | 未改，`ass.h` 与 `ass_types.h` 的签名不变 |
| 依赖 | 需要 FriBidi（LGPL-2.1） | 不需要，且未新增任何依赖 |

## 实现

核心逻辑只有三处。

1. 嵌入层级全置 0

```c
for (i = 0; i < len; i++)
    shaper->emblevels[i] = 0;
```

2. 重排映射取恒等

```c
for (i = 0; i < len; i++)
    shaper->cmap[i] = i;
```

3. 整形一律走 HarfBuzz——删掉 `shape_fribidi()`，`ass_shaper_shape()` 直接调用 `shape_harfbuzz()`。

此外 `ass_resolve_base_direction()` 恒返回 LTR。

代码里保留了一组 `FriBidi*` 类型名（`FriBidiChar`、`FriBidiCharType`、`FriBidiParType`、`FriBidiLevel`、`FriBidiStrIndex`），目的是让其余源码不必改动即可编译。这些名字都在内部头（`ass_render.h`、`ass_shaper.h`）里，libass 对外只安装 `ass.h` 与 `ass_types.h`，因此它们不构成对外 API/ABI。

实际用到的两个常量取 FriBidi 公开 ABI 的真实取值：

```c
#define FRIBIDI_PAR_LTR   0x00000110u  /* FRIBIDI_TYPE_LTR_VAL */
#define FRIBIDI_PAR_ON    0x00000040u  /* FRIBIDI_TYPE_ON_VAL，上游用它表示"自动检测" */
```

## 改动范围

相对上游 0.17.5，共 9 个文件 **+67 / −206 行**。

| 文件 | 改动 |
|---|---|
| `meson.build` | 去掉 fribidi 依赖声明 |
| `configure.ac` | 去掉 fribidi pkg-config 检测 |
| `libass/ass_shaper.h` | 去掉 `#include <fribidi.h>`，改为内部类型与常量定义 |
| `libass/ass_shaper.c` | 核心：BiDi 计算改为固定 LTR，删掉 `shape_fribidi()`，清掉随之无用的字段与函数 |
| `libass/ass_render.h` | 去掉 `#include <fribidi.h>`，补一个 `FriBidiChar` typedef |
| `libass/ass_render.c` | 去掉 `ass_shaper_set_bidi_brackets` 调用 |
| `libass/ass.c` | 去掉 `BIDI_BRACKETS` feature 位 |
| `libass/ass_compat.h` | 去掉 Windows 静态 FriBidi 的适配 |
| `libass/ass.h` | 更新 `BIDI_BRACKETS` 的注释 |

`libass-0.17.5-nofribidi.patch` 是完整补丁，可以打到任何一份干净的 0.17.5 上。已核对：把补丁打到上游快照后得到的树，与本分支的补丁提交逐文件 SHA256 全等（136 / 136 个文件）。**补丁必须用 LF 行尾**，CRLF 会被 `git apply` 拒绝。

## 注意事项

- **不要用于 RTL 内容。** 阿拉伯语、希伯来语以及任何混合 LTR/RTL 的文本都会排错。这是设计范围，不是待办事项。
- **`ASS_FEATURE_BIDI_BRACKETS` 不可用。** 用 `ass_track_set_feature()` 启用它返回 −1，调用方需要处理这个返回值。
- **版本号与上游相同。** `ass_library_version()` 返回 `0x01705000`，`RELEASEVERSION` 是 `0.17.5`，从版本号区分不出这一支。要在自己的构建里区分它，请自行加标记。
- **消费 `ASS_Image` 时必须按 `0xRRGGBBTT` 解读颜色。** `TT` 是 ASS 式透明度（0x00 = 完全不透明），真 `alpha = 0xFF − TT`。依据是 `ass.h` 里该字段的注释、`ass_parse.h` 的 `_r/_g/_b/_a` 宏，以及 FFmpeg `libavfilter/vf_subtitles.c` 里那行 `libass stores an RGBA color in the format RRGGBBTT`。读成 `RRGGBBAA` 会让所有图块变全透明；把最低字节当成红分量，则红分量为 0 的图块（例如 `\c&HFF0000&` 的纯蓝）会被整块丢弃。
- **没有可对照的上游或发行版补丁。** 检索不到公开的"移除 FriBidi"实现，评估时请以本仓库的补丁与下面的验证判据为准。
- **性能没有实测数据。** 跳过的 BiDi 分析在纯 LTR 输入上本来就是恒等处理，省下的量级未测量。
- **验证范围有限**，见下文。

## 构建

Meson（推荐）：

```bash
meson setup build --buildtype=release -Dtest=disabled
ninja -C build
```

Autotools：

```bash
./autogen.sh
./configure --disable-test
make
```

依赖：FreeType2 ≥ 9.17.3、HarfBuzz ≥ 1.2.3、可选 fontconfig、可选 libunibreak。**不再需要 FriBidi。**

## 验证与已知边界

`tools/check.sh` 一条命令做完四件事：编 libass（打开 `compare`）、跑上游自带的官方回归、编并跑 `tools/probe.c` 打印每帧摘要、再断言产物里没有 FriBidi。

| 检查 | 判据 |
|---|---|
| 编译无警告 | `ninja` 输出里筛 `warning\|error` 为空 |
| FriBidi 不在产物里 | meson 配置日志无 fribidi、`nm` 符号表 0 个、构建目录无 `libfribidi*` |
| FriBidi 不在运行时 | libass 自己打的 `Shaper: HarfBuzz-ng … (COMPLEX, no FriBidi)` |
| `BIDI_BRACKETS` 已下线 | `ass_track_set_feature(track, ASS_FEATURE_BIDI_BRACKETS, 1)` 返回 −1 |
| 渲染行为未变 | 官方回归四个数字仍是 2.464 / 1.412 / 4.919 / 0.728 |

`tools/differential.sh` 做原版对照：把干净基线配上本机的一份 fribidi 编成原版，与打了补丁的版本喂同一份输入，对每帧算一个覆盖全部图块几何、颜色与位图字节的摘要。结果是三个时间点两边逐字相同（`640976dd3c00be03` / `f51343e0dc30e078` / `549f67681ffcbb79`），frames 文件 md5 相同，官方回归四个数字相同。全流程唯一一处差异是 `Shaper:` 那行日志——那正是本分支的目的。

需要知道的是，上游那套金标参考图并不全过（`sub1` 的 2500 ms 是 FAIL 级），所以官方回归的判据是"这四个数字有没有变化"，而不是"有没有全过"。

**已验证的范围**：`test.ass` 的三个时间点，逐帧摘要与原版逐字相同；官方回归四个数字相同；产物里没有任何 FriBidi 痕迹。

**未覆盖的范围**：

- 输入只有一个样本的三个时间点，不等于在任意输入上都等价。
- 三条 LTR 邻近路径未专测：`\fay` 斜切（它读重排映射）、段落分隔符（U+000D / U+0085 / U+2029，以及启用 `ASS_FEATURE_WRAP_UNICODE` 时的 U+001C..1E）、显式设为 `ASS_SHAPING_SIMPLE` 的调用方。这三条不是 RTL，但删掉的代码原本也管它们。
- 官方回归那 0.7%–0.8% 的边缘像素差异未归因到底。它是原版与补丁版共有的，与本分支无关，最可能是构建侧的原因（本机构建的 ASM 关闭）。
- `upstream` 分支的内容是未打补丁的原版，但它没有上游的提交历史与标签，所以"基线自洽"不等于"基线等于官方"。

## 许可证

libass 本身与本分支的改动都按 **ISC License** 分发。分发时请保留 libass 的原始版权声明与 ISC 许可证文本（见 `COPYING`）。

## 参考

- libass 官方仓库：<https://github.com/libass/libass>
- FriBidi：<https://github.com/fribidi/fribidi>
- 本分支的完整改动：`libass-0.17.5-nofribidi.patch`，或本仓库的提交历史
