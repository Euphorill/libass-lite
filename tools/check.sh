#!/bin/bash
#
# tools/check.sh —— 一条命令做完四件事
#
#   1. 编 libass（顺带打开 compare，用于官方回归测试）
#   2. 跑上游自带的回归测试（拿官方参考图做对照）
#   3. 编并跑 tools/probe.c，打印每帧摘要
#   4. 断言产物里不含 FriBidi（LGPL-2.1，绝不能进产物）
#
# 必须在 MSYS2 的 MINGW64 环境里跑。用：
#   MSYSTEM=MINGW64 /mingw64/bin/bash tools/check.sh
# 或者打开 "MSYS2 MINGW64" 终端后直接 ./tools/check.sh
#
set -u

if [ "${MSYSTEM:-}" != "MINGW64" ]; then
    echo "[错误] 请用 MSYS2 的 MINGW64 环境。当前 MSYSTEM='${MSYSTEM:-未设置}'"
    echo "       打开 “MSYS2 MINGW64” 终端再跑，或设 MSYSTEM=MINGW64 并把 /mingw64/bin 放到 PATH 前面。"
    echo "       （进错环境的表现是 meson 报 Unknown compiler(s)，因为找不到 mingw64 的 gcc。）"
    exit 1
fi

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1

for t in gcc meson ninja pkg-config; do
    command -v "$t" >/dev/null 2>&1 || { echo "[错误] 找不到 $t"; exit 1; }
done

echo "### 1/4 配置（compare 打开，用于官方回归测试）"
if [ ! -d build ]; then
    meson setup build -Dcompare=enabled -Dlibunibreak=disabled --buildtype=release || exit 1
else
    echo "    build/ 已存在，跳过配置"
fi

echo
echo "### 2/4 编译（应当没有任何警告）"
ninja -C build || exit 1

echo
echo "### 3/4 官方回归测试"
echo "    预期四个数字（与本分支记录一致）："
echo "      sub1 500ms    2.464  BAD"
echo "      sub1 1500ms   1.412  GOOD"
echo "      sub1 2500ms   4.919  FAIL"
echo "      sub2 153000ms 0.728  GOOD"
echo "    这四个数字若发生变化，说明渲染行为被改动了，必须查清原因。"
echo "    （注意：上游参考图在本地这套工具链下本来就不全过，所以判据是「数字是否变化」，不是「是否全过」。）"
echo
./build/compare/compare.exe compare/test
echo "    compare 退出码: $?  （3 = 有 FAIL 级差异，与本地基线一致）"

echo
echo "### 附：探针"
gcc -O1 -o build/probe.exe tools/probe.c -Ibuild/libass \
    $(pkg-config --cflags freetype2 harfbuzz) \
    build/libass/libass.a \
    $(pkg-config --libs freetype2 harfbuzz) -liconv -lgdi32 || exit 1
./build/probe.exe test.ass build/probe_out 1000 4000 4500 2>&1 | grep -Ev '^\[ass:[67]\]'

echo
echo "### 4/4 断言：产物里不含 FriBidi"
./tools/verify-no-fribidi.sh build
VERIFY=$?

echo
echo "### 完成。图片在 build/probe_out.*.bmp"
exit "$VERIFY"
