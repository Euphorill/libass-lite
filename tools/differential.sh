#!/bin/bash
#
# differential.sh —— 等价性验证：原版 libass 与补丁版对同一输入必须完全一致
#
#   原版   = 干净基线 53a1d57 + 本机 fribidi（解在一次性目录里，不进系统、不进产物）
#   补丁版 = 仓库当前 HEAD
#
# 判据：两边 probe 打印的每帧 digest 逐字相同，且官方回归的四个数字相同。
#
# 关于 FriBidi：本机的 fribidi 是一个解在工作目录里的一次性副本，只通过
# PKG_CONFIG_PATH / PATH 供给这次对照构建。系统里没有装它，产物里也没有它
# （产物侧由 tools/verify-no-fribidi.sh 断言）。
#
# 顶部路径：
#   REPO  —— 本仓库（默认取本脚本所在仓库）
#   WORK  —— 放一次性 fribidi 与中间产物的目录。**默认落到系统临时目录，不是仓库**，
#            免得 stock/ 与 diffout/ 被误提交。
#   FRIBIDI_PREFIX —— 一次性 fribidi 的安装前缀（其下有 lib/pkgconfig 与 bin）
#
set -u
export MSYSTEM=MINGW64
export PATH=/mingw64/bin:/usr/bin:$PATH

REPO=${REPO:-$(cd "$(dirname "$0")/.." && pwd)}
WORK=${WORK:-${TMPDIR:-/tmp}/libass-differential}
FRIBIDI_PREFIX=${FRIBIDI_PREFIX:-$WORK/fribidi-local/mingw64}
PC=$FRIBIDI_PREFIX/lib/pkgconfig
TS="1000 4000 4500"
OUT=$WORK/diffout
mkdir -p "$WORK"
case $WORK in
    "$REPO"/*|"$REPO") echo "WORK 不能放在仓库里（$WORK）。换一个位置。" >&2; exit 1;;
esac
rm -rf "$OUT" && mkdir -p "$OUT"

if [ ! -d "$PC" ]; then
    echo "找不到 fribidi 的 pkg-config 目录：$PC" >&2
    echo "先在本机放一份不装进系统的 fribidi，再用 FRIBIDI_PREFIX 指过来（见 README.nofribidi.md 的「测试」一节）。" >&2
    exit 1
fi

echo "################ A. 原版（干净基线 + 本机 fribidi） ################"
rm -rf "$WORK/stock" && mkdir -p "$WORK/stock"
git -C "$REPO" archive 53a1d57 | tar -x -C "$WORK/stock" || exit 1
cd "$WORK/stock" || exit 1
export PKG_CONFIG_PATH="$PC"
# fribidi 在本机是动态链接的（libfribidi-0.dll），运行时要从那个一次性目录里找它。
# 它只存在于这里，不进系统、不进产物。
export PATH="$FRIBIDI_PREFIX/bin:$PATH"
echo "  fribidi 版本: $(pkg-config --modversion fribidi 2>&1)"
echo "  运行时 DLL 来自: $FRIBIDI_PREFIX/bin（已置入 PATH）"
echo "  基线 configure.ac 里 fribidi 出现次数: $(grep -c -i fribidi configure.ac)"
meson setup build -Dcompare=enabled -Dlibunibreak=disabled --buildtype=release > "$OUT/setup.log" 2>&1
echo "  meson 退出码: $?"
grep -Ei "dependency fribidi|Font providers" "$OUT/setup.log" | head -3 | sed 's/^/    /'
ninja -C build > "$OUT/ninja.log" 2>&1
echo "  ninja 退出码: $?   警告数: $(grep -ci 'warning' "$OUT/ninja.log")"

echo "  --- 原版跑官方回归 ---"
./build/compare/compare.exe compare/test > "$OUT/stock.cmp" 2>&1
sed 's/^/    /' "$OUT/stock.cmp"

echo "  --- 原版探针 ---"
gcc -O1 -o build/probe.exe "$REPO/tools/probe.c" -Ibuild/libass \
    $(pkg-config --cflags freetype2 harfbuzz fribidi) \
    build/libass/libass.a \
    $(pkg-config --libs freetype2 harfbuzz fribidi) -liconv -lgdi32 || exit 1
./build/probe.exe "$REPO/test.ass" "$OUT/bmp_stock" $TS 2>&1 | grep -E '^FRAME' > "$OUT/stock.frames"
sed 's/^/    /' "$OUT/stock.frames"
./build/probe.exe "$REPO/test.ass" /dev/null $TS 2>&1 | grep -E 'Shaper' > "$OUT/stock.shaper"

echo
echo "################ B. 补丁版（仓库 HEAD） ################"
unset PKG_CONFIG_PATH
cd "$REPO" || exit 1
rm -rf build
./tools/check.sh > "$OUT/check.log" 2>&1
echo "  check.sh 退出码: $?"
grep -E 'ninja 退出码|Time 0:|Only |compare 退出码|结论：|\[通过\]|\[失败\]' "$OUT/check.log" | sed 's/^/    /'
grep -E '^FRAME' "$OUT/check.log" > "$OUT/patched.frames"
grep -E 'Shaper' "$OUT/check.log" > "$OUT/patched.shaper"
echo "  --- 补丁版探针 ---"
sed 's/^/    /' "$OUT/patched.frames"

echo
echo "################ C. 摘要对照 ################"
H1=$(md5sum < "$OUT/stock.frames" | cut -d' ' -f1)
H2=$(md5sum < "$OUT/patched.frames" | cut -d' ' -f1)
printf "  原版   frames 摘要: %s\n" "$H1"
printf "  补丁版 frames 摘要: %s\n" "$H2"
if [ "$H1" = "$H2" ] && [ -n "$H1" ]; then
    echo "  => 两边逐字相同"
else
    echo "  => 两边不同。原版："
    sed 's/^/       /' "$OUT/stock.frames"
    echo "     补丁版："
    sed 's/^/       /' "$OUT/patched.frames"
fi
echo
echo "################ D. 预期内的差异（唯一一处） ################"
echo "  原版   : $(head -1 "$OUT/stock.shaper")"
echo "  补丁版 : $(head -1 "$OUT/patched.shaper")"
echo "  这是补丁有意改的日志行（补丁的目的就是让这里不再出现 FriBidi），不是渲染差异。"

echo
echo "################ E. 官方回归数字对照 ################"
echo "  原版:"
grep -E 'Time 0:' "$OUT/stock.cmp" | sed 's/^/    /'
echo "  补丁版:"
grep -E 'Time 0:' "$OUT/check.log" | sed 's/^/    /'
