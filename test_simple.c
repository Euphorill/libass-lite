/*
 * libass-lite 快速测试程序 - BMP 输出版
 *
 * 用法: test_simple.exe <subtitle.ass> <output.bmp>
 *
 * 功能:
 *   1. 初始化 libass
 *   2. 检查可用 features（确认无 FRIBIDI）
 *   3. 加载字幕文件
 *   4. 渲染一帧到 1280x720 画布
 *   5. 保存为 24-bit BMP 图片
 *
 * 特点:
 *   - 只依赖 libass + freetype + harfbuzz
 *   - BMP 输出，无需额外图像库
 *   - 代码极简，便于排查问题
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ass/ass.h>

#pragma pack(push, 1)
typedef struct {
    unsigned char  signature[2];   /* "BM" */
    unsigned int   fileSize;
    unsigned short reserved1;
    unsigned short reserved2;
    unsigned int   dataOffset;
    unsigned int   headerSize;     /* 40 */
    int            width;
    int            height;
    unsigned short planes;
    unsigned short bitCount;       /* 24 */
    unsigned int   compression;    /* 0 = BI_RGB */
    unsigned int   imageSize;
    int            xPelsPerMeter;
    int            yPelsPerMeter;
    unsigned int   clrUsed;
    unsigned int   clrImportant;
} BMPHeader;
#pragma pack(pop)

/*
 * 保存 24-bit BMP
 * data 格式: RGBA 每行 width*4 字节
 * BMP 是 BGR 顺序，且从下往上存
 */
static int write_bmp(const char *filename, int width, int height,
                     const unsigned char *rgba, int stride)
{
    FILE *f = fopen(filename, "wb");
    if (!f) return 0;

    /* 24-bit BMP 每行字节数必须是 4 的倍数 */
    int row_bytes = width * 3;
    int padding = (4 - (row_bytes % 4)) % 4;
    int image_size = (row_bytes + padding) * height;

    BMPHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.signature[0] = 'B';
    hdr.signature[1] = 'M';
    hdr.fileSize = sizeof(BMPHeader) + image_size;
    hdr.dataOffset = sizeof(BMPHeader);
    hdr.headerSize = 40;
    hdr.width = width;
    hdr.height = height;  /* 正数 = bottom-up */
    hdr.planes = 1;
    hdr.bitCount = 24;
    hdr.compression = 0;
    hdr.imageSize = image_size;
    hdr.xPelsPerMeter = 2835;  /* ~72 DPI */
    hdr.yPelsPerMeter = 2835;

    fwrite(&hdr, sizeof(hdr), 1, f);

    /* BMP 从下往上存，BGR 顺序 */
    unsigned char *row_buf = (unsigned char *)malloc(row_bytes + padding);
    if (!row_buf) { fclose(f); return 0; }
    memset(row_buf + row_bytes, 0, padding);

    for (int y = height - 1; y >= 0; y--) {
        const unsigned char *src = rgba + y * stride;
        unsigned char *dst = row_buf;
        for (int x = 0; x < width; x++) {
            /* RGBA -> BGR, 忽略 alpha（背景透明就当黑色） */
            dst[0] = src[2];  /* B */
            dst[1] = src[1];  /* G */
            dst[2] = src[0];  /* R */
            src += 4;
            dst += 3;
        }
        fwrite(row_buf, 1, row_bytes + padding, f);
    }

    free(row_buf);
    fclose(f);
    return 1;
}

/*
 * 把 libass 的 ASS_Image 链合成到 RGBA 缓冲区
 */
