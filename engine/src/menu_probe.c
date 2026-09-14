/*
 * menu_probe.c —— 主菜单场景渲染验证器（M2-5）
 *
 * 用法：
 *   sango3menu <out_dir> <pak1> [pak2 ...] [--entry "Setting\Menu.ini"]
 *              [--encoding <dir>] [--root N] [--name <base>] [--fonts-dir <dir>]
 * 例：
 *   sango3menu build/pc/out "E:/.../Sango3.PAK" "E:/.../Update.PAK"
 *
 * 链路：PAK 取 Menu.ini → 展开 #include → s3_ui_load → 控件树运行时
 *       （menu_scene）→ 640×480 逻辑画布（按 Range 逐控件贴素材 + 文本）→ 写 raw。
 *
 * 产出：
 *   <out_dir>/<base>.raw          RGBA8888（tools/render_menu.py 转 PNG）
 *   <out_dir>/<base>_summary.txt  尺寸 + sanity 计数 + 失败素材清单
 *
 * 多 PAK：**后给出的覆盖先给出的**（Update.PAK 覆盖 Sango3.PAK）。
 * 文本层：当构建带 SDL_ttf（SANGO3_HAVE_TTF）且 --fonts-dir 可用时启用，否则自动跳过。
 * 控制台只输出 ASCII；中文一律写文件。
 */
#include "ui.h"
#include "text.h"
#include "pak.h"
#include "render.h"
#include "menu_scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SANGO3_HAVE_TTF
#include "font.h"
#endif

#define S3UI_MAX_PAK 8

typedef struct {
    PakArchive ar[S3UI_MAX_PAK];
    int        n_ar;
    char       base_dir[512];
} PakCtx;

static int ieq(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        ++a; ++b;
    }
    return *a == *b;
}

/* 后给出的 PAK 优先；先精确（大小写不敏感）后子串。 */
static uint8_t *pak_get(PakCtx *c, const char *path, uint32_t *out_len) {
    for (int pass = 0; pass < 2; ++pass) {
        for (int a = c->n_ar - 1; a >= 0; --a) {
            PakArchive *ar = &c->ar[a];
            if (pass == 0) {
                for (uint32_t i = 0; i < ar->entries; ++i)
                    if (ieq(ar->tab[i].name, path)) return pak_read(ar, i, out_len);
            } else {
                int32_t idx = pak_find(ar, path);
                if (idx >= 0) return pak_read(ar, (uint32_t)idx, out_len);
            }
        }
    }
    return NULL;
}

static uint8_t *read_asset_cb(void *ud, const char *path, uint32_t *out_len) {
    return pak_get((PakCtx *)ud, path, out_len);
}

static int include_cb(const char *name, unsigned char **out_data, size_t *out_n, void *ud) {
    PakCtx *c = (PakCtx *)ud;
    char path[600];
    if (strchr(name, '\\') || strchr(name, '/'))
        snprintf(path, sizeof path, "%s", name);
    else
        snprintf(path, sizeof path, "%s%s", c->base_dir, name);
    uint32_t len = 0;
    uint8_t *d = pak_get(c, path, &len);
    if (!d || !len) return -1;
    *out_data = (unsigned char *)d;
    *out_n = (size_t)len;
    return 0;
}

/* ---------------------------------------------------------------- 文本层 */
#ifdef SANGO3_HAVE_TTF
typedef struct {
    S3Font *hd[3];   /* Font=0/1/2 → 14/16/20 px */
    int     ready;
} FontCtx;

static int font_size_of(int font) {
    switch (font) { case 0: return 14; case 2: return 20; default: return 16; }
}

