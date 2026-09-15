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
#include "editor_scene.h"
#include "kingdom_scene.h"
#include "strategy_scene.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SANGO3_ANDROID
#include <android/log.h>
#define ALOG(...) __android_log_print(ANDROID_LOG_INFO, "sango3", __VA_ARGS__)
#else
#define ALOG(...) do { } while (0)
#endif

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
/* 字号档：0/1/2 = 常规三档（逻辑 14/16/20px）；3 = 大字（32px），
 * 供高分辨率画布（战略地图 1024×768）使用，否则文字相对屏幕会偏小。 */
typedef struct { S3Font *hd[4]; int ready; } FontCtx;

static int font_size_of(int font) {
    switch (font) {
        case 0: return 14;
        case 2: return 20;
        case 3: return 32;
        default: return 16;
    }
}

static void draw_text_cb(void *ud, Sango3Canvas *cv, const char *utf8,
                         int32_t x, int32_t y, int32_t w, int32_t h,
                         uint32_t rgb, int font, uint32_t style) {
    FontCtx *fc = (FontCtx *)ud;
    if (!fc || !fc->ready) return;
    int idx = (font >= 0 && font <= 3) ? font : 1;
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
/* 登錄武將（創建自定義武將）：主菜單背景 + 新君主/麾下武將 tab
 * + 武將列表框架 + 已登錄武將標題。列表內容為運行時數據。
 * 注：root 400（新增/更改/刪除面板）與 440/441 同位置，是 tab 選中後的互斥態，暫不疊。 */
static const uint32_t SC_LOGIN[]  = { S3_MENU_ROOT_ICON_ONLY | 1u,
                                      440u, 441u, 410u, 430u };
static const uint32_t SC_OPTION[] = { 300, 301, 302, 303, 304, 305, 306, 307,
                                      308, 309 };            /* 設定選項 */
static const uint32_t SC_EDITOR_BG[] = { S3_MENU_ROOT_ICON_ONLY | 1u };
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

/* ------------------------------------------------------- 开局流程（M3-lite）
 * 主菜单「開始遊戲」(cmd=1) → 選擇時期 (root 100) → 剧本按钮 cmd=11..17
 *   → 解析 Setting\City0N.ini 得到该剧本的君主与其城池/人口/金钱
 *   → 選擇君主（自绘列表，含自定义武将）→ 決定 → 写 start_state.json
 * 剧本名用简体硬编码（简中优先，不依赖 Big5 原文）。 */
static const char *SCENARIO_NAMES[8] = {
    "", "黄巾之乱", "讨伐董卓", "群雄割据", "官渡之战",
    "卧龙出渊", "三国鼎立", "天下归魏"
};

/* 载入某剧本（id 1..7）的君主：City0N.ini 的每个 [ITEM] 是一座城 */
static int load_scenario(S3Kingdom *k, int id, PakCtx *c) {
    char path[64];
    snprintf(path, sizeof path, "Setting\\City%02d.ini", id);
    uint32_t len = 0;
    uint8_t *data = pak_get(c, path, &len);
    if (!data) return -1;
    S3Ini *ini = s3_ini_parse_inc(data, len, include_cb, c);
    free(data);
    if (!ini) return -2;
    int n = 0;
    for (int i = 0; ; ++i) {
        const S3IniSection *sec = s3_ini_section_at(ini, "ITEM", i);
        if (!sec) break;
        const char *lord = s3_ini_str(sec, "Lord", NULL);
        if (!lord || !*lord) continue;
        s3_kingdom_add_lord(k, lord, 1,
                            s3_ini_int(sec, "People", 0),
                            s3_ini_int(sec, "Money", 0), 0);
        ++n;
    }
    s3_ini_free(ini);
    return n;
}

/* 自定义武将（JSONL 每行一个）也作为可选君主（★ 标记） */
static int load_custom_generals(S3Kingdom *k, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char line[512];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = strstr(line, "\"name\":\"");
        if (!p) continue;
        p += 8;
        char *e = strchr(p, '"');
        if (!e) continue;
        *e = '\0';
        if (*p) { s3_kingdom_add_lord(k, p, 0, 0, 0, 1); ++n; }
    }
    fclose(f);
    return n;
}

