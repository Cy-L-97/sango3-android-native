/*
 * render.c —— 分辨率无关渲染核心实现
 * 见 render.h 的设计说明。纯 C11，不依赖 SDL（可在 dump 模式下离线出图验证）。
 */
#include "render.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================ 画布 */
Sango3Canvas *sango3_canvas_new(int32_t w, int32_t h, uint8_t r, uint8_t g, uint8_t b) {
    if (w <= 0 || h <= 0) return NULL;
    Sango3Canvas *c = (Sango3Canvas *)malloc(sizeof(Sango3Canvas));
    if (!c) return NULL;
    c->w = w;
    c->h = h;
    size_t n = (size_t)w * (size_t)h;
    c->px = (uint8_t *)malloc(n * 4);
    if (!c->px) { free(c); return NULL; }
    for (size_t i = 0; i < n; i++) {
        c->px[i * 4 + 0] = r;
        c->px[i * 4 + 1] = g;
        c->px[i * 4 + 2] = b;
        c->px[i * 4 + 3] = 255;
    }
    return c;
}

void sango3_canvas_free(Sango3Canvas *c) {
    if (!c) return;
    free(c->px);
    free(c);
}

void sango3_canvas_fill(Sango3Canvas *c, int32_t x, int32_t y,
                        int32_t w, int32_t h, uint8_t r, uint8_t g, uint8_t b) {
    if (!c) return;
    for (int32_t yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= c->h) continue;
        for (int32_t xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= c->w) continue;
            uint8_t *d = c->px + ((size_t)yy * c->w + xx) * 4;
            d[0] = r; d[1] = g; d[2] = b; d[3] = 255;
        }
    }
}

void sango3_canvas_frame(Sango3Canvas *c, int32_t x, int32_t y, int32_t w, int32_t h,
                         int32_t t, uint8_t r, uint8_t g, uint8_t b) {
    if (t < 1) t = 1;
    sango3_canvas_fill(c, x, y, w, t, r, g, b);                 /* 上 */
    sango3_canvas_fill(c, x, y + h - t, w, t, r, g, b);         /* 下 */
    sango3_canvas_fill(c, x, y, t, h, r, g, b);                 /* 左 */
    sango3_canvas_fill(c, x + w - t, y, t, h, r, g, b);         /* 右 */
}

void sango3_canvas_blit(Sango3Canvas *c, const uint8_t *rgba, int32_t sw, int32_t sh,
                        int32_t dx, int32_t dy, int32_t zoom) {
    if (!c || !rgba || sw <= 0 || sh <= 0) return;
    if (zoom < 1) zoom = 1;
    for (int32_t y = 0; y < sh * zoom; y++) {
        int32_t ty = dy + y;
        if (ty < 0 || ty >= c->h) continue;
        for (int32_t x = 0; x < sw * zoom; x++) {
            int32_t tx = dx + x;
            if (tx < 0 || tx >= c->w) continue;
            const uint8_t *s = rgba + (((size_t)(y / zoom) * sw) + (x / zoom)) * 4;
            uint8_t a = s[3];
            if (a == 0) continue;
            uint8_t *d = c->px + ((size_t)ty * c->w + tx) * 4;
            if (a == 255) {
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
            } else {
                d[0] = (uint8_t)((s[0] * a + d[0] * (255 - a)) / 255);
                d[1] = (uint8_t)((s[1] * a + d[1] * (255 - a)) / 255);
                d[2] = (uint8_t)((s[2] * a + d[2] * (255 - a)) / 255);
                d[3] = 255;
            }
        }
    }
}

/* ================================================================ 视口 */
void sango3_viewport_init(Sango3Viewport *vp,
                          int32_t logical_w, int32_t logical_h,
                          int32_t out_w, int32_t out_h,
                          Sango3Aspect aspect, Sango3Filter filter) {
    vp->logical_w = logical_w;
    vp->logical_h = logical_h;
    vp->out_w = out_w;
    vp->out_h = out_h;
    vp->aspect = aspect;
    vp->filter = filter;

    float sx = (logical_w > 0) ? (float)out_w / (float)logical_w : 1.0f;
    float sy = (logical_h > 0) ? (float)out_h / (float)logical_h : 1.0f;

    if (aspect == SANGO3_ASPECT_STRETCH) {
        vp->dst_x = 0; vp->dst_y = 0;
        vp->dst_w = out_w; vp->dst_h = out_h;
        vp->scale = sx;                 /* 非等比，scale 仅作参考 */
    } else {
        float s = (sx < sy) ? sx : sy;  /* 等比：取较小系数，保证完整可见 */
        vp->scale = s;
        vp->dst_w = (int32_t)(logical_w * s + 0.5f);
        vp->dst_h = (int32_t)(logical_h * s + 0.5f);
        vp->dst_x = (out_w - vp->dst_w) / 2;
        vp->dst_y = (out_h - vp->dst_h) / 2;
    }
}

