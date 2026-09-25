/*
 * Copyright (C) 2011 Grigori Goronzy <greg@chown.ath.cx>
 *
 * This file is part of libass.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef LIBASS_SHAPER_H
#define LIBASS_SHAPER_H

typedef struct ass_shaper ASS_Shaper;

#include <stdint.h>
#include <stdbool.h>
#include "ass_render.h"
#include "ass_cache.h"

/*
 * 本构建不含 FriBidi（见 README.nofribidi.md）。下面这些名字只为让其余源码
 * 继续编译而存在：它们不是 FriBidi 的头文件，也绝不与外部交换，仅在本文件
 * 的各个调用点之间自比。取值照抄 FriBidi 的公开 ABI（fribidi-bidi-types.h
 * 里的 FRIBIDI_*_VAL），这样语义与上游一致，而不是各写一套自造的数。
 *
 * 本构建里实际只需 FRIBIDI_PAR_LTR 与 FRIBIDI_PAR_ON 两个。上游若新增对
 * 其它取值的引用，应当让它编译失败——那表示上游动了双向文本这条路径，
 * 本地补丁需要重新审一遍。
 */
typedef uint32_t FriBidiCharType;
typedef int FriBidiStrIndex;
typedef int FriBidiParType;
typedef signed char FriBidiLevel;

#define FRIBIDI_PAR_LTR   0x00000110u /* FRIBIDI_TYPE_LTR_VAL */
#define FRIBIDI_PAR_ON    0x00000040u /* FRIBIDI_TYPE_ON_VAL，上游用它表示“自动检测” */

void ass_shaper_info(ASS_Library *lib);
ASS_Shaper *ass_shaper_new(Cache *metrics_cache, Cache *face_size_metrics_cache);
void ass_shaper_free(ASS_Shaper *shaper);
bool ass_create_hb_font(ASS_Font *font, int index);
void ass_shaper_set_kerning(ASS_Shaper *shaper, bool kern);
void ass_shaper_find_runs(ASS_Shaper *shaper, ASS_Renderer *render_priv,
                          GlyphInfo *glyphs, size_t len);
void ass_shaper_set_base_direction(ASS_Shaper *shaper, FriBidiParType dir);
void ass_shaper_set_language(ASS_Shaper *shaper, const char *code);
void ass_shaper_set_level(ASS_Shaper *shaper, ASS_ShapingLevel level);
void ass_shaper_set_whole_text_layout(ASS_Shaper *shaper, bool enable);
bool ass_shaper_shape(ASS_Shaper *shaper, TextInfo *text_info);
void ass_shaper_cleanup(ASS_Shaper *shaper, TextInfo *text_info);
FriBidiStrIndex *ass_shaper_reorder(ASS_Shaper *shaper, TextInfo *text_info);
FriBidiStrIndex *ass_shaper_get_reorder_map(ASS_Shaper *shaper);
FriBidiParType ass_resolve_base_direction(int font_encoding);

#endif