/* ---------------------------------------------- 战略层：城市表（MenuMap.ini）
 * 城市窗口 = WND_CLASS_CITYBUTTON，Comment = 城名、Range = 地图像素坐标（1024×768）、
 * Command = 101.. 归属判定：City0N.ini（该剧本）里同名城池的 Lord 是否等于所选君主。 */
static int load_cities(S3Strategy *st, PakCtx *c, int scenario_id,
                       const char *my_lord, const char *truth_path) {
    uint32_t len = 0;
    uint8_t *data = pak_get(c, "Setting\\MenuMap.ini", &len);
    if (!data) return -1;
    S3Ini *ini = s3_ini_parse_inc(data, len, include_cb, c);
    free(data);
    if (!ini) return -2;

    /* 先收集"我方城池名"（City0N.ini 里 Lord == 所选君主 的 Name） */
    char mine[96][32];
    int  n_mine = 0;
    {
        char pc[64];
        snprintf(pc, sizeof pc, "Setting\\City%02d.ini", scenario_id);
        uint32_t l2 = 0;
        uint8_t *d2 = pak_get(c, pc, &l2);
        if (d2) {
            S3Ini *ci = s3_ini_parse_inc(d2, l2, include_cb, c);
            free(d2);
            if (ci) {
                for (int i = 0; ; ++i) {
                    const S3IniSection *sec = s3_ini_section_at(ci, "ITEM", i);
                    if (!sec) break;
                    const char *lord = s3_ini_str(sec, "Lord", NULL);
                    const char *nm   = s3_ini_str(sec, "Name", NULL);
                    if (lord && nm && my_lord && !strcmp(lord, my_lord) && n_mine < 96)
                        snprintf(mine[n_mine++], 32, "%s", nm);
                }
                s3_ini_free(ci);
            }
        }
    }

    int n = 0;
    for (int i = 0; ; ++i) {
        const S3IniSection *sec = s3_ini_section_at(ini, "WINDOW", i);
        if (!sec) break;
        const char *cls = s3_ini_str(sec, "Class", NULL);
        if (!cls || strcmp(cls, "WND_CLASS_CITYBUTTON") != 0) continue;
        const char *nm = s3_ini_str(sec, "Comment", NULL);
        char range[64] = "";
        const char *rg = s3_ini_str(sec, "Range", NULL);
        if (!nm || !rg) continue;
        snprintf(range, sizeof range, "%s", rg);
        int x = 0, y = 0, w = 0, h = 0;
        if (sscanf(range, "%d%*[ ,]%d%*[ ,]%d%*[ ,]%d", &x, &y, &w, &h) < 2) continue;
        int is_mine = 0;
        for (int k = 0; k < n_mine; ++k)
            if (!strcmp(mine[k], nm)) { is_mine = 1; break; }
        /* ⚠ Range 是城市按钮**矩形的左上角**（如 襄平 = 823,44,24,19），
         * 城池图标中心要加半个宽高 —— 否则标记/命中都偏到左上角（用户实测发现）。 */
        s3_strategy_add_city(st, nm, x + w / 2, y + h / 2, w, h, is_mine);
        ++n;
    }
    s3_ini_free(ini);
    if (truth_path) (void)truth_path;
    printf("strategy: %d cities (%d mine)\n", n, n_mine);
    ALOG("strategy: %d cities (%d mine)", n, n_mine);
    return n;
}