/* --------------------------------------------------------------- 采样 */
static void fetch_clamped(const uint8_t *src, int32_t sw, int32_t sh,
                          int32_t x, int32_t y, uint8_t out[4]) {
    if (x < 0) x = 0;
    if (x >= sw) x = sw - 1;
    if (y < 0) y = 0;
    if (y >= sh) y = sh - 1;
    const uint8_t *p = src + ((size_t)y * sw + x) * 4;
    out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3];
}

static void sample_nearest(const uint8_t *src, int32_t sw, int32_t sh,
                           float fx, float fy, uint8_t out[4]) {
    fetch_clamped(src, sw, sh, (int32_t)floorf(fx + 0.5f), (int32_t)floorf(fy + 0.5f), out);
}

static void sample_bilinear(const uint8_t *src, int32_t sw, int32_t sh,
                            float fx, float fy, uint8_t out[4]) {
    float x = fx - 0.5f, y = fy - 0.5f;
    float xf = floorf(x), yf = floorf(y);
    int32_t x0 = (int32_t)xf, y0 = (int32_t)yf;
    float ax = x - xf, ay = y - yf;
    uint8_t c00[4], c10[4], c01[4], c11[4];
    fetch_clamped(src, sw, sh, x0,     y0,     c00);
    fetch_clamped(src, sw, sh, x0 + 1, y0,     c10);
    fetch_clamped(src, sw, sh, x0,     y0 + 1, c01);
    fetch_clamped(src, sw, sh, x0 + 1, y0 + 1, c11);
    for (int i = 0; i < 4; i++) {
        float v = c00[i] * (1 - ax) * (1 - ay) + c10[i] * ax * (1 - ay)
                + c01[i] * (1 - ax) * ay       + c11[i] * ax * ay;
        out[i] = (uint8_t)(v + 0.5f);
    }
}

/* 锐化双线性：先把逻辑图像"预设放大 k 倍"（k = floor(scale)），
 * 在这层做双线性，两个邻居回落到原始像素中心取值 —— 本质是
 * "整数倍用最近邻保住像素块，不足的零头用双线性抹平台阶"。
 * 当 scale 恰为整数（如 640×480 → 2560×1440 的 ×3）时，结果与最近邻完全一致。 */
static void sample_sharp(const uint8_t *src, int32_t sw, int32_t sh,
                         float fx, float fy, int32_t k, uint8_t out[4]) {
    float px = (fx + 0.5f) * (float)k - 0.5f;
    float py = (fy + 0.5f) * (float)k - 0.5f;
    float pxf = floorf(px), pyf = floorf(py);
    int32_t ix = (int32_t)pxf, iy = (int32_t)pyf;
    float ax = px - pxf, ay = py - pyf;

    float xa = ((float)ix + 0.5f) / (float)k - 0.5f;
    float xb = ((float)(ix + 1) + 0.5f) / (float)k - 0.5f;
    float ya = ((float)iy + 0.5f) / (float)k - 0.5f;
    float yb = ((float)(iy + 1) + 0.5f) / (float)k - 0.5f;

    uint8_t c00[4], c10[4], c01[4], c11[4];
    sample_nearest(src, sw, sh, xa, ya, c00);
    sample_nearest(src, sw, sh, xb, ya, c10);
    sample_nearest(src, sw, sh, xa, yb, c01);
    sample_nearest(src, sw, sh, xb, yb, c11);
    for (int i = 0; i < 4; i++) {
        float v = c00[i] * (1 - ax) * (1 - ay) + c10[i] * ax * (1 - ay)
                + c01[i] * (1 - ax) * ay       + c11[i] * ax * ay;
        out[i] = (uint8_t)(v + 0.5f);
    }
}

