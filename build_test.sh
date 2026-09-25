@echo off
REM ============================================
REM  libass-lite 一键测试脚本 (Windows / MSYS2 MinGW64)
REM ============================================
REM
REM 用法:
REM   1. 打开 MSYS2 MinGW 64-bit 终端
REM   2. cd 到 libass-lite 目录
REM   3. ./build_test.sh
REM
REM 前置依赖 (MSYS2):
REM   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-pkg-config
REM   pacman -S mingw-w64-x86_64-freetype mingw-w64-x86_64-harfbuzz
REM   pacman -S meson ninja
REM

echo "=== libass-lite 一键测试 ==="
echo ""

# 检查 meson
if ! command -v meson &> /dev/null; then
    echo "[错误] 找不到 meson，请先安装: pacman -S meson ninja"
    exit 1
fi

# 检查 pkg-config
if ! pkg-config --exists freetype2; then
    echo "[错误] 找不到 freetype2，请先安装: pacman -S mingw-w64-x86_64-freetype"
    exit 1
fi

if ! pkg-config --exists harfbuzz; then
    echo "[错误] 找不到 harfbuzz，请先安装: pacman -S mingw-w64-x86_64-harfbuzz"
    exit 1
fi

echo "[1/4] 配置 meson..."
if [ ! -d build ]; then
    meson setup build --buildtype=debug -Dtest=disabled
else
    echo "      build 目录已存在，跳过"
fi

if [ $? -ne 0 ]; then
    echo "[错误] meson setup 失败"
    exit 1
fi

echo ""
echo "[2/4] 编译 libass..."
ninja -C build -j4

if [ $? -ne 0 ]; then
    echo "[错误] 编译失败"
    exit 1
fi

echo ""
echo "[3/4] 编译测试程序..."

# 找 libass 库文件
LIBASS_LIB=""
if [ -f build/libass/libass.dll.a ]; then
    LIBASS_LIB="build/libass/libass.dll.a"
elif [ -f build/libass/libass.a ]; then
    LIBASS_LIB="build/libass/libass.a"
fi

if [ -z "$LIBASS_LIB" ]; then
    echo "[错误] 找不到编译好的 libass 库"
    ls build/libass/
    exit 1
fi

gcc test_simple.c -o test_simple.exe \
    -Ilibass -Ibuild \
    $LIBASS_LIB \
    $(pkg-config --cflags --libs freetype2 harfbuzz) \
    -lgdi32

if [ $? -ne 0 ]; then
    echo "[错误] 测试程序编译失败"
    exit 1
fi

echo ""
echo "[4/4] 运行测试..."

if [ ! -f test.ass ]; then
    echo "[错误] 找不到 test.ass"
    exit 1
fi

./test_simple.exe test.ass test_output.bmp

RESULT=$?

echo ""
if [ $RESULT -eq 0 ]; then
    echo "=== 全部通过 ✓ ==="
    echo ""
    echo "打开 test_output.bmp 查看渲染结果"
else
    echo "=== 测试失败 ✗ ==="
fi

exit $RESULT