static int save_start_state(const char *path, S3Kingdom *k) {    int idx = s3_kingdom_selected(k);
    if (idx < 0) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -2;
    int sc = s3_kingdom_scenario(k);
    fprintf(f, "{\"scenario\":%d,\"scenario_name\":\"%s\",",
            sc, (sc >= 1 && sc <= 7) ? SCENARIO_NAMES[sc] : "");
    fprintf(f, "\"lord\":\"%s\",\"cities\":%d,\"people\":%d,\"money\":%d,\"custom_generals\":[",
            s3_kingdom_lord_name(k, idx), s3_kingdom_lord_cities(k, idx),
            s3_kingdom_lord_people(k, idx), s3_kingdom_lord_money(k, idx));
    int first = 1;
    for (int i = 0; i < s3_kingdom_count(k); ++i) {
        if (!s3_kingdom_lord_custom(k, i)) continue;
        fprintf(f, "%s\"%s\"", first ? "" : ",", s3_kingdom_lord_name(k, i));
        first = 0;
    }
    fprintf(f, "]}\n");
    fclose(f);
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
    const char *entry     = "Setting\\Menu.ini";
    const char *enc_dir   = "engine/assets/encoding";
    const char *fonts_dir = "engine/assets/fonts";
    const char *paks[S3APP_MAX_PAK];
    int      n_paks   = 0;
    int32_t  scene_sel = 0;                  /* 0 = 主菜单 */
    const char *preset = NULL;               /* --preset 指定起始场景 */
    int32_t  out_w    = 1280, out_h = 960;   /* 2× 逻辑，整数倍最清晰 */
    Sango3Aspect aspect = SANGO3_ASPECT_EXTEND;   /* 默认 EXTEND：长屏不留黑边 */
    Sango3Filter filter = SANGO3_FILTER_NEAREST;
    uint32_t frames   = 0;
    int      selftest = 0;

#ifdef SANGO3_ANDROID
    /* Android 无命令行参数：资源固定在外部存储 /sdcard/Sango3/（由 adb push 放入）。
     * 窗口尺寸交给设备（presenter 会按实际窗口尺寸重算视口）。 */
    (void)argc; (void)argv;
    /* 资源放**应用外部私有目录**（/sdcard/Android/data/<pkg>/files/）：
     * 该目录应用自身无需运行时存储权限即可读写，adb push 也能直接写入；
     * 公共 /sdcard/ 在 Android 6+ 需运行时授权，早期会因读不到资源而立即退出。 */
    paks[0] = "/sdcard/Android/data/org.libsdl.app/files/Sango3/Sango3.PAK";
    paks[1] = "/sdcard/Android/data/org.libsdl.app/files/Sango3/Update.PAK";
    n_paks  = 2;
    enc_dir   = "/sdcard/Android/data/org.libsdl.app/files/Sango3/encoding";
    fonts_dir = "/sdcard/Android/data/org.libsdl.app/files/Sango3/fonts";
    out_w = 0; out_h = 0;                    /* 0 → 以实际窗口尺寸为准 */
#else
    if (argc < 2) {
        fprintf(stderr,
                "usage: sango3app <pak1> [pak2 ...] [--scene N] [--out WxH] "
                "[--aspect pillarbox|stretch] [--filter nearest|bilinear|sharp] "
                "[--fonts-dir D] [--encoding D] [--entry X] [--frames N] [--selftest]\n");
        return 2;
    }
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
            const char *a = argv[++i];
            if      (!strcmp(a, "stretch")) aspect = SANGO3_ASPECT_STRETCH;
            else if (!strcmp(a, "extend"))  aspect = SANGO3_ASPECT_EXTEND;
            else if (!strcmp(a, "pillarbox")) aspect = SANGO3_ASPECT_PILLARBOX;
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
#endif

#ifdef SANGO3_ANDROID
    /* 拦截系统返回键：Back 不再直接退出应用，而是作为 SDL_AC_BACK 键盘事件
     * 交给引擎（presenter 把它转成"右键返回"边沿）。必须在窗口创建前设置。 */
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif

    if (s3_text_init(enc_dir) != 0 || !s3_text_ready()) {
        fprintf(stderr, "text_init failed (encoding dir: %s)\n", enc_dir);
        ALOG("text_init failed: %s", enc_dir);
        return 1;
    }

    PakCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    for (int i = 0; i < n_paks; ++i) {
        const char *err = NULL;
        if (!pak_open(paks[i], &ctx.ar[ctx.n_ar], &err)) {
            fprintf(stderr, "pak_open failed: %s (%s)\n", paks[i], err ? err : "?");
            ALOG("pak_open failed: %s (%s)", paks[i], err ? err : "?");
            for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
            return 1;
        }
        ++ctx.n_ar;
    }

    uint32_t len = 0;
    uint8_t *data = pak_get(&ctx, entry, &len);
    if (!data) {
        fprintf(stderr, "entry not found: %s\n", entry);
        ALOG("entry not found: %s", entry);
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
    /* 战略地图专用画布：与地图素材同分辨率（1024×768），1:1 上屏零重采样。
     * 若塞进 640×480 逻辑画布，会"先降采样再放大"，画面发糊（用户实测）。 */
    Sango3Canvas *cv_map = sango3_canvas_new(1024, 768, 0, 0, 0);
    if (!cv_map) { fprintf(stderr, "map canvas alloc failed\n"); sango3_canvas_free(cv); s3_ui_free(&L); s3_ini_free(ini); for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]); return 1; }

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
        for (int k = 0; k < 4; ++k) fc.hd[k] = s3_font_open(S3_FONT_HD, font_size_of(k), S3_LANG_HANS);
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

    int32_t crx = 0, cry = 0, crw = 0, crh = 0;
    sango3_presenter_content_rect(p, &crx, &cry, &crw, &crh);
    printf("OK app started: roots=%d canvas=%dx%d out=%dx%d (ESC/close=quit, RMB=back)\n",
           app.n_roots, cw, ch, out_w, out_h);
    ALOG("app started: roots=%d canvas=%dx%d out=%dx%d content=%d,%d %dx%d",
         app.n_roots, cw, ch, out_w, out_h, crx, cry, crw, crh);

    uint32_t drawn = 0;
    /* 应用模式：0=菜单场景 1=创建武将表单 2=选择君主 3=开局占位 */
    int mode = 0;
    S3Editor *ed = NULL;
    S3Kingdom *kd = NULL;
    S3Strategy *st = NULL;
    char start_msg[256] = "";
