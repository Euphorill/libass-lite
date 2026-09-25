/*
 * tools/probe.c —— libass 无 FriBidi 分支的测试台
 *
 * 为什么写这个：仓库原来那两个测试程序都编译不过。
 *   - test_simple.c 用了不存在的 ass_get_available_features()，以及八个不存在的
 *     ASS_FEATURE_* 枚举成员（libass 的 ASS_Feature 只有四个）。
 *   - test_render.c 用了同样的东西，另外它写的是 PNG。
 * 而且它们对 ASS_Image.color 的解读是错的，导致颜色全错、红分量为 0 的图块被整块丢掉。
 *
 * 本程序：
 *   1. 只用公开 API（ass_library_version、ass_track_set_feature 等）；
 *   2. 用真正的办法探测 feature —— 调 ass_track_set_feature，不支持时返回 -1；
 *   3. 正确解读 ASS_Image.color（见下面注释）；
 *   4. 对每一帧算一个摘要（所有图块的几何、颜色与位图字节），用于把两个构建
 *      逐字节对照：原版与打过补丁的版本对同一输入必须给出同一个摘要。
 *
 * 用法: probe <字幕.ass> [输出前缀] [毫秒 ...]
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ass/ass.h>

/* ------------------------------------------------------------------
 * ASS_Image.color 的格式：0xRRGGBBTT
 *
 * TT 是 ASS 式透明度：0x00 表示完全不透明，0xFF 表示全透明。
 * 所以真 alpha = 0xFF - TT。依据有三处：
 *   - ass.h 里该字段的注释写的是 "RGBA"；
 *   - ass_parse.h 里的访问宏 _r/_g/_b/_a，其中 _a(c) = c & 0xFF；
 *   - FFmpeg 的 libavfilter/vf_subtitles.c 里那行注释
 *     "libass stores an RGBA color in the format RRGGBBTT" 及其 AA(c) 宏。
 *
 * 原来那两个测试程序把 (c>>16) 当成红、(c>>24) 当成 alpha，于是把绿当红、
 * 把红当透明度，红分量为 0 的图块（例如 \c&HFF0000& 的纯蓝）会被直接丢弃。
 * ------------------------------------------------------------------ */
static unsigned c_r(uint32_t c) { return (c >> 24) & 0xff; }
static unsigned c_g(uint32_t c) { return (c >> 16) & 0xff; }
static unsigned c_b(uint32_t c) { return (c >> 8) & 0xff; }
static unsigned c_transparency(uint32_t c) { return c & 0xff; }
static unsigned c_alpha(uint32_t c) { return 255 - (c & 0xff); }

/* 把 libass 自己的日志原样打出来：那行 "(COMPLEX, no FriBidi)" 是最直接的证据 */
static void msg_cb(int level, const char *fmt, va_list args, void *ctx)
{
    (void)ctx;
    printf("[ass:%d] ", level);
    vprintf(fmt, args);
    printf("\n");
}

/* FNV-1a 64：对原始输出做摘要，不经过任何消费端代码 */
static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = p;
    while (n--) { h ^= *b++; h *= 1099511628211ULL; }
    return h;
}

/*
 * 一帧的摘要：把所有图块的几何、颜色与位图字节都算进去。
 * 注意位图的最后一行可能没有补齐到 stride，安全长度是 stride*(h-1) + w。
 */
static uint64_t frame_digest(const ASS_Image *img)
{
    uint64_t h = 1469598103934665603ULL;
    for (const ASS_Image *i = img; i; i = i->next) {
        int32_t geo[6] = { i->w, i->h, i->stride, i->dst_x, i->dst_y, (int32_t)i->color };
        h = fnv(h, geo, sizeof(geo));
        size_t safe = (size_t)i->stride * (i->h - 1) + i->w;
        h = fnv(h, i->bitmap, safe);
    }
    return h;
}

/* ---------- 24 位 BMP，仅用于肉眼查看 ---------- */
#pragma pack(push, 1)
typedef struct {
    unsigned char  sig[2];
    unsigned int   fileSize;
    unsigned short r1, r2;
    unsigned int   dataOffset, headerSize;
    int            width, height;
    unsigned short planes, bitCount;
    unsigned int   compression, imageSize;
    int            xppm, yppm;
    unsigned int   clrUsed, clrImportant;
} BMPHeader;
#pragma pack(pop)

static int write_bmp(const char *path, int w, int h, const unsigned char *rgba, int stride)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int row = w * 3, pad = (4 - (row % 4)) % 4, isize = (row + pad) * h;
    BMPHeader hd;
    memset(&hd, 0, sizeof(hd));
    hd.sig[0] = 'B'; hd.sig[1] = 'M';
    hd.fileSize = (unsigned)sizeof(hd) + isize;
    hd.dataOffset = sizeof(hd);
    hd.headerSize = 40;
    hd.width = w; hd.height = h;
    hd.planes = 1; hd.bitCount = 24;
    hd.imageSize = isize;
    hd.xppm = hd.yppm = 2835;
    fwrite(&hd, sizeof(hd), 1, f);
    unsigned char *rb = (unsigned char *)malloc(row + pad);
    if (!rb) { fclose(f); return 0; }
    memset(rb + row, 0, pad);
    for (int y = h - 1; y >= 0; y--) {
        const unsigned char *s = rgba + (size_t)y * stride;
        unsigned char *d = rb;
        for (int x = 0; x < w; x++) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; s += 4; d += 3; }
        fwrite(rb, 1, row + pad, f);
    }
    free(rb);
    fclose(f);
    return 1;
}

