#!/bin/bash
#
# tools/verify-no-fribidi.sh —— 断言产物里不含 FriBidi
#
# 为什么需要它：FriBidi 是 LGPL-2.1。本机装它**只为**编一份原版 libass 做对照，
# 绝不能进入任何交付产物（PC 的 libass.a/.so/.exe、Android 的 .so 与 APK）。
#
# 隔离在结构上已经成立：打了补丁的源码里没有任何 fribidi 引用，meson 与
# configure 都不声明这个依赖，所以本机装了它也链不上。这个脚本把"结构性成立"
# 变成可断言的检查，防止将来有人把依赖加回去、或把对照构建的产物拷进来。
#
# 用法: tools/verify-no-fribidi.sh [构建目录，默认 build] [额外要检查的文件或目录...]
#
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1

BUILD=${1:-build}
shift 2>/dev/null || true
EXTRA="$*"

fail=0
ok()   { printf '  [通过] %s\n' "$1"; }
bad()  { printf '  [失败] %s\n' "$1"; fail=1; }

echo "=== 断言：产物中不含 FriBidi ==="

# 1. meson 配置阶段有没有发现 fribidi
if [ -f "$BUILD/meson-logs/meson-log.txt" ]; then
    if grep -qi 'fribidi' "$BUILD/meson-logs/meson-log.txt"; then
        bad "meson 配置日志里出现了 fribidi"
    else
        ok "meson 配置日志里没有 fribidi"
    fi
else
    printf '  [跳过] 找不到 %s/meson-logs/meson-log.txt\n' "$BUILD"
fi

# 2. 符号表：既不该定义也不该引用 fribidi_*
for lib in $(find "$BUILD" -name 'libass.a' -o -name 'libass.so*' 2>/dev/null); do
    syms=$(nm -A "$lib" 2>/dev/null | grep -ci 'fribidi' || true)
    undef=$(nm -u "$lib" 2>/dev/null | grep -ci 'fribidi' || true)
    if [ "$syms" = "0" ] && [ "$undef" = "0" ]; then
        ok "$(basename "$lib"): 0 个 fribidi 符号（含未定义引用）"
    else
        bad "$(basename "$lib"): 符号 $syms 个、未定义引用 $undef 个"
    fi
done

# 3. 产物里不该出现 libfribidi*
hits=$(find "$BUILD" -iname '*fribidi*' 2>/dev/null | head -5)
if [ -z "$hits" ]; then
    ok "构建目录里没有 libfribidi* 文件"
else
    bad "构建目录里出现 fribidi 相关文件：$hits"
fi

# 4. 额外指定的文件或目录（例如 Android 的 APK 或合并后的 .so）
for target in $EXTRA; do
    if [ ! -e "$target" ]; then
        printf '  [跳过] 不存在: %s\n' "$target"
        continue
    fi
    n=$(find "$target" -iname '*fribidi*' 2>/dev/null | wc -l)
    if [ "$n" = "0" ]; then
        ok "$target: 无 fribidi 相关文件"
    else
        bad "$target: 发现 $n 个 fribidi 相关文件"
    fi
    # 对 .so / .a 再查符号
    for f in $(find "$target" -name '*.so' -o -name '*.a' 2>/dev/null | head -40); do
        s=$(nm -D "$f" 2>/dev/null | grep -ci fribidi || true)
        [ "$s" = "0" ] || bad "$(basename "$f"): $s 个 fribidi 动态符号"
    done
done

echo
if [ "$fail" = "0" ]; then
    echo "结论：通过。产物里没有 FriBidi。"
else
    echo "结论：未通过。上面标了失败的项必须处理——FriBidi 是 LGPL-2.1，不能进产物。"
fi
echo
echo "注意：不要用一句 'strings | grep -i fribidi' 当判据。"
echo "      本库自己的日志里就有 \"(COMPLEX, no FriBidi)\" 这个字符串，那是有意留的，"
echo "      会假阳性。可靠的判据是符号表（第 2 项）与文件清单（第 3、4 项）。"

exit "$fail"