void sango3_present(const uint8_t *logical, const Sango3Viewport *vp,
                    uint8_t *out, uint8_t bg_r, uint8_t bg_g, uint8_t bg_b) {
    if (!logical || !vp || !out) return;

    size_t total = (size_t)vp->out_w * (size_t)vp->out_h;
    for (size_t i = 0; i < total; i++) {   /* 内容区外：黑边 */
        out[i * 4 + 0] = bg_r; out[i * 4 + 1] = bg_g; out[i * 4 + 2] = bg_b; out[i * 4 + 3] = 255;
    }

    int32_t k = 1;
    if (vp->filter == SANGO3_FILTER_SHARP) {
        k = (int32_t)floorf(vp->scale + 1e-4f);
        if (k < 1) k = 1;
    }

    float sxs = (vp->logical_w > 0) ? (float)vp->dst_w / (float)vp->logical_w : 1.0f;
    float sys = (vp->logical_h > 0) ? (float)vp->dst_h / (float)vp->logical_h : 1.0f;

    for (int32_t y = 0; y < vp->dst_h; y++) {
        int32_t oy = vp->dst_y + y;
        if (oy < 0 || oy >= vp->out_h) continue;
        float fy = ((float)y + 0.5f) / sys - 0.5f;
        uint8_t *row = out + ((size_t)oy * vp->out_w + vp->dst_x) * 4;
        for (int32_t x = 0; x < vp->dst_w; x++) {
            int32_t ox = vp->dst_x + x;
            if (ox < 0 || ox >= vp->out_w) continue;
            float fx = ((float)x + 0.5f) / sxs - 0.5f;
            uint8_t c[4];
            switch (vp->filter) {
                case SANGO3_FILTER_BILINEAR:
                    sample_bilinear(logical, vp->logical_w, vp->logical_h, fx, fy, c);
                    break;
                case SANGO3_FILTER_SHARP:
                    sample_sharp(logical, vp->logical_w, vp->logical_h, fx, fy, k, c);
                    break;
                default:
                    sample_nearest(logical, vp->logical_w, vp->logical_h, fx, fy, c);
                    break;
            }
            uint8_t *d = row + (size_t)x * 4;
            d[0] = c[0]; d[1] = c[1]; d[2] = c[2]; d[3] = 255;
        }
    }
}

/* ================================================ 坐标变换（UI 命中测试） */
void sango3_logical_to_physical(const Sango3Viewport *vp,
                                float lx, float ly, float *px, float *py) {
    float sxs = (vp->logical_w > 0) ? (float)vp->dst_w / (float)vp->logical_w : 1.0f;
    float sys = (vp->logical_h > 0) ? (float)vp->dst_h / (float)vp->logical_h : 1.0f;
    if (px) *px = (float)vp->dst_x + lx * sxs;
    if (py) *py = (float)vp->dst_y + ly * sys;
}

void sango3_physical_to_logical(const Sango3Viewport *vp,
                                float px, float py, float *lx, float *ly) {
    float sxs = (vp->dst_w > 0) ? (float)vp->logical_w / (float)vp->dst_w : 1.0f;
    float sys = (vp->dst_h > 0) ? (float)vp->logical_h / (float)vp->dst_h : 1.0f;
    if (lx) *lx = (px - (float)vp->dst_x) * sxs;
    if (ly) *ly = (py - (float)vp->dst_y) * sys;
}

/* ============================================ 物理分辨率直绘（清晰 UI） */
void sango3_fill_rect(uint8_t *out, int32_t out_w, int32_t out_h,
                      int32_t x, int32_t y, int32_t w, int32_t h,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (int32_t yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= out_h) continue;
        for (int32_t xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= out_w) continue;
            uint8_t *d = out + ((size_t)yy * out_w + xx) * 4;
            if (a == 255) {
                d[0] = r; d[1] = g; d[2] = b; d[3] = 255;
            } else {
                d[0] = (uint8_t)((r * a + d[0] * (255 - a)) / 255);
                d[1] = (uint8_t)((g * a + d[1] * (255 - a)) / 255);
                d[2] = (uint8_t)((b * a + d[2] * (255 - a)) / 255);
                d[3] = 255;
            }
        }
    }
}

/* ================================================= 素材分级（高清包选档） */
int32_t sango3_pick_asset_tier(float scale, int32_t max_tier) {
    if (max_tier < 1) max_tier = 1;
    int32_t t = (int32_t)floorf(scale + 1e-4f);
    if (t < 1) t = 1;
    if (t > max_tier) t = max_tier;
    return t;
}
