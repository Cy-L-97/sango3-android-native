/*
 * sango3app.c —— 游戏主程序雏形（M2-6 交互态 + M2-7 场景切换）
 *
 * 与 sango3menu（离线出图）的区别：本程序开**真实窗口**并处理鼠标交互。
 *
 * 场景模型（关键）：
 *   原版同一时刻可**叠加显示多个 root 窗口**（Menu.ini 里 99 个 root 各自独立，
 *   由引擎按需显示/隐藏）。因此一个"场景" = 一组 root，按数组顺序叠加渲染
 *   （后面的在上）。例：存档界面 = 招牌 +「儲存進度01~10」+「讀取自動存檔」+「確定／取消」。
 *   ⚠ 列表类控件（BUTTONREPORT / LIST）的内容是**运行时数据**，此处只渲染
 *     Menu.ini 声明的框架与静态图元。
 *
 * 交互：悬停 → focus 素材；按下 → down 素材；抬起且仍在该控件 → 触发 command；
 *       右键返回上一屏（场景栈）；ESC / 关窗退出。
 *
 * 用法：
 *   sango3app <pak1> [pak2 ...] [--scene N] [--out WxH] [--aspect a] [--filter f]
 *             [--fonts-dir D] [--encoding D] [--entry X] [--frames N] [--selftest]
 *   --selftest 不开窗口，对若干坐标跑 hit_test 并打印命中控件（离线校验交互逻辑）。
 */
#include "ui.h"
#include "text.h"
#include "pak.h"
#include "render.h"
#include "menu_scene.h"
#include "presenter.h"
#include "font.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define S3APP_MAX_PAK 8

typedef struct {
    PakArchive ar[S3APP_MAX_PAK];
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
    if (strchr(name, '\\') || strchr(name, '/')) snprintf(path, sizeof path, "%s", name);
    else snprintf(path, sizeof path, "%s%s", c->base_dir, name);
    uint32_t len = 0;
    uint8_t *d = pak_get(c, path, &len);
    if (!d || !len) return -1;
    *out_data = (unsigned char *)d;
    *out_n = (size_t)len;
    return 0;
}

/* ---------------------------------------------------------------- 文本层 */
typedef struct { S3Font *hd[3]; int ready; } FontCtx;

static int font_size_of(int font) {
    switch (font) { case 0: return 14; case 2: return 20; default: return 16; }
}

static void draw_text_cb(void *ud, Sango3Canvas *cv, const char *utf8,
                         int32_t x, int32_t y, int32_t w, int32_t h,
                         uint32_t rgb, int font, uint32_t style) {
    FontCtx *fc = (FontCtx *)ud;
    if (!fc || !fc->ready) return;
    int idx = (font >= 0 && font <= 2) ? font : 1;
    S3Font *f = fc->hd[idx] ? fc->hd[idx] : fc->hd[1];
    if (!f) return;
    uint8_t *px = NULL; int tw = 0, th = 0;
    if (s3_font_render_utf8(f, utf8, &px, &tw, &th, rgb) != 0 || !px) return;
    int32_t dx = x, dy = y;
    if (style & S3_WS_HCENTER) dx = x + (w - tw) / 2;
    if (style & S3_WS_VCENTER) dy = y + (h - th) / 2;
    sango3_canvas_blit(cv, px, tw, th, dx, dy, 1);
    free(px);
}

/* ---------------------------------------------------------------- 场景表 */
/* 一个场景 = 一组 root 窗口（数组顺序 = 叠加顺序，后面的在上）。
 * 列表内容为运行时数据，此处仅渲染 Menu.ini 声明的框架与静态图元。 */
static const uint32_t SC_MAIN[]   = { 1 };
static const uint32_t SC_AGE[]    = { 100 };                 /* 選擇時期 */
static const uint32_t SC_SAVE[]   = { 220, 201, 202, 203, 204, 205, 206, 207,
                                      208, 209, 210, 240, 230 };  /* 存檔：招牌+10 槽+自動+確定/取消 */