static void draw_text_cb(void *ud, Sango3Canvas *cv, const char *utf8,
                         int32_t x, int32_t y, int32_t w, int32_t h,
                         uint32_t rgb, int font, uint32_t style) {
    FontCtx *fc = (FontCtx *)ud;
    if (!fc || !fc->ready) return;
    int idx = (font >= 0 && font <= 2) ? font : 1;
    S3Font *f = fc->hd[idx];
    if (!f) f = fc->hd[1];
    if (!f) return;

    uint8_t *px = NULL; int tw = 0, th = 0;
    if (s3_font_render_utf8(f, utf8, &px, &tw, &th, rgb) != 0 || !px) return;

    int32_t dx = x, dy = y;
    if (style & S3_WS_HCENTER) dx = x + (w - tw) / 2;
    if (style & S3_WS_VCENTER) dy = y + (h - th) / 2;
    sango3_canvas_blit(cv, px, tw, th, dx, dy, 1);
    free(px);
}
#endif

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "usage: sango3menu <out_dir> <pak1> [pak2 ...] "
                "[--entry \"Setting\\Menu.ini\"] [--encoding <dir>] [--root N] "
                "[--name base] [--fonts-dir <dir>]\n");
        return 2;
    }
    const char *out_dir   = argv[1];
    const char *entry     = "Setting\\Menu.ini";
    const char *enc_dir   = "engine/assets/encoding";
    const char *fonts_dir = "engine/assets/fonts";
    const char *base      = "menu";
    int32_t     root_id   = 1;
    const char *paks[S3UI_MAX_PAK];
    int n_paks = 0;

    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--entry") && i + 1 < argc) { entry = argv[++i]; continue; }
        if (!strcmp(argv[i], "--encoding") && i + 1 < argc) { enc_dir = argv[++i]; continue; }
        if (!strcmp(argv[i], "--fonts-dir") && i + 1 < argc) { fonts_dir = argv[++i]; continue; }
        if (!strcmp(argv[i], "--name") && i + 1 < argc) { base = argv[++i]; continue; }
        if (!strcmp(argv[i], "--root") && i + 1 < argc) { root_id = atoi(argv[++i]); continue; }
        if (n_paks < S3UI_MAX_PAK) paks[n_paks++] = argv[i];
    }
    if (n_paks == 0) { fprintf(stderr, "no pak given\n"); return 2; }

    /* Big5 → UTF-8 必须在解析 INI 之前就绪 */
    if (s3_text_init(enc_dir) != 0 || !s3_text_ready()) {
        fprintf(stderr, "text_init failed (encoding dir: %s)\n", enc_dir);
        return 1;
    }

    PakCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    for (int i = 0; i < n_paks; ++i) {
        const char *err = NULL;
        if (!pak_open(paks[i], &ctx.ar[ctx.n_ar], &err)) {
            fprintf(stderr, "pak_open failed: %s (%s)\n", paks[i], err ? err : "?");
            for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
            return 1;
        }
        ++ctx.n_ar;
    }

    uint32_t len = 0;
    uint8_t *data = pak_get(&ctx, entry, &len);
    if (!data) {
        fprintf(stderr, "entry not found: %s\n", entry);
        for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
        return 1;
    }
    {
        const char *slash = strrchr(entry, '\\');
        const char *slash2 = strrchr(entry, '/');
        if (slash2 && (!slash || slash2 > slash)) slash = slash2;
        if (slash) {
            size_t n = (size_t)(slash - entry) + 1;
            if (n >= sizeof ctx.base_dir) n = sizeof ctx.base_dir - 1;
            memcpy(ctx.base_dir, entry, n);
            ctx.base_dir[n] = '\0';
        }
    }

    S3Ini *ini = s3_ini_parse_inc(data, len, include_cb, &ctx);
    free(data);
    if (!ini) {
        fprintf(stderr, "ini parse failed\n");
        for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
        return 1;
    }

    S3UiLayout L;
    if (s3_ui_load(&L, ini) != 0) {
        fprintf(stderr, "ui_load failed\n");
        s3_ini_free(ini);
        for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
        return 1;
    }

    const S3UiWindow *root = s3_ui_window(&L, (uint32_t)root_id);
    int32_t cw = root && root->range.w > 0 ? root->range.w : 640;
    int32_t ch = root && root->range.h > 0 ? root->range.h : 480;

    Sango3Canvas *cv = sango3_canvas_new(cw, ch, 0, 0, 0);
    if (!cv) {
        fprintf(stderr, "canvas alloc failed\n");
        s3_ui_free(&L); s3_ini_free(ini);
        for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
        return 1;
    }

    S3MenuScene ms;
    memset(&ms, 0, sizeof ms);
    ms.layout     = &L;
    ms.read_asset = read_asset_cb;
    ms.asset_ud   = &ctx;
    ms.root_id    = root_id;
    ms.state      = 0;

