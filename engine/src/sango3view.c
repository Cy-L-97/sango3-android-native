/*
 * sango3view.c —— 引擎渲染验证器（M1 首个可产出图像的产物）
 *
 * 两种模式：
 *   dump  —— 从 PAK 取一批 SHP 素材，解码并合成到一张 RGBA 画布，写出原始像素
 *            （不依赖 SDL，纯 C；由 tools/raw2png.py 转成 PNG 后可肉眼校验）
 *   show  —— 开 SDL2 窗口把同一张画布显示出来（人机交互用）
 *
 * 用法:
 *   sango3view dump <pak> <关键字> <out.raw> [--cols N] [--limit N]
 *   sango3view show <pak> <关键字> [--cols N] [--limit N]
 */
#include "pak.h"
#include "shp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SANGO3_HAVE_SDL
/* 实现在 sango3view_sdl.c */
int sango3view_show_sdl(const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t frames);
#endif

typedef struct {
    uint8_t *px;       /* RGBA */
    uint32_t w, h;
} Canvas;

static Canvas *canvas_new(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
    Canvas *c = (Canvas *)malloc(sizeof(Canvas));
    if (!c) return NULL;
    c->w = w; c->h = h;
    size_t n = (size_t)w * h * 4;
    c->px = (uint8_t *)malloc(n);
    if (!c->px) { free(c); return NULL; }
    for (size_t i = 0; i < n; i += 4) {
        c->px[i] = r; c->px[i + 1] = g; c->px[i + 2] = b; c->px[i + 3] = 255;
    }
    return c;
}

static void canvas_free(Canvas *c) {
    if (!c) return;
    free(c->px);
    free(c);
}

/* 把 src 贴到画布 (dx,dy)，做标准 over 混合（忽略浮点，用整数近似） */
static void canvas_blit(Canvas *dst, const ShpImage *src, uint32_t dx, uint32_t dy) {
    for (uint32_t y = 0; y < src->height; y++) {
        uint32_t ty = dy + y;
        if (ty >= dst->h) break;
        for (uint32_t x = 0; x < src->width; x++) {
            uint32_t tx = dx + x;
            if (tx >= dst->w) break;
            const uint8_t *s = src->rgba + ((size_t)y * src->width + x) * 4;
            uint8_t a = s[3];
            if (a == 0) continue;
            uint8_t *d = dst->px + ((size_t)ty * dst->w + tx) * 4;
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

/* 收集匹配条目索引 */
static uint32_t collect(const PakArchive *ar, const char *kw, uint32_t limit,
                        uint32_t **out_idx) {
    uint32_t *idx = (uint32_t *)malloc(sizeof(uint32_t) * (ar->entries ? ar->entries : 1));
    uint32_t n = 0;
    for (uint32_t i = 0; i < ar->entries && n < limit; i++) {
        if (!pak_contains_ci(ar->tab[i].name, kw)) continue;
        idx[n++] = i;
    }
    *out_idx = idx;
    return n;
}

typedef struct {
    uint32_t idx;
    ShpImage img;
} Item;

static Canvas *build_canvas(const PakArchive *ar, const char *kw, uint32_t cols,
                            uint32_t limit, uint32_t *out_n, uint32_t *out_ok) {
    uint32_t *idx = NULL;
    uint32_t n = collect(ar, kw, limit, &idx);
    if (n == 0) { free(idx); return NULL; }

    Item *items = (Item *)calloc(n, sizeof(Item));
    uint32_t ok = 0;
    uint32_t cw = 1, ch = 1;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t len = 0;
        uint8_t *raw = pak_read(ar, idx[i], &len);
        if (!raw) continue;
        const char *err = NULL;
        ShpImage im;
        if (shp_decode(raw, len, &im, &err)) {
            items[ok].idx = idx[i];
            items[ok].img = im;
            if (im.width  > cw) cw = im.width;
            if (im.height > ch) ch = im.height;
            ok++;
        }
        free(raw);
    }
    free(idx);

    if (ok == 0) { free(items); return NULL; }
    if (cols == 0) cols = 8;
    uint32_t rows = (ok + cols - 1) / cols;

    Canvas *c = canvas_new(cols * cw, rows * ch, 24, 24, 28);
    if (!c) { for (uint32_t i = 0; i < ok; i++) shp_free(&items[i].img); free(items); return NULL; }

    for (uint32_t i = 0; i < ok; i++) {
        uint32_t gx = (i % cols) * cw;
        uint32_t gy = (i / cols) * ch;
        canvas_blit(c, &items[i].img, gx, gy);
    }
    for (uint32_t i = 0; i < ok; i++) shp_free(&items[i].img);
    free(items);

    if (out_n) *out_n = n;
    if (out_ok) *out_ok = ok;
    return c;
}

static int write_raw(const char *path, const Canvas *c) {
    FILE *f = fopen(path, "wb");
    if (!f) { printf("FAIL : cannot open output\n"); return 0; }
    size_t n = (size_t)c->w * c->h * 4;
    size_t got = fwrite(c->px, 1, n, f);
    fclose(f);
    printf("wrote_bytes = %zu\n", got);
    return got == n;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "usage:\n"
            "  sango3view dump <pak> <keyword> <out.raw> [--cols N] [--limit N]\n"
            "  sango3view show <pak> <keyword> [--cols N] [--limit N]\n");
        return 1;
    }
    const char *mode = argv[1];
    int dump = (strcmp(mode, "dump") == 0);
    int show = (strcmp(mode, "show") == 0);
    if (!dump && !show) { fprintf(stderr, "unknown mode: %s\n", mode); return 1; }
    if (argc < 4) { fprintf(stderr, "not enough arguments\n"); return 1; }

    const char *pak_path = argv[2];
    const char *kw       = argv[3];
    const char *out_path = dump ? argv[4] : NULL;
    uint32_t cols = 8, limit = 64, frames = 0;

    for (int i = dump ? 5 : 4; i < argc; i++) {
        if (strcmp(argv[i], "--cols") == 0 && i + 1 < argc)  cols  = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) limit = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = (uint32_t)atoi(argv[++i]);
    }

    const char *err = NULL;
    PakArchive ar;
    if (!pak_open(pak_path, &ar, &err)) {
        printf("FAIL : pak_open: %s\n", err ? err : "?");
        return 2;
    }
    printf("pak_entries = %u\n", ar.entries);
    printf("keyword = %s\n", kw);
    printf("cols = %u  limit = %u\n", cols, limit);

    uint32_t n = 0, ok = 0;
    Canvas *c = build_canvas(&ar, kw, cols, limit, &n, &ok);
    if (!c) {
        printf("FAIL : no decodable sprite matched\n");
        pak_close(&ar);
        return 3;
    }
    printf("matched = %u\n", n);
    printf("decoded = %u\n", ok);
    printf("canvas_w = %u\n", c->w);
    printf("canvas_h = %u\n", c->h);

    int rc = 0;
    if (dump) {
        if (write_raw(out_path, c)) {
            printf("hash_fnv1a = 0x%08X\n", shp_fnv1a(c->px, (size_t)c->w * c->h * 4));
        } else {
            rc = 4;
        }
    } else {
#ifdef SANGO3_HAVE_SDL
        rc = sango3view_show_sdl(c->px, c->w, c->h, frames);
#else
        printf("FAIL : this build has no SDL support (use dump mode)\n");
        rc = 5;
#endif
    }

    canvas_free(c);
    pak_close(&ar);
    return rc;
}
