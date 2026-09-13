/*
 * sango3view_sdl.c —— sango3view 的 SDL2 显示后端（M1-b GPU 路径 A + 字体显示层）
 *
 * 把逻辑画布交给 GPU 呈现器（presenter）：逻辑画布作为纹理上传，
 * 开一个物理分辨率窗口，由 GPU 按宽高比/滤镜缩放显示，并支持拖动缩放。
 * 可选叠加 UTF-8 文本（字体渲染层），再整体交给 GPU 缩放。
 * 注意：SDL_MAIN_HANDLED 已由构建系统定义，这里的 main 保持为真 main。
 */
#include "sango3view.h"
#include "presenter.h"
#include "font.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *aspect_name(Sango3Aspect a) {
    return (a == SANGO3_ASPECT_STRETCH) ? "stretch" : "pillarbox";
}
static const char *filter_name(Sango3Filter f) {
    switch (f) {
        case SANGO3_FILTER_BILINEAR: return "bilinear";
        case SANGO3_FILTER_SHARP:    return "sharp";
        default:                     return "nearest";
    }
}

/* 把文本 RGBA（含 alpha）以 over 混合叠到画布中央，返回叠加的非透明像素数（<0 失败）。 */
static int overlay_font_text(uint8_t *canvas, uint32_t cw, uint32_t ch,
                             const char *fonts_dir, const char *text,
                             S3FontMode mode, int size, uint32_t color, S3Lang lang) {
    if (!text || !*text) return 0;
    if (!s3_font_ready() && s3_font_init(fonts_dir) != 0) {
        printf("WARN : font init failed (no TTF or missing fonts)\n");
        return -1;
    }
    S3Font *f = s3_font_open(mode, size, lang);
    if (!f) { printf("WARN : font open failed (mode=%d)\n", mode); return -1; }

    uint8_t *rgba = NULL; int tw = 0, th = 0;
    int rc = s3_font_render_utf8(f, text, &rgba, &tw, &th, color);
    if (rc != 0 || !rgba) { printf("WARN : font render failed\n"); s3_font_close(f); return -1; }

    int dx = (int)(cw / 2) - tw / 2;
    int dy = (int)(ch / 2) - th / 2;
    if (dx < 0) dx = 0;
    if (dy < 0) dy = 0;
    int drawn = 0;
    for (int y = 0; y < th; ++y) {
        int ty = dy + y; if (ty >= (int)ch) break;
        for (int x = 0; x < tw; ++x) {
            int tx = dx + x; if (tx >= (int)cw) break;
            const uint8_t *s = rgba + ((size_t)y * tw + x) * 4;
            uint8_t a = s[3]; if (a == 0) continue;
            uint8_t *d = canvas + ((size_t)ty * cw + tx) * 4;
            if (a == 255) {
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
            } else {
                d[0] = (uint8_t)((s[0] * a + d[0] * (255 - a)) / 255);
                d[1] = (uint8_t)((s[1] * a + d[1] * (255 - a)) / 255);
                d[2] = (uint8_t)((s[2] * a + d[2] * (255 - a)) / 255);
                d[3] = 255;
            }
            drawn++;
        }
    }
    free(rgba);
    s3_font_close(f);
    printf("font_overlay = %s mode=%d size=%d glyph_pixels=%d text_w=%d text_h=%d\n",
           text, mode, size, drawn, tw, th);
    return drawn;
}

/* 内置中文自检：分别用高清/像素字体渲染「三国群英传三五」，报告非透明像素数，
 * 证明 CJK glyph 可用（哪怕命令行参数传不了中文，也能验证中文渲染）。 */
static void font_selftest(S3Lang lang) {
    const char *sample = "三国群英传三五";
    for (int m = 0; m < 2; ++m) {
        S3FontMode mode = (m == 0) ? S3_FONT_HD : S3_FONT_PIXEL;
        const char *mn = (m == 0) ? "HD" : "PIXEL";
        S3Font *f = s3_font_open(mode, (m == 0) ? 28 : 24, lang);
        if (!f) { printf("selftest %s : OPEN_FAIL\n", mn); continue; }
        uint8_t *rgba = NULL; int tw = 0, th = 0;
        int rc = s3_font_render_utf8(f, sample, &rgba, &tw, &th, 0xFFFFFF);
        if (rc != 0 || !rgba) { printf("selftest %s : RENDER_FAIL\n", mn); s3_font_close(f); continue; }
        int npix = 0;
        for (int i = 0; i < tw * th; ++i) if (rgba[i * 4 + 3] > 0) npix++;
        printf("selftest %s : glyph_pixels=%d w=%d h=%d\n", mn, npix, tw, th);
        free(rgba);
        s3_font_close(f);
    }
}

int sango3view_show_sdl(const uint8_t *rgba, uint32_t w, uint32_t h, const Sango3ViewParams *p) {
    if (!rgba || w == 0 || h == 0 || !p) return 10;

    /* 字体子系统（按需初始化；无 --fonts-dir 则不初始化，文本叠加降级跳过） */
    if (p->fonts_dir) {
        if (s3_font_init(p->fonts_dir) != 0) printf("WARN : font init failed\n");
    }
    if (p->font_selftest && s3_font_ready()) font_selftest(S3_LANG_HANS);

    /* 文本叠加：复制到可写画布，叠加后再上传（不改动原始 rgba） */
    uint8_t *upload_buf = (uint8_t *)rgba;
    uint8_t *copy = NULL;
    if (p->text && s3_font_ready()) {
        size_t n = (size_t)w * h * 4;
        copy = (uint8_t *)malloc(n);
        if (copy) {
            memcpy(copy, rgba, n);
            overlay_font_text(copy, w, h, p->fonts_dir, p->text,
                              p->font_mode, p->font_size, p->font_color, S3_LANG_HANS);
            upload_buf = copy;
        }
    }

    Sango3Presenter *pr = sango3_presenter_new((int32_t)w, (int32_t)h,
                                              p->out_w, p->out_h,
                                              p->resizable, p->aspect, p->filter,
                                              "Sango3 Native - GPU Presenter");
    if (!sango3_presenter_valid(pr)) {
        sango3_presenter_free(pr);
        free(copy);
        if (p->fonts_dir) s3_font_quit();
        printf("FAIL : presenter init\n");
        return 11;
    }

    printf("sdl_video_driver = %s\n", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(null)");
    printf("logical = %ux%u\n", w, h);
    {
        int32_t ww = 0, hh = 0;
        sango3_presenter_window_size(pr, &ww, &hh);
        printf("window_physical = %dx%d\n", ww, hh);
    }
    {
        int32_t x = 0, y = 0, cw = 0, ch = 0;
        sango3_presenter_content_rect(pr, &x, &y, &cw, &ch);
        printf("content_rect = %d,%d,%d,%d\n", x, y, cw, ch);
    }
    printf("scale = %.4f\n", sango3_presenter_scale(pr));
    printf("aspect = %s\n", aspect_name(p->aspect));
    printf("filter = %s\n", filter_name(p->filter));
    printf("font_ready = %d\n", s3_font_ready());

    sango3_presenter_upload(pr, upload_buf);

    int running = 1;
    uint32_t drawn = 0;
    while (running) {
        int quit = sango3_presenter_frame(pr, &drawn);
        if (quit) running = 0;
        if (p->frames > 0 && drawn >= p->frames) running = 0;
        SDL_Delay(16);
    }
    printf("frames_drawn = %u\n", drawn);

    sango3_presenter_free(pr);
    free(copy);
    if (p->fonts_dir) s3_font_quit();
    printf("window_closed = 1\n");
    return 0;
}
