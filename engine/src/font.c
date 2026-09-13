/*
 * font.c —— TTF 字体渲染层（M1-b 字体显示）
 *
 * 表/字体文件由 tools/setup_sdl_ttf.py 与 engine/assets/fonts 提供，运行时零平台依赖。
 */
#include "font.h"

#ifdef SANGO3_HAVE_TTF
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_fonts_dir[2048];
static int  g_init = 0;

int s3_font_ready(void) { return g_init; }

int s3_font_init(const char *fonts_dir) {
    if (g_init) return 0;
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return -1;
    }
    if (fonts_dir) {
        size_t n = strlen(fonts_dir);
        if (n >= sizeof g_fonts_dir) n = sizeof g_fonts_dir - 1;
        memcpy(g_fonts_dir, fonts_dir, n);
        g_fonts_dir[n] = '\0';
    } else {
        g_fonts_dir[0] = '\0';
    }
    g_init = 1;
    return 0;
}

void s3_font_quit(void) {
    if (g_init) { TTF_Quit(); g_init = 0; }
}

static int file_readable(const char *p) {
    /* 用 SDL 的 UTF-8 安全文件 API（Windows 下内部转宽字符），
     * 否则 ANSI fopen 认不了含中文的 UTF-8 路径。 */
    SDL_RWops *rw = SDL_RWFromFile(p, "rb");
    if (!rw) return 0;
    SDL_RWclose(rw);
    return 1;
}

/* 回退链：项目字体（按模式/语言选文件）→ 系统字体（Windows）→ NULL。
 * buf 为静态缓冲，调用方需立即使用（不跨调用持有）。 */
static const char *pick_font_path(S3FontMode mode, S3Lang lang) {
    static char buf[2048];
    buf[0] = '\0';
    if (g_fonts_dir[0]) {
        if (mode == S3_FONT_PIXEL) {
            const char *fn = (lang == S3_LANG_HANT)
                ? "FusionPixel12-zh_hant.ttf" : "FusionPixel12-zh_hans.ttf";
            snprintf(buf, sizeof buf, "%s/%s", g_fonts_dir, fn);
            if (file_readable(buf)) return buf;
        } else {
            snprintf(buf, sizeof buf, "%s/LXGWWenKai-Regular.ttf", g_fonts_dir);
            if (file_readable(buf)) return buf;
        }
    }
    /* 系统字体兜底（Windows 常见 CJK；Android 不走到这，依赖项目字体） */
    static const char *sys[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyhbd.ttc",
        "C:/Windows/Fonts/simsun.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/arial.ttf",
        NULL
    };
    for (int i = 0; sys[i]; ++i) {
        if (file_readable(sys[i])) return sys[i];
    }
    return NULL;
}

struct S3Font { TTF_Font *font; };

S3Font *s3_font_open(S3FontMode mode, int size_px, S3Lang lang) {
    if (!g_init) return NULL;
    if (size_px <= 0) size_px = 16;
    const char *path = pick_font_path(mode, lang);
    if (!path) return NULL;
    /* .ttc 是字体集合，需指定 index（0 = 第一张，通常是我们要的）；普通 .ttf 用 index 0 也兼容 */
    TTF_Font *ttf = TTF_OpenFontIndex(path, size_px, 0);
    if (!ttf) ttf = TTF_OpenFont(path, size_px);
    if (!ttf) return NULL;
    /* 像素模式关 hinting 保持像素锐利；高清模式正常 hinting 得平滑轮廓 */
    TTF_SetFontHinting(ttf, mode == S3_FONT_PIXEL ? TTF_HINTING_NONE : TTF_HINTING_NORMAL);
    TTF_SetFontKerning(ttf, 1);
    S3Font *f = (S3Font *)malloc(sizeof(S3Font));
    if (!f) { TTF_CloseFont(ttf); return NULL; }
    f->font = ttf;
    return f;
}

void s3_font_close(S3Font *f) {
    if (!f) return;
    if (f->font) TTF_CloseFont(f->font);
    free(f);
}

int s3_font_render_utf8(const S3Font *f, const char *utf8,
                        uint8_t **out_rgba, int *w, int *h, uint32_t color) {
    *out_rgba = NULL; *w = 0; *h = 0;
    if (!f || !f->font || !utf8 || !utf8[0]) return -1;

    SDL_Color fg = {
        (Uint8)((color >> 16) & 0xFF),
        (Uint8)((color >> 8)  & 0xFF),
        (Uint8)( color        & 0xFF),
        255
    };
    /* Blended：前景色 + 边缘 alpha 抗锯齿，背景透明 */
    SDL_Surface *surf = TTF_RenderUTF8_Blended(f->font, utf8, fg);
    if (!surf) return -1;
    /* 统一转到 ARGB8888（内存序 B,G,R,A），便于逐像素转 RGBA8888 */
    SDL_Surface *cv = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(surf);
    if (!cv) return -1;

    int sw = cv->w, sh = cv->h;
    int pitch = cv->pitch;
    uint8_t *out = (uint8_t *)malloc((size_t)sw * sh * 4);
    if (!out) { SDL_FreeSurface(cv); return -1; }
    for (int y = 0; y < sh; ++y) {
        const Uint32 *row = (const Uint32 *)((const Uint8 *)cv->pixels + (size_t)y * pitch);
        for (int x = 0; x < sw; ++x) {
            Uint32 v = row[x];
            size_t o = ((size_t)y * sw + x) * 4;
            out[o + 0] = (Uint8)((v >> 16) & 0xFF);  /* R */
            out[o + 1] = (Uint8)((v >> 8)  & 0xFF);  /* G */
            out[o + 2] = (Uint8)( v        & 0xFF);  /* B */
            out[o + 3] = (Uint8)( v >> 24);          /* A */
        }
    }
    *out_rgba = out; *w = sw; *h = sh;
    SDL_FreeSurface(cv);
    return 0;
}

#else /* !SANGO3_HAVE_TTF —— 无 TTF 子系统时提供安全降级（调用方不会渲染文本） */

int s3_font_ready(void) { return 0; }
int s3_font_init(const char *fonts_dir) { (void)fonts_dir; return -1; }
void s3_font_quit(void) {}
S3Font *s3_font_open(S3FontMode mode, int size_px, S3Lang lang) {
    (void)mode; (void)size_px; (void)lang; return NULL;
}
void s3_font_close(S3Font *f) { (void)f; }
int s3_font_render_utf8(const S3Font *f, const char *utf8,
                        uint8_t **out_rgba, int *w, int *h, uint32_t color) {
    (void)f; (void)utf8; (void)color;
    if (out_rgba) *out_rgba = NULL;
    if (w) *w = 0;
    if (h) *h = 0;
    return -1;
}

#endif