static const uint32_t SC_LOGIN[]  = { 400, 410 };            /* 登錄武將：面板 + 列表框架 */
static const uint32_t SC_OPTION[] = { 300, 301, 302, 303, 304, 305, 306, 307,
                                      308, 309 };            /* 設定選項 */
/* 战略 / 战术层：UI 面板组合。
 * 地图地形层待接 —— 需要 BlkData 逆向 + Shape\SF\Map\*.shp 等距瓦片拼接。 */
static const uint32_t SC_STRATEGY[] = { 20000, 7000 };       /* Statusbar + MISSION CONTROL */
static const uint32_t SC_BATTLE[]   = { 34000, 30000 };      /* 戰術狀態欄 + 作戰指令 */

#define NARR(a) ((int)(sizeof(a) / sizeof *(a)))

typedef struct { const uint32_t *roots; int n; } Scene;

static Scene scene_for_command(int32_t cmd) {
    Scene s = { NULL, 0 };
    switch (cmd) {
        case 1: s.roots = SC_AGE;    s.n = (int)(sizeof SC_AGE    / sizeof *SC_AGE);    break;
        case 2: s.roots = SC_SAVE;   s.n = (int)(sizeof SC_SAVE   / sizeof *SC_SAVE);   break;
        case 3: s.roots = SC_LOGIN;  s.n = (int)(sizeof SC_LOGIN  / sizeof *SC_LOGIN);  break;
        case 5: s.roots = SC_OPTION; s.n = (int)(sizeof SC_OPTION / sizeof *SC_OPTION); break;
        default: break;   /* 其他 command 暂不切换 */
    }
    return s;
}

/* ---------------------------------------------------------------- App */
typedef struct {
    const S3UiLayout *L;
    const uint32_t   *roots;
    int               n_roots;
    uint32_t          hover;
    uint32_t          press;
    Scene             stack[16];
    int               sp;
} App;

static int state_of_cb(void *ud, uint32_t id) {
    App *a = (App *)ud;
    if (a->press && id == a->press) return 2;   /* down */
    if (a->hover && id == a->hover) return 1;   /* focus */
    return 0;
}

/* ---------------------------------------------------------------- selftest */
static int run_selftest(const S3UiLayout *L, const uint32_t *roots, int n) {
    struct { int32_t x, y; const char *note; } pts[] = {
        { 320, 208, "btn1 (newgame)" },
        { 320, 256, "btn2 (load)" },
        { 320, 304, "btn3 (login)" },
        { 320, 352, "btn4 (option)" },
        { 320, 400, "btn5 (quit)" },
        { 600, 460, "version area" },
        {  60,  60, "title area" },
    };
    printf("== hit_test selftest (roots=%d) ==\n", n);
    for (size_t i = 0; i < sizeof pts / sizeof pts[0]; ++i) {
        uint32_t hit = s3_menu_hit_test_multi(L, roots, n, pts[i].x, pts[i].y);
        const S3UiWindow *w = hit ? s3_ui_window(L, hit) : NULL;
        printf("  (%3d,%3d) -> hit=%-5u cmd=%-4d %s\n",
               pts[i].x, pts[i].y, hit, w ? w->command : -1, pts[i].note);
    }
    return 0;
}