/* src-over 合成，alpha 用取反后的真值 */
static void blend(unsigned char *dst, int dw, int dh, int stride, const ASS_Image *img)
{
    for (const ASS_Image *i = img; i; i = i->next) {
        unsigned tr = c_transparency(i->color);
        if (tr == 0xff) continue;
        unsigned r = c_r(i->color), g = c_g(i->color), b = c_b(i->color), ca = 255 - tr;
        for (int y = 0; y < i->h; y++) {
            int dy = i->dst_y + y;
            if (dy < 0 || dy >= dh) continue;
            for (int x = 0; x < i->w; x++) {
                int dx = i->dst_x + x;
                if (dx < 0 || dx >= dw) continue;
                unsigned ma = i->bitmap[(size_t)y * i->stride + x];
                if (!ma) continue;
                unsigned a = ma * ca / 255;
                if (!a) continue;
                unsigned char *p = dst + (size_t)dy * stride + dx * 4;
                unsigned inv = 255 - a;
                p[0] = (unsigned char)((r * a + p[0] * inv) / 255);
                p[1] = (unsigned char)((g * a + p[1] * inv) / 255);
                p[2] = (unsigned char)((b * a + p[2] * inv) / 255);
                p[3] = (unsigned char)(a + p[3] * inv / 255);
            }
        }
    }
}

int main(int argc, char *argv[])
{
    const char *sub = (argc >= 2) ? argv[1] : "test.ass";
    const char *prefix = (argc >= 3) ? argv[2] : NULL;
    const int FW = 1280, FH = 720;

    printf("libass 版本号: 0x%08x\n", ass_library_version());

    ASS_Library *lib = ass_library_init();
    if (!lib) { printf("[FAIL] ass_library_init\n"); return 1; }
    ass_set_message_cb(lib, msg_cb, NULL);

    ASS_Renderer *ren = ass_renderer_init(lib);
    if (!ren) { printf("[FAIL] ass_renderer_init\n"); return 1; }
    ass_set_frame_size(ren, FW, FH);
    ass_set_storage_size(ren, FW, FH);
    ass_set_fonts(ren, NULL, "sans-serif", ASS_FONTPROVIDER_AUTODETECT, NULL, 1);

    ASS_Track *tr = ass_read_file(lib, (char *)sub, NULL);
    if (!tr) { printf("[FAIL] ass_read_file(%s)\n", sub); return 1; }
    printf("字幕: %s   样式数=%d  事件数=%d\n", sub, tr->n_styles, tr->n_events);

    printf("feature 探测（不支持时返回 -1）:\n");
    printf("  ASS_FEATURE_BIDI_BRACKETS     -> %d\n",
           ass_track_set_feature(tr, ASS_FEATURE_BIDI_BRACKETS, 1));
    printf("  ASS_FEATURE_WRAP_UNICODE      -> %d\n",
           ass_track_set_feature(tr, ASS_FEATURE_WRAP_UNICODE, 1));
    printf("  ASS_FEATURE_INCOMPATIBLE_EXTENSIONS -> %d\n",
           ass_track_set_feature(tr, ASS_FEATURE_INCOMPATIBLE_EXTENSIONS, 1));
    printf("  ASS_FEATURE_WHOLE_TEXT_LAYOUT -> %d\n",
           ass_track_set_feature(tr, ASS_FEATURE_WHOLE_TEXT_LAYOUT, 1));

    int stride = FW * 4;
    unsigned char *buf = (unsigned char *)calloc((size_t)stride * FH, 1);

    printf("\n逐帧摘要（原版与补丁版对同一输入必须给出同一个摘要）\n");
    char *end = NULL;
    for (int k = 3; k < argc; k++) {
        long long ts = strtoll(argv[k], &end, 10);
        int detect = 0;
        ASS_Image *img = ass_render_frame(ren, tr, ts, &detect);
        int n = 0;
        for (ASS_Image *p = img; p; p = p->next) n++;
        printf("FRAME %-7lld blocks=%-3d digest=%016llx", ts, n,
               (unsigned long long)frame_digest(img));
        if (img)
            printf("  first=%dx%d@(%d,%d) color=0x%08x R=%u G=%u B=%u TT=%u alpha=%u",
                   img->w, img->h, img->dst_x, img->dst_y, img->color,
                   c_r(img->color), c_g(img->color), c_b(img->color),
                   c_transparency(img->color), c_alpha(img->color));
        printf("\n");

        if (prefix && img && buf) {
            memset(buf, 0, (size_t)stride * FH);
            blend(buf, FW, FH, stride, img);
            char name[1024];
            snprintf(name, sizeof(name), "%s.%lld.bmp", prefix, ts);
            write_bmp(name, FW, FH, buf, stride);
        }
    }

    free(buf);
    ass_free_track(tr);
    ass_renderer_done(ren);
    ass_library_done(lib);
    return 0;
}