#ifdef SANGO3_HAVE_TTF
    FontCtx fc;
    memset(&fc, 0, sizeof fc);
    if (fonts_dir && s3_font_init(fonts_dir) == 0 && s3_font_ready()) {
        for (int k = 0; k < 3; ++k) fc.hd[k] = s3_font_open(S3_FONT_HD, font_size_of(k), S3_LANG_HANS);
        fc.ready = (fc.hd[0] || fc.hd[1] || fc.hd[2]);
    }
    if (fc.ready) { ms.draw_text = draw_text_cb; ms.text_ud = &fc; }
#endif

    int32_t drawn = s3_menu_render(&ms, cv);

    /* 写 raw */
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.raw", out_dir, base);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s (out_dir exists?)\n", path);
        sango3_canvas_free(cv); s3_ui_free(&L); s3_ini_free(ini);
        for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
        return 1;
    }
    size_t nbytes = (size_t)cv->w * cv->h * 4;
    fwrite(cv->px, 1, nbytes, f);
    fclose(f);

    /* 写 summary */
    snprintf(path, sizeof path, "%s/%s_summary.txt", out_dir, base);
    f = fopen(path, "wb");
    if (f) {
        fprintf(f, "# Sango3 menu scene\n");
        for (int i = 0; i < n_paks; ++i) fprintf(f, "pak\t%s\n", paks[i]);
        fprintf(f, "entry\t%s\n", entry);
        fprintf(f, "root_id\t%d\n", root_id);
        fprintf(f, "canvas\t%dx%d\n", cv->w, cv->h);
        fprintf(f, "state\t%s\n", s3_menu_state_name(ms.state));
        fprintf(f, "n_drawn\t%d\n", ms.n_drawn);
        fprintf(f, "n_icon_missing\t%d\n", ms.n_icon_missing);
        fprintf(f, "n_asset_missing\t%d\n", ms.n_asset_missing);
        fprintf(f, "n_decode_fail\t%d\n", ms.n_decode_fail);
        fprintf(f, "n_text\t%d\n", ms.n_text);
        fprintf(f, "n_containers\t%d\n", ms.n_containers);
        int lim = ms.n_asset_missing < 8 ? ms.n_asset_missing : 8;
        for (int i = 0; i < lim; ++i)
            fprintf(f, "missing\t%s\n", ms.log_asset_missing[i]);
        lim = ms.n_decode_fail < 8 ? ms.n_decode_fail : 8;
        for (int i = 0; i < lim; ++i)
            fprintf(f, "decode_fail\t%s\n", ms.log_decode_fail[i]);
        fclose(f);
    }

    printf("OK root=%d canvas=%dx%d drawn=%d icon_missing=%d asset_missing=%d "
           "decode_fail=%d text=%d containers=%d -> %s/%s.raw\n",
           root_id, cv->w, cv->h, drawn,
           ms.n_icon_missing, ms.n_asset_missing, ms.n_decode_fail,
           ms.n_text, ms.n_containers, out_dir, base);

    sango3_canvas_free(cv);
    s3_ui_free(&L);
    s3_ini_free(ini);
    for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
#ifdef SANGO3_HAVE_TTF
    for (int k = 0; k < 3; ++k) if (fc.hd[k]) s3_font_close(fc.hd[k]);
    s3_font_quit();
#endif
    s3_text_shutdown();
    return 0;
}