/* ---------------------------------------------------------------- main */
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage: sango3app <pak1> [pak2 ...] [--scene N] [--out WxH] "
                "[--aspect pillarbox|stretch] [--filter nearest|bilinear|sharp] "
                "[--fonts-dir D] [--encoding D] [--entry X] [--frames N] [--selftest]\n");
        return 2;
    }
    const char *entry     = "Setting\\Menu.ini";
    const char *enc_dir   = "engine/assets/encoding";
    const char *fonts_dir = "engine/assets/fonts";
    const char *paks[S3APP_MAX_PAK];
    int      n_paks   = 0;
    int32_t  scene_sel = 0;                  /* 0 = 主菜单 */
    const char *preset = NULL;               /* --preset 指定起始场景 */
    int32_t  out_w    = 1280, out_h = 960;   /* 2× 逻辑，整数倍最清晰 */
    Sango3Aspect aspect = SANGO3_ASPECT_PILLARBOX;
    Sango3Filter filter = SANGO3_FILTER_NEAREST;
    uint32_t frames   = 0;
    int      selftest = 0;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--entry") && i + 1 < argc) { entry = argv[++i]; continue; }
        if (!strcmp(argv[i], "--encoding") && i + 1 < argc) { enc_dir = argv[++i]; continue; }
        if (!strcmp(argv[i], "--fonts-dir") && i + 1 < argc) { fonts_dir = argv[++i]; continue; }
        if (!strcmp(argv[i], "--scene") && i + 1 < argc) { scene_sel = atoi(argv[++i]); continue; }
        if (!strcmp(argv[i], "--preset") && i + 1 < argc) { preset = argv[++i]; continue; }
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) { frames = (uint32_t)atoi(argv[++i]); continue; }
        if (!strcmp(argv[i], "--selftest")) { selftest = 1; continue; }
        if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            if (sscanf(argv[++i], "%d%*[xX]%d", &out_w, &out_h) != 2) {
                fprintf(stderr, "bad --out (expect WxH)\n"); return 1;
            }
            continue;
        }
        if (!strcmp(argv[i], "--aspect") && i + 1 < argc) {
            if (!strcmp(argv[++i], "stretch")) aspect = SANGO3_ASPECT_STRETCH;
            continue;
        }
        if (!strcmp(argv[i], "--filter") && i + 1 < argc) {
            const char *f = argv[++i];
            if (!strcmp(f, "bilinear")) filter = SANGO3_FILTER_BILINEAR;
            else if (!strcmp(f, "sharp")) filter = SANGO3_FILTER_SHARP;
            continue;
        }
        if (n_paks < S3APP_MAX_PAK) paks[n_paks++] = argv[i];
    }
    if (n_paks == 0) { fprintf(stderr, "no pak given\n"); return 2; }

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
    if (!ini) { fprintf(stderr, "ini parse failed\n"); for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]); return 1; }

    S3UiLayout L;
    if (s3_ui_load(&L, ini) != 0) {
        fprintf(stderr, "ui_load failed\n");
        s3_ini_free(ini);
        for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
        return 1;
    }

    Scene start = { SC_MAIN, NARR(SC_MAIN) };
    static uint32_t one[1];
    if (preset) {
        if      (!strcmp(preset, "main"))     start = (Scene){ SC_MAIN,     NARR(SC_MAIN) };
        else if (!strcmp(preset, "age"))      start = (Scene){ SC_AGE,      NARR(SC_AGE) };
        else if (!strcmp(preset, "save"))     start = (Scene){ SC_SAVE,     NARR(SC_SAVE) };
        else if (!strcmp(preset, "login"))    start = (Scene){ SC_LOGIN,    NARR(SC_LOGIN) };
        else if (!strcmp(preset, "option"))   start = (Scene){ SC_OPTION,   NARR(SC_OPTION) };
        else if (!strcmp(preset, "strategy")) start = (Scene){ SC_STRATEGY, NARR(SC_STRATEGY) };
        else if (!strcmp(preset, "battle"))   start = (Scene){ SC_BATTLE,   NARR(SC_BATTLE) };
    } else if (scene_sel > 0) {
        one[0] = (uint32_t)scene_sel;
        start.roots = one;
        start.n = 1;
    }

    if (selftest) {
        int rc = run_selftest(&L, start.roots, start.n);
        s3_ui_free(&L); s3_ini_free(ini);
        for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
        s3_text_shutdown();
        return rc;
    }

    const int32_t cw = 640, ch = 480;   /* 场景均为整屏组合，画布固定逻辑尺寸 */

    Sango3Canvas *cv = sango3_canvas_new(cw, ch, 0, 0, 0);
    if (!cv) { fprintf(stderr, "canvas alloc failed\n"); s3_ui_free(&L); s3_ini_free(ini); for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]); return 1; }

    Sango3Presenter *p = sango3_presenter_new(cw, ch, out_w, out_h, 1, aspect, filter, "Sango3 Native");
    if (!sango3_presenter_valid(p)) {
        fprintf(stderr, "presenter init failed (SDL: %s)\n", SDL_GetError());
        sango3_canvas_free(cv); s3_ui_free(&L); s3_ini_free(ini);
        for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
        return 1;
    }

    FontCtx fc;
    memset(&fc, 0, sizeof fc);
    if (s3_font_init(fonts_dir) == 0 && s3_font_ready())
        for (int k = 0; k < 3; ++k) fc.hd[k] = s3_font_open(S3_FONT_HD, font_size_of(k), S3_LANG_HANS);
    fc.ready = (fc.hd[0] || fc.hd[1] || fc.hd[2]);

    App app;
    memset(&app, 0, sizeof app);
    app.L = &L;
    app.roots = start.roots;
    app.n_roots = start.n;

    S3MenuScene ms;
    memset(&ms, 0, sizeof ms);
    ms.layout     = &L;
    ms.read_asset = read_asset_cb;
    ms.asset_ud   = &ctx;
    ms.state_of   = state_of_cb;
    ms.state_ud   = &app;
    if (fc.ready) { ms.draw_text = draw_text_cb; ms.text_ud = &fc; }

    printf("OK app started: roots=%d canvas=%dx%d out=%dx%d (ESC/close=quit, RMB=back)\n",
           app.n_roots, cw, ch, out_w, out_h);

    int prev_down = 0;
    uint32_t drawn = 0;
    for (;;) {
        ms.roots = app.roots;
        ms.n_roots = app.n_roots;
        s3_menu_render(&ms, cv);

        sango3_presenter_upload(p, cv->px);
        if (sango3_presenter_frame(p, &drawn)) break;
        if (frames && drawn >= frames) break;

        S3Pointer pt;
        sango3_presenter_pointer(p, &pt);

        uint32_t hit = pt.inside
                     ? s3_menu_hit_test_multi(&L, app.roots, app.n_roots,
                                              (int32_t)pt.lx, (int32_t)pt.ly)
                     : 0;
        app.hover = hit;

        if (pt.ldown && !prev_down) app.press = hit;
        if (!pt.ldown && prev_down) {
            if (app.press && app.press == hit) {
                const S3UiWindow *w = s3_ui_window(&L, app.press);
                int32_t cmd = w ? w->command : -1;
                if (cmd == 6) { printf("command=6 (quit)\n"); break; }
                Scene next = scene_for_command(cmd);
                if (next.n > 0) {
                    if (app.sp < 16) { app.stack[app.sp].roots = app.roots; app.stack[app.sp].n = app.n_roots; ++app.sp; }
                    app.roots = next.roots;
                    app.n_roots = next.n;
                    app.hover = app.press = 0;
                    printf("scene -> %d roots (from cmd=%d)\n", app.n_roots, cmd);
                } else if (cmd >= 0) {
                    printf("click id=%u cmd=%d (no scene mapping yet)\n", app.press, cmd);
                }
            }
            app.press = 0;
        }
        prev_down = pt.ldown;

        if (pt.rclick && app.sp > 0) {
            --app.sp;
            app.roots = app.stack[app.sp].roots;
            app.n_roots = app.stack[app.sp].n;
            app.hover = app.press = 0;
            printf("scene <- %d roots (back)\n", app.n_roots);
        }

        SDL_Delay(16);
    }

    s3_menu_scene_release(&ms);
    for (int k = 0; k < 3; ++k) if (fc.hd[k]) s3_font_close(fc.hd[k]);
    s3_font_quit();
    sango3_presenter_free(p);
    sango3_canvas_free(cv);
    s3_ui_free(&L);
    s3_ini_free(ini);
    for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
    s3_text_shutdown();
    return 0;
}