static void blend_ass_image(unsigned char *dst, int dst_w, int dst_h,
                            int dst_stride, ASS_Image *img)
{
    for (ASS_Image *i = img; i; i = i->next) {
        unsigned char r = (i->color >> 16) & 0xff;
        unsigned char g = (i->color >> 8) & 0xff;
        unsigned char b = i->color & 0xff;
        unsigned char ca = (i->color >> 24) & 0xff;

        if (ca == 0) continue;

        int x0 = i->dst_x;
        int y0 = i->dst_y;
        int w = i->w;
        int h = i->h;

        for (int y = 0; y < h; y++) {
            int dy = y0 + y;
            if (dy < 0 || dy >= dst_h) continue;

            for (int x = 0; x < w; x++) {
                int dx = x0 + x;
                if (dx < 0 || dx >= dst_w) continue;

                unsigned char mask_a = i->bitmap[y * i->stride + x];
                if (mask_a == 0) continue;

                /* 最终 alpha = mask_alpha * color_alpha / 255 */
                unsigned int a = (unsigned int)mask_a * ca / 255;
                if (a == 0) continue;

                unsigned char *dp = dst + dy * dst_stride + dx * 4;

                if (a >= 255) {
                    dp[0] = r;
                    dp[1] = g;
                    dp[2] = b;
                    dp[3] = 255;
                } else {
                    unsigned int inv = 255 - a;
                    dp[0] = (unsigned char)((r * a + dp[0] * inv) / 255);
                    dp[1] = (unsigned char)((g * a + dp[1] * inv) / 255);
                    dp[2] = (unsigned char)((b * a + dp[2] * inv) / 255);
                    dp[3] = (unsigned char)(a + dp[3] * inv / 255);
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
    const char *ass_file = "test.ass";
    const char *bmp_file = "test_output.bmp";

    if (argc >= 2) ass_file = argv[1];
    if (argc >= 3) bmp_file = argv[2];

    const int W = 1280;
    const int H = 720;

    printf("=== libass-lite 冒烟测试 ===\n\n");

    /* 1. 初始化 */
    ASS_Library *lib = ass_library_init();
    if (!lib) {
        printf("[FAIL] ass_library_init()\n");
        return 1;
    }
    printf("[ OK ] ass_library_init()\n");

    /* 2. 检查 features */
    int feat = ass_get_available_features(lib);
    printf("\n       可用功能:\n");
    if (feat & ASS_FEATURE_HARFBUZZ)    printf("         - HARFBUZZ (复杂整形)\n");
    if (feat & ASS_FEATURE_FONTCONFIG)  printf("         - FONTCONFIG\n");
    if (feat & ASS_FEATURE_DIRECTWRITE) printf("         - DIRECTWRITE (Windows字体)\n");
    if (feat & ASS_FEATURE_CORETEXT)    printf("         - CORETEXT (Apple字体)\n");
    if (feat & ASS_FEATURE_LIBUNIBREAK) printf("         - LIBUNIBREAK (换行)\n");
    if (feat & ASS_FEATURE_ENCA)        printf("         - ENCA (编码检测)\n");
    if (feat & ASS_FEATURE_LIBAVCODEC)  printf("         - LIBAVCODEC (字幕解码)\n");

    if (feat & ASS_FEATURE_FRIBIDI) {
        printf("         - FRIBIDI <-- 仍然存在！\n");
        printf("\n[WARN] 检测到 FRIBIDI，移除不彻底！\n");
    } else {
        printf("         - (无 FRIBIDI) ✓\n");
        printf("\n[ OK ] FriBidi 已成功移除\n");
    }

    /* 3. 创建渲染器 */
    ASS_Renderer *renderer = ass_renderer_init(lib);
    if (!renderer) {
        printf("[FAIL] ass_renderer_init()\n");
        return 1;
    }
    printf("[ OK ] ass_renderer_init()\n");

    ass_set_frame_size(renderer, W, H);
    printf("[ OK ] ass_set_frame_size(%d, %d)\n", W, H);

    /* 4. 设置字体 */
    ass_set_fonts(renderer, NULL, "sans-serif",
                  ASS_FONTPROVIDER_AUTODETECT, NULL, 1);
    printf("[ OK ] ass_set_fonts()\n");

    /* 5. 加载字幕 */
    ASS_Track *track = ass_read_file(lib, (char *)ass_file, NULL);
    if (!track) {
        printf("[FAIL] ass_read_file(\"%s\")\n", ass_file);
        printf("       请确保字幕文件在当前目录下。\n");
        return 1;
    }
    printf("[ OK ] ass_read_file(\"%s\")\n", ass_file);
    printf("       - %d 个样式, %d 条事件\n", track->n_styles, track->n_events);

    /* 6. 渲染第 1 秒 */
    long long ts = 1000;  /* ms */
    ASS_Image *img = ass_render_frame(renderer, track, ts, NULL);
    if (!img) {
        /* 试试 0ms */
        img = ass_render_frame(renderer, track, 0, NULL);
        ts = 0;
    }

    if (!img) {
        printf("[WARN] 没有渲染出图像（可能时间点不对或字体问题）\n");
    } else {
        printf("[ OK ] ass_render_frame(t=%lldms)\n", ts);
        int count = 0;
        for (ASS_Image *p = img; p; p = p->next) count++;
        printf("       - %d 个图像块\n", count);
        printf("       - 首块: %dx%d @ (%d,%d), color=0x%08x\n",
               img->w, img->h, img->dst_x, img->dst_y, img->color);
    }

    /* 7. 合成并保存 BMP */
    int stride = W * 4;
    unsigned char *buf = (unsigned char *)calloc(stride * H, 1);
    if (buf && img) {
        blend_ass_image(buf, W, H, stride, img);
        printf("[ OK ] 图像合成\n");

        if (write_bmp(bmp_file, W, H, buf, stride)) {
            printf("[ OK ] 保存到 %s\n", bmp_file);
        } else {
            printf("[FAIL] 保存 %s 失败\n", bmp_file);
        }
    }
    free(buf);

    /* 8. 清理 */
    ass_free_track(track);
    ass_renderer_done(renderer);
    ass_library_done(lib);

    printf("\n=== 测试完成 ===\n");
    return 0;
}
