# libass-nofribidi

libass 的无 FriBidi 分支 — 移除 FriBidi 依赖，仅支持 LTR 语言。

## 分支说明

| 分支 | 说明 |
|------|------|
| `main` | 无 FriBidi 版本（当前分支） |
| `upstream` | 原版 libass 基线（含 FriBidi） |

基于 **libass 0.17.5**。

## 为什么移除 FriBidi？

- **许可证**：FriBidi 使用 LGPL-2.1+，静态链接时有 copyleft 义务。移除后整个依赖栈都是宽松型许可证（ISC / MIT / FreeType License）。
- **依赖精简**：少一个外部依赖，构建更简单。
- **适用场景**：只需要处理 LTR 语言（中文、英文、日文、韩文等）的字幕渲染。

## 失去的功能

移除 FriBidi 意味着移除了完整的 Unicode 双向文本（BiDi）算法支持：

| 功能 | 状态 |
|------|------|
| RTL 语言（阿拉伯语、希伯来语等）正确显示 | ❌ 顺序错乱 |
| 混合 LTR/RTL 文本排版 | ❌ 无法正确重排 |
| BiDi 括号匹配 | ❌ 移除 |
| 阿拉伯语简单整形（SIMPLE 模式） | ❌ 移除，强制走 HarfBuzz |
| LTR 语言（中英日韩等） | ✅ 完全正常 |
| HarfBuzz OpenType 复杂整形 | ✅ 不受影响 |
| 所有 ASS 特效 | ✅ 不受影响 |
| 对外 API / ABI | ✅ 兼容（类型定义保留） |

## 修改的文件

共修改 **9 个文件**，净减少 **132 行代码**：

| 文件 | 改动 |
|------|------|
| `meson.build` | 移除 fribidi 依赖声明 |
| `configure.ac` | 移除 fribidi pkg-config 检测 |
| `libass/ass_shaper.h` | 移除 `#include <fribidi.h>`，添加兼容 typedef 和常量 |
| `libass/ass_shaper.c` | 核心改动：BiDi 计算改为固定 LTR，删除 shape_fribidi |
| `libass/ass_render.h` | 移除 `#include <fribidi.h>`，添加 `FriBidiChar` typedef |
| `libass/ass_render.c` | 移除 `ass_shaper_set_bidi_brackets` 调用 |
| `libass/ass.c` | 移除 BIDI_BRACKETS feature 支持 |
| `libass/ass_compat.h` | 移除 Windows 静态 fribidi 适配 |
| `libass/ass.h` | 更新 BIDI_BRACKETS 注释 |

## 核心改动原理

1. **所有嵌入级别设为 0（LTR）**
   ```c
   for (i = 0; i < len; i++)
       shaper->emblevels[i] = 0;
   ```

2. **重排映射为恒等映射**
   ```c
   for (i = 0; i < len; i++)
       shaper->cmap[i] = i;
   ```

3. **强制使用 HarfBuzz 复杂整形**
   - 删除 `shape_fribidi()` 函数
   - `ass_shaper_shape()` 直接调用 `shape_harfbuzz()`

4. **保留 FriBidi 兼容类型定义**
   - `FriBidiChar`、`FriBidiCharType`、`FriBidiParType`、`FriBidiLevel`、`FriBidiStrIndex`
   - 保证 API/ABI 兼容，现有调用方无需修改代码

## 构建

### Meson（推荐）

```bash
meson setup build --buildtype=release -Dtest=disabled
ninja -C build
```

### Autotools

```bash
./autogen.sh
./configure --disable-test
make
```

### 依赖

- FreeType2 (>= 9.17.3)
- HarfBuzz (>= 1.2.3)
- Fontconfig（可选）
- libunibreak（可选）
- **不再需要 FriBidi**

## 同步上游更新

当 libass 发布新版本时，按以下步骤更新：

```bash
# 1. 下载新版本源码
# 2. 切换到 upstream 分支，用新版本替换
git checkout upstream
# ... 替换源码 ...
git add -A
git commit -m "upstream: libass X.Y.Z"

# 3. 切换回 main 分支，rebase
git checkout main
git rebase upstream

# 4. 如果有冲突，解决后继续
# 冲突通常集中在 ass_shaper.c / meson.build
```

### 常见冲突处理

- **meson.build**：如果上游改了依赖列表，保留新版本，删除 fribidi 相关行
- **ass_shaper.c**：如果上游改了 shape 函数结构，用我们的简化版替换 BiDi 计算部分
- **ass_shaper.h / ass_render.h**：如果上游加了新函数声明，添加到对应位置

## 许可证

本分支基于 libass 源码修改，遵循 **ISC License**。

- libass 本身：ISC License
- 本分支的修改：ISC License（与上游一致）
- 依赖：全部为宽松型许可证

**合规要求**：分发时保留 libass 原始版权声明和 ISC 许可证文本。

## 与原版的性能对比

理论上 no-fribidi 版本应该**更快**，因为：
- 跳过了 BiDi 字符类型分析（`fribidi_get_bidi_types`）
- 跳过了 BiDi 嵌入级别计算（`fribidi_get_par_embedding_levels`）
- 跳过了行级重排序（`fribidi_reorder_line`）

对于纯 LTR 文本，原版 FriBidi 也不会做太多重排工作，但仍然会运行完整的 BiDi 算法来确认"确实都是 LTR"。no-fribidi 版本直接跳过所有这些步骤。

## 适用场景

✅ 适合：
- 只处理中文/英文/日文/韩文等 LTR 语言字幕
- 需要静态链接且不想受 LGPL 约束
- 嵌入式系统，需要尽量精简依赖
- 对依赖数量有严格限制的项目

❌ 不适合：
- 需要处理阿拉伯语、希伯来语等 RTL 语言
- 需要正确显示混合 LTR/RTL 文本
- 需要 BiDi 括号匹配等高级特性

## 参考

- [libass 官方仓库](https://github.com/libass/libass)
- [FriBidi 官网](https://github.com/fribidi/fribidi)
- KISS Linux no-fribidi 补丁（参考了思路）