#ifdef SANGO3_ANDROID
    const char *gen_path = "/sdcard/Android/data/org.libsdl.app/files/Sango3/custom_generals.jsonl";
    const char *start_path = "/sdcard/Android/data/org.libsdl.app/files/Sango3/start_state.json";
#else
    const char *gen_path = "tmp/custom_generals.jsonl";
    const char *start_path = "tmp/start_state.json";
#endif
    for (;;) {
        if (mode == 1) {
            /* ================= 创建武将表单 ================= */
            if (!ed) ed = s3_editor_new(draw_text_cb, &fc, read_asset_cb, &ctx);
            ms.roots = SC_EDITOR_BG;
            ms.n_roots = NARR(SC_EDITOR_BG);
            s3_menu_render(&ms, cv);          /* 背景（借主菜单背景图） */
            s3_editor_render(ed, cv);
            sango3_presenter_upload(p, cv->px);
            if (sango3_presenter_frame(p, &drawn)) break;

            char txt[32];
            while (sango3_presenter_poll_text(p, txt)) s3_editor_on_text(ed, txt);
            int32_t sym;
            while (sango3_presenter_poll_key(p, &sym)) s3_editor_on_key(ed, sym);

            S3Pointer pt;
            sango3_presenter_pointer(p, &pt);
            if (pt.rclick) {                  /* 返回键 = 取消 */
                SDL_StopTextInput();
                s3_editor_free(ed); ed = NULL;
                mode = 0;
            } else {
                /* 点击：按下边沿记录位置，抬起且未移动才触发（防拖动误触） */
                static int pressing = 0;
                static int32_t px = -1, py = -1;
                if (pt.lclick) {
                    pressing = 1;
                    px = (int32_t)pt.lx; py = (int32_t)pt.ly;
                }
                if (!pt.ldown && pressing) {
                    pressing = 0;
                    if ((int32_t)pt.lx == px && (int32_t)pt.ly == py)
                        s3_editor_on_click(ed, px, py);
                }
                if (s3_editor_typing(ed)) SDL_StartTextInput();
                else                          SDL_StopTextInput();

                int r = s3_editor_result(ed);
                if (r == 1) {
                    if (s3_editor_save(ed, gen_path) == 0) {
                        printf("general saved -> %s\n", gen_path);
                        ALOG("general saved -> %s", gen_path);
                    }
                    SDL_StopTextInput();
                    s3_editor_free(ed); ed = NULL;
                    mode = 0;
                } else if (r == 2) {
                    SDL_StopTextInput();
                    s3_editor_free(ed); ed = NULL;
                    mode = 0;
                }
            }
            SDL_Delay(16);
            continue;
        }

        if (mode == 2) {
            /* ================= 选择君主 ================= */
            if (!kd) kd = s3_kingdom_new(draw_text_cb, &fc);
            ms.roots = SC_EDITOR_BG;
            ms.n_roots = NARR(SC_EDITOR_BG);
            s3_menu_render(&ms, cv);
            s3_kingdom_render(kd, cv);
            sango3_presenter_upload(p, cv->px);
            if (sango3_presenter_frame(p, &drawn)) break;

            S3Pointer pt;
            sango3_presenter_pointer(p, &pt);
            if (pt.rclick) { mode = 0; app.hover = app.press = 0; }
            else {
                static int kpressing = 0;
                static int32_t kx = -1, ky = -1;
                if (pt.lclick) { kpressing = 1; kx = (int32_t)pt.lx; ky = (int32_t)pt.ly; }
                if (!pt.ldown && kpressing) {
                    kpressing = 0;
                    if ((int32_t)pt.lx == kx && (int32_t)pt.ly == ky)
                        s3_kingdom_on_click(kd, kx, ky);
                }
                int r = s3_kingdom_result(kd);
                if (r == 1) {
                    int sc = s3_kingdom_scenario(kd);
                    int si = s3_kingdom_selected(kd);
                    snprintf(start_msg, sizeof start_msg, "%s · 君主 %s（%d 城）",
                             (sc >= 1 && sc <= 7) ? SCENARIO_NAMES[sc] : "",
                             s3_kingdom_lord_name(kd, si),
                             s3_kingdom_lord_cities(kd, si));
                    if (save_start_state(start_path, kd) == 0) {
                        printf("start state saved: %s -> %s\n", start_msg, start_path);
                        ALOG("start saved: %s -> %s", start_msg, start_path);
                    }
                    mode = 3;
                } else if (r == 2) {
                    mode = 0; app.hover = app.press = 0;
                }
            }
            SDL_Delay(16);
            continue;
        }
        if (mode == 3) {
            /* ================= 进入战略层 ================= */
            if (!st) st = s3_strategy_new(read_asset_cb, &ctx, draw_text_cb, &fc);
            if (s3_strategy_set_map(st, "Shape\\AD\\Base\\Map.shp") != 0)
                ALOG("strategy map load FAILED");
            s3_strategy_clear_cities(st);
            if (kd) {
                int sid = s3_kingdom_scenario(kd);
                int si  = s3_kingdom_selected(kd);
                load_cities(st, &ctx, sid,
                            si >= 0 ? s3_kingdom_lord_name(kd, si) : NULL, NULL);
            }
            /* 地图是"内容本身" → COVER（铺满裁切）+ 原生分辨率 + 平滑滤镜 */
            sango3_presenter_set_logical_size(p, cv_map->w, cv_map->h);
            sango3_presenter_set_aspect(p, SANGO3_ASPECT_COVER);
            sango3_presenter_set_filter(p, SANGO3_FILTER_BILINEAR);
            mode = 4;
        }
        if (mode == 4) {
            /* ================= 战略层：地图 + 城市 ================= */
            s3_strategy_render(st, cv_map);
            sango3_presenter_upload(p, cv_map->px);
            if (sango3_presenter_frame(p, &drawn)) break;

            S3Pointer pt;
            sango3_presenter_pointer(p, &pt);
            if (pt.rclick) {
                /* 回菜单：恢复 640×480 逻辑画布 + EXTEND 条带 + 像素滤镜 */
                sango3_presenter_set_filter(p, SANGO3_FILTER_NEAREST);
                sango3_presenter_set_aspect(p, SANGO3_ASPECT_EXTEND);
                sango3_presenter_set_logical_size(p, cw, ch);
                mode = 0; app.hover = app.press = 0;
            } else {
                static int spressing = 0;
                static int32_t sx = -1, sy = -1;
                if (pt.lclick) { spressing = 1; sx = (int32_t)pt.lx; sy = (int32_t)pt.ly; }
                if (!pt.ldown && spressing) {
                    spressing = 0;
                    if ((int32_t)pt.lx == sx && (int32_t)pt.ly == sy)
                        s3_strategy_on_click(st, sx, sy);
                }
            }
            SDL_Delay(16);
            continue;
        }

        /* ================= 菜单场景 ================= */
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

        /* 按下：本帧有按下边沿 → 记录按压目标。
         * 抬起：只要当前无按压且 press 有记录就判定触发。
         * （不能依赖 prev_down 电平判定抬起 —— 快速点按的 down/up 可能落在同一帧，
         *   用电平判定会整次丢失点击。2026-09-15 实测踩坑。） */
        /* 按下边沿即记录按压目标：同帧 down+up 时 ldown 已被 UP 事件清零，
         * 不能再要求 ldown==1（否则快速点按整次丢失，2026-09-15 实测踩坑）。
         * 抬起判定：无按压 + 有记录 → 触发；按住拖出控件后抬起 hit 变化 → 不触发。 */
        if (pt.lclick || pt.rclick)
            ALOG("input: lclick=%d rclick=%d at %.0f,%.0f (inside=%d)",
                 pt.lclick, pt.rclick, pt.lx, pt.ly, pt.inside);
        if (pt.lclick) app.press = hit;
        if (!pt.ldown && app.press) {
            if (app.press == hit) {
                const S3UiWindow *w = s3_ui_window(&L, app.press);
                int32_t cmd = w ? w->command : -1;
                if (cmd == 6) { printf("command=6 (quit)\n"); break; }
                Scene next = scene_for_command(cmd);
                if (cmd == 3) {
                    /* 登錄武將 → 创建自定义武将表单 */
                    mode = 1;
                    app.hover = app.press = 0;
                    printf("editor mode (create general)\n");
                    ALOG("editor mode (create general)");
                } else if (cmd >= 11 && cmd <= 17) {
                    /* 選擇時期的剧本按钮 → 载入该剧本的君主列表 */
                    int sid = cmd - 10;                 /* 11..17 → 剧本 1..7 */
                    if (!kd) kd = s3_kingdom_new(draw_text_cb, &fc);
                    s3_kingdom_begin(kd, sid, SCENARIO_NAMES[sid]);
                    int nl = load_scenario(kd, sid, &ctx);
                    int nc = load_custom_generals(kd, gen_path);
                    printf("scenario %d (%s): %d lords (+%d custom)\n",
                           sid, SCENARIO_NAMES[sid], nl, nc);
                    ALOG("scenario %d (%s): %d lords (+%d custom)",
                         sid, SCENARIO_NAMES[sid], nl, nc);
                    if (nl > 0 || nc > 0) { mode = 2; app.hover = app.press = 0; }
                } else if (next.n > 0) {
                    if (app.sp < 16) { app.stack[app.sp].roots = app.roots; app.stack[app.sp].n = app.n_roots; ++app.sp; }
                    app.roots = next.roots;
                    app.n_roots = next.n;
                    app.hover = app.press = 0;
                    printf("scene -> %d roots (from cmd=%d)\n", app.n_roots, cmd);
                    ALOG("scene -> %d roots (cmd=%d)", app.n_roots, cmd);
                } else if (cmd >= 0) {
                    printf("click id=%u cmd=%d (no scene mapping yet)\n", app.press, cmd);
                    ALOG("click id=%u cmd=%d (no mapping)", app.press, cmd);
                }
            }
            app.press = 0;
        }

        if (pt.rclick && app.sp > 0) {
            --app.sp;
            app.roots = app.stack[app.sp].roots;
            app.n_roots = app.stack[app.sp].n;
            app.hover = app.press = 0;
            printf("scene <- %d roots (back)\n", app.n_roots);
            ALOG("scene <- back to %d roots", app.n_roots);
        }

        SDL_Delay(16);
    }

    s3_menu_scene_release(&ms);
    if (ed) s3_editor_free(ed);
    if (kd) s3_kingdom_free(kd);
    if (st) s3_strategy_free(st);
    for (int k = 0; k < 4; ++k) if (fc.hd[k]) s3_font_close(fc.hd[k]);
    s3_font_quit();
    sango3_presenter_free(p);
    sango3_canvas_free(cv);
    sango3_canvas_free(cv_map);
    s3_ui_free(&L);
    s3_ini_free(ini);
    for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
    s3_text_shutdown();
    return 0;
}
