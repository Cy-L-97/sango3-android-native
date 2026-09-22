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
#include "admin_menu.h"
#include "roster.h"
#include "gen_picker.h"
#include "lord_picker.h"

#include <SDL.h>
#include <stdarg.h>
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

/* ------------------------------------------------------------ 城 / 關口 分类
 * 权威源：游戏根目录 `LoadCity.txt`（70 条目 = **51 城（索引 0~50）+ 19 關隘（索引 51~69）**）。
 * ⚠ 不能用"城名以「關」结尾"来判定 —— 19 座關隘里有 9 座不以「關」结尾
 *   （界橋 / 定陶 / 官渡 / 長阪坡 / 赤壁 / 合肥 / 街亭 / 陳倉 / 五丈原）。
 *   实测反例（2026-09-22）：張角 12 城实为 **6 城 + 6 關**，却显示成「城數 10 · 關口數 2」。
 * 说明：本表同时是 S 区「关卡通路」（甲 §22）的关隘全集，实现通路规则时直接复用。 */
static const char *const S3_FORT_NAMES[] = {
    "雁門關", "樂陵關", "界橋",   "壺關",   "定陶",   "都陽關", "官渡",
    "筑陽關", "長阪坡", "建平關", "赤壁",   "合肥",   "虎牢關", "街亭",
    "陳倉",   "陽平關", "五丈原", "白水關", "葭萌關",
};

static int city_is_fort(const char *name) {
    if (!name || !*name) return 0;
    for (size_t i = 0; i < sizeof S3_FORT_NAMES / sizeof S3_FORT_NAMES[0]; ++i)
        if (!strcmp(name, S3_FORT_NAMES[i])) return 1;
    return 0;
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
/* 剧本名用简体硬编码（简中优先，不依赖 Big5 原文）。
 * ⚠ 权威源 = `Setting\Text.ini` 的 String 400~406（**不是** Menu.ini 的 Comment）：
 *   400 黃巾之亂 / 401 討伐董卓 / 402 群雄割據 / 403 官渡之戰 / **404 赤壁之戰** /
 *   405 三國鼎立 / 406 天下歸魏。
 *   Menu.ini 里 105 的 Comment 写作"臥龍出淵"（开发者旧注释，全库再无"臥龍"二字），
 *   2026-09-17 由用户指正 + Text.ini 复核确认 → 正确名是 **赤壁之戰**。 */
static const char *SCENARIO_NAMES[8] = {
    "", "黄巾之乱", "讨伐董卓", "群雄割据", "官渡之战",
    "赤壁之战", "三国鼎立", "天下归魏"
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

    /* ---- 君主本人属性：General01.ini 的 [GENERAL] 段（按 Name 匹配）----
     * I2 的君主列表要显示 武/智/忠/士 与肖像，全部来自这里：
     *   Strength=武力 · Intelligence=智力 · Justice=忠 · Morale=士 · Portrait=肖像号 */
    {
        uint32_t l2 = 0;
        uint8_t *d2 = pak_get(c, "Setting\\General01.ini", &l2);
        if (d2) {
            S3Ini *gi = s3_ini_parse_inc(d2, l2, include_cb, c);
            free(d2);
            if (gi) {
                for (int i = 0; ; ++i) {
                    const S3IniSection *sec = s3_ini_section_at(gi, "GENERAL", i);
                    if (!sec) break;
                    const char *nm = s3_ini_str(sec, "Name", NULL);
                    if (!nm || !*nm) continue;
                    s3_kingdom_set_lord_stats(k, nm,
                        s3_ini_int(sec, "Strength", 0),
                        s3_ini_int(sec, "Intelligence", 0),
                        s3_ini_int(sec, "Justice", 0),
                        s3_ini_int(sec, "Morale", 0),
                        s3_ini_int(sec, "Personality", 0),   /* 相性（= 原版"声望"） */
                        s3_ini_int(sec, "Portrait", 0));
                }
                s3_ini_free(gi);
            }
        }
    }
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
 * Command = 101.. 归属判定：City0N.ini（该剧本）里同名城池的 Lord 是否等于所选君主。
 *
 * 城池详情（面板九行）另有三个来源，见 docs/城池信息面板与行政菜单.md 第二节：
 *   Setting\City.ini        城名 → Size（城规模）
 *   Setting\General02.ini   武将归属：每武将 City1..City7 = 该剧本所在城池（"廬江,野"）
 *   Setting\Nation.ini      势力友好度：Friendship = "君主,值,君主,值..."
 */

/* 城名 → 计数（简单线性表，70 城规模足够） */
typedef struct { char name[32]; int n; } NameCount;
/* 城名 → 字符串（存太守等） */
typedef struct { char name[32]; char val[32]; } NameStr;
/* 城名 → 最佳执行者（誰智力/武力最高） */
typedef struct { char city[32]; char who[32]; int val; } CityBest;

static int nc_add(NameCount *a, int cap, const char *name, int n) {
    if (!name || !*name) return 0;
    for (int i = 0; i < cap; ++i) {
        if (a[i].name[0] && !strcmp(a[i].name, name)) { a[i].n += n; return 1; }
        if (!a[i].name[0]) { snprintf(a[i].name, sizeof a[i].name, "%s", name); a[i].n += n; return 1; }
    }
    return 0;
}
static int nc_get(const NameCount *a, int cap, const char *name) {
    if (!name) return 0;
    for (int i = 0; i < cap; ++i) if (a[i].name[0] && !strcmp(a[i].name, name)) return a[i].n;
    return 0;
}
/* 查询并区分"未收录"与"收录且值为 0"（Nation.ini 的友好度真的会是 0） */
static int nc_find(const NameCount *a, int cap, const char *name, int *out) {
    if (!name) return 0;
    for (int i = 0; i < cap; ++i)
        if (a[i].name[0] && !strcmp(a[i].name, name)) { if (out) *out = a[i].n; return 1; }
    return 0;
}
static void ns_set(NameStr *a, int cap, const char *name, const char *val) {
    if (!name || !*name) return;
    for (int i = 0; i < cap; ++i) {
        if (a[i].name[0] && !strcmp(a[i].name, name)) {
            snprintf(a[i].val, sizeof a[i].val, "%.31s", val ? val : "");
            return;
        }
        if (!a[i].name[0]) {
            snprintf(a[i].name, sizeof a[i].name, "%s", name);
            snprintf(a[i].val,  sizeof a[i].val,  "%.31s", val ? val : "");
            return;
        }
    }
}
static const char *ns_get(const NameStr *a, int cap, const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < cap; ++i) if (a[i].name[0] && !strcmp(a[i].name, name)) return a[i].val;
    return NULL;
}
/* 城池最佳执行者：val 更高者替换（原版口径：內政看智力、軍事看武力 —— 用户口述） */
static void cb_take(CityBest *a, int cap, const char *city, const char *who, int val) {
    if (!city || !*city || !who || !*who) return;
    for (int i = 0; i < cap; ++i) {
        if (a[i].city[0] && !strcmp(a[i].city, city)) {
            if (val > a[i].val) {
                a[i].val = val;
                snprintf(a[i].who, sizeof a[i].who, "%.31s", who);
            }
            return;
        }
        if (!a[i].city[0]) {
            snprintf(a[i].city, sizeof a[i].city, "%s", city);
            snprintf(a[i].who,  sizeof a[i].who,  "%.31s", who);
            a[i].val = val;
            return;
        }
    }
}
static const CityBest *cb_get(const CityBest *a, int cap, const char *city) {
    if (!city) return NULL;
    for (int i = 0; i < cap; ++i) if (a[i].city[0] && !strcmp(a[i].city, city)) return &a[i];
    return NULL;
}

static int load_cities(S3Strategy *st, PakCtx *c, int scenario_id,
                       const char *my_lord, S3Roster *roster) {
    uint32_t len = 0;
    uint8_t *data = pak_get(c, "Setting\\MenuMap.ini", &len);
    if (!data) return -1;
    S3Ini *ini = s3_ini_parse_inc(data, len, include_cb, c);
    free(data);
    if (!ini) return -2;

    /* ---- 表①：City0N.ini —— 每城的 Lord/People/Money/Development/ReserveForce ---- */
    static NameCount people_n[96], money_n[96], dev_n[96], reserve_n[96];
    static NameStr   lord_n[96];
    memset(people_n,  0, sizeof people_n);
    memset(money_n,   0, sizeof money_n);
    memset(dev_n,     0, sizeof dev_n);
    memset(reserve_n, 0, sizeof reserve_n);
    memset(lord_n,    0, sizeof lord_n);
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
                    const char *nm   = s3_ini_str(sec, "Name", NULL);
                    const char *lord = s3_ini_str(sec, "Lord", NULL);
                    if (!nm || !lord) continue;
                    ns_set(lord_n, 96, nm, lord);
                    nc_add(people_n,  96, nm, s3_ini_int(sec, "People", 0));
                    nc_add(money_n,   96, nm, s3_ini_int(sec, "Money", 0));
                    nc_add(dev_n,     96, nm, s3_ini_int(sec, "Development", 0));
                    nc_add(reserve_n, 96, nm, s3_ini_int(sec, "ReserveForce", 0));
                }
                s3_ini_free(ci);
            }
        }
    }

    /* ---- 表②：City.ini —— 城规模 Size ---- */
    static NameCount size_n[96];
    memset(size_n, 0, sizeof size_n);
    {
        uint32_t l3 = 0;
        uint8_t *d3 = pak_get(c, "Setting\\City.ini", &l3);
        if (d3) {
            S3Ini *mi = s3_ini_parse_inc(d3, l3, include_cb, c);
            free(d3);
            if (mi) {
                for (int i = 0; ; ++i) {
                    const S3IniSection *sec = s3_ini_section_at(mi, "ITEM", i);
                    if (!sec) break;
                    const char *nm = s3_ini_str(sec, "Name", NULL);
                    if (!nm) continue;
                    nc_add(size_n, 96, nm, s3_ini_int(sec, "Size", 0));
                }
                s3_ini_free(mi);
            }
        }
    }

    /* ---- 表③：General02.ini —— 该剧本每城武将数（City<N>，值形如 "廬江,野"）----
     * 同时算出各城的「最佳执行者」：先解 General01.ini 拿武力/智力，
     * 再按归属逐城比较 —— 內政效果量看智力、徵兵量看武力（2026-09-16 用户口述）。 */
    static NameCount gen_n[96];
    static CityBest   bint[96], bstr[96];
    static char gname[421][32];
    static int  gstr[421], gintel[421];
    static int  gn = 0;
    memset(gen_n, 0, sizeof gen_n);
    memset(bint,  0, sizeof bint);
    memset(bstr,  0, sizeof bstr);
    memset(gname, 0, sizeof gname);
    gn = 0;
    {
        uint32_t l3b = 0;
        uint8_t *d3b = pak_get(c, "Setting\\General01.ini", &l3b);
        if (d3b) {
            S3Ini *gi = s3_ini_parse_inc(d3b, l3b, include_cb, c);
            free(d3b);
            if (gi) {
                for (int i = 0; i < gi->n_sections && gn < 421; ++i) {
                    const S3IniSection *sec = &gi->sections[i];
                    if (!sec->name || strcmp(sec->name, "GENERAL") != 0) continue;
                    const char *nm = s3_ini_str(sec, "Name", NULL);
                    if (!nm || !*nm) continue;
                    snprintf(gname[gn], sizeof gname[gn], "%.31s", nm);
                    gstr[gn]   = s3_ini_int(sec, "Strength", 0);
                    gintel[gn] = s3_ini_int(sec, "Intelligence", 0);
                    ++gn;
                }
                s3_ini_free(gi);
            }
        }
        printf("roster: %d generals\n", gn);
    }
    {
        uint32_t l4 = 0;
        uint8_t *d4 = pak_get(c, "Setting\\General02.ini", &l4);
        if (d4) {
            S3Ini *gi = s3_ini_parse_inc(d4, l4, include_cb, c);
            free(d4);
            if (gi) {
                char key[16];
                snprintf(key, sizeof key, "City%d", scenario_id);
                for (int i = 0; ; ++i) {
                    const S3IniSection *sec = s3_ini_section_at(gi, "ITEM", i);
                    if (!sec) break;
                    const char *v = s3_ini_str(sec, key, NULL);
                    const char *gnm = s3_ini_str(sec, "Name", NULL);
                    if (!v || !*v) continue;
                    char buf[32];
                    int k = 0;
                    while (v[k] && v[k] != ',' && k < (int)sizeof buf - 1) { buf[k] = v[k]; ++k; }
                    buf[k] = '\0';
                    nc_add(gen_n, 96, buf, 1);
                    if (gnm && *gnm) {
                        int gi_str = 0, gi_intel = 0;
                        for (int g = 0; g < gn; ++g) {
                            if (!strcmp(gname[g], gnm)) {
                                gi_str = gstr[g]; gi_intel = gintel[g];
                                cb_take(bint, 96, buf, gnm, gintel[g]);
                                cb_take(bstr, 96, buf, gnm, gstr[g]);
                                break;
                            }
                        }
                        /* 名册：`,野` = 在野（不能作为执行者，只能被招募/搜索） */
                        int wild = (strstr(v, ",野") != NULL) ? 1 : 0;
                        if (roster) s3_roster_add(roster, gnm, buf, gi_str, gi_intel, wild);
                    }
                }
                s3_ini_free(gi);
            }
        }
    }

    /* ---- 表④：Nation.ini —— ① 各势力旗号（I1 插旗）② 我方君主 → 各势力友好度 ---- */
    static NameCount friend_n[96], flag_n[96];
    memset(friend_n, 0, sizeof friend_n);
    memset(flag_n,   0, sizeof flag_n);
    {
        uint32_t l5 = 0;
        uint8_t *d5 = pak_get(c, "Setting\\Nation.ini", &l5);
        if (d5) {
            S3Ini *ni = s3_ini_parse_inc(d5, l5, include_cb, c);
            free(d5);
            if (ni) {
                /* 段落名是 NATION{n}{xx}（不是纯 "NATION"）—— **首位数字 = 剧本号**，
                 * 如 NATION100~117 = 剧本 1 的 18 个势力。故先按剧本过滤，避免串本。 */
                char pf[16];
                snprintf(pf, sizeof pf, "NATION%d", scenario_id);
                const int pflen = (int)strlen(pf);
                for (int i = 0; i < ni->n_sections; ++i) {
                    const S3IniSection *sec = &ni->sections[i];
                    if (!sec->name || strncmp(sec->name, pf, pflen) != 0) continue;
                    const char *nl = s3_ini_str(sec, "Lord", NULL);
                    if (!nl || !*nl) continue;
                    /* 旗号：Lord → Flag（1~35），插旗绘制时按号取色板 */
                    int fl = s3_ini_int(sec, "Flag", 0);
                    if (fl > 0) nc_add(flag_n, 96, nl, fl);
                    if (!my_lord || strcmp(nl, my_lord)) continue;
                    const char *fs = s3_ini_str(sec, "Friendship", NULL);
                    if (!fs) continue;
                    /* "君主,值,君主,值..." —— 逐对扫描 */
                    const char *p = fs;
                    while (*p) {
                        char nm[32]; int k = 0;
                        while (*p && *p != ',' && k < (int)sizeof nm - 1) nm[k++] = *p++;
                        nm[k] = '\0';
                        if (*p == ',') ++p;
                        int val = 0, any = 0;
                        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); ++p; any = 1; }
                        while (*p && *p != ',') ++p;
                        if (*p == ',') ++p;
                        if (k > 0 && any) nc_add(friend_n, 96, nm, val);
                    }
                }
                s3_ini_free(ni);
            }
        }
    }

    /* ---- 组装：遍历 MenuMap.ini 的城市按钮，逐城取详情 ---- */
    int n = 0, n_mine = 0;
    for (int i = 0; ; ++i) {
        const S3IniSection *sec = s3_ini_section_at(ini, "WINDOW", i);
        if (!sec) break;
        const char *cls = s3_ini_str(sec, "Class", NULL);
        if (!cls || strcmp(cls, "WND_CLASS_CITYBUTTON") != 0) continue;
        const char *nm = s3_ini_str(sec, "Comment", NULL);
        const char *rg = s3_ini_str(sec, "Range", NULL);
        if (!nm || !rg) continue;
        int x = 0, y = 0, w = 0, h = 0;
        if (sscanf(rg, "%d%*[ ,]%d%*[ ,]%d%*[ ,]%d", &x, &y, &w, &h) < 2) continue;

        const char *cl = ns_get(lord_n, 96, nm);     /* 太守 = City0N.ini 的 Lord */

        int is_mine = (my_lord && cl && !strcmp(cl, my_lord)) ? 1 : 0;
        if (is_mine) ++n_mine;

        /* ⚠ Range 是城市按钮**矩形的左上角**（如 襄平 = 823,44,24,19），
         * 城池图标中心要加半个宽高 —— 否则标记/命中都偏到左上角（用户实测发现）。 */
        s3_strategy_add_city(st, nm, x + w / 2, y + h / 2, w, h, is_mine);
        /* I1 插旗：城市太守 → 所属势力的旗号（查不到 → 0，不插旗） */
        if (cl) s3_strategy_set_city_flag(st, n, nc_get(flag_n, 96, cl));

        S3CityDetail det;
        memset(&det, 0, sizeof det);
        if (cl) snprintf(det.lord, sizeof det.lord, "%.31s", cl);
        det.people       = nc_get(people_n, 96, nm);
        det.money        = nc_get(money_n, 96, nm);
        det.dev          = nc_get(dev_n, 96, nm);
        det.reserve      = nc_get(reserve_n, 96, nm);
        det.n_generals   = nc_get(gen_n, 96, nm);
        det.size         = nc_get(size_n, 96, nm);
        /* 友好度：我方 = 100（Nation.ini 不列自己）；他方查表，未列出的缺省 50
         * （每势力只列 17 个对手，规模小的会漏列）。注意表里**允许值为 0**
         * （张角与所有势力友好度就是 0），所以必须用带 found 标志的查询。 */
        if (is_mine) {
            det.friendliness = 100;
        } else {
            int fr = 0;
            det.friendliness = nc_find(friend_n, 96, det.lord, &fr) ? fr : 50;
        }
        {   /* 最佳执行者（ General01 能力 × General02 归属，各取该城最高） */
            const CityBest *bi = cb_get(bint, 96, nm);
            const CityBest *bs = cb_get(bstr, 96, nm);
            if (bi) { snprintf(det.worker_int, sizeof det.worker_int, "%.31s", bi->who);
                      det.worker_int_val = bi->val; }
            if (bs) { snprintf(det.worker_str, sizeof det.worker_str, "%.31s", bs->who);
                      det.worker_str_val = bs->val; }
        }
        det.morale = 70;        /* 定稿 J4：士气初值 70、上限 100、每回合不衰减 */
        s3_strategy_set_city_detail(st, n, &det);
        ++n;
    }
    s3_ini_free(ini);
    /* 名册按城归属标"是否我方"（执行者只在我方城挑） */
    if (roster) {
        for (int i = 0; i < s3_strategy_count(st); ++i) {
            const char *cn = s3_strategy_city_name(st, i);
            if (cn) s3_roster_mark_city(roster, cn, s3_strategy_city_mine(st, i));
        }
    }
    printf("strategy: %d cities (%d mine)\n", n, n_mine);
    ALOG("strategy: %d cities (%d mine)", n, n_mine);
    /* 数据层抽样日志（实机 logcat / PC 控制台可直接核对面板九行取值 + 插旗旗号） */
    for (int i = 0; i < n && i < 6; ++i) {
        const char *cn = s3_strategy_city_name(st, i);
        const S3CityDetail *dt = s3_strategy_city_detail(st, i);
        if (!dt) { printf("  city[%d] %s: (no detail)\n", i, cn ? cn : "?"); continue; }
        printf("  city[%d] %s: lord=%s gens=%d dev=%d people=%d money=%d res=%d size=%d fr=%d flag=%d\n",
               i, cn ? cn : "?", dt->lord, dt->n_generals, dt->dev,
               dt->people, dt->money, dt->reserve, dt->size, dt->friendliness,
               s3_strategy_city_flag(st, i));
        ALOG("city[%d] %s lord=%s gens=%d dev=%d people=%d money=%d res=%d fr=%d flag=%d",
             i, cn ? cn : "?", dt->lord, dt->n_generals, dt->dev,
             dt->people, dt->money, dt->reserve, dt->friendliness,
             s3_strategy_city_flag(st, i));
    }
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

/* ------------------------------------------------- 行政主選單：命令执行（S5，2026-09-16）
 * 命令清单由用户对照原版逐一核对（見 docs 第五节）：
 *   內政 移動/搜索/開發/人才 · 軍政 徵兵/訓練/戰爭/整備/調兵 · 外交 同盟/離間 ·
 *   任免 太守/軍師/將軍 · 計略 調查/離間/情報 · 系統 進度存檔/進度讀取/設定調整/回主選單 ·
 *   休息 確定（= 结束本月）
 * 已实现（闭环）：搜索(简化) / 開發 / 徵兵 / 訓練 / 調查 / 情報(全局) /
 *                進度存檔 / 回主選單 / 確定(月度结算)。
 * 其余返回 0 → 菜单自动提示「尚未實現」，等对应玩法系统接入。
 *
 * ⚠ 数值口径属**首版自定**（原版公式未逆向），按用户口述的结构搭骨架：
 *   · 開發：花钱买开发度；
 *   · 開發度 → 「確定」时的每月金錢收入 + 人口成長；
 *   · 徵兵：人口与金钱双消耗 → 兵士增加；
 *   实测后按原版感觉再调。 */
static S3Strategy  *g_ast = NULL;          /* 战略层实例（命令就地改城池数据） */
static S3AdminMenu *g_adm = NULL;
static S3Kingdom   *g_kd  = NULL;
static const char  *g_start_path = NULL;
static int          g_admin_quit = 0;      /* 置 1 → 主循环回主菜单 */
static int          g_admin_map  = 0;      /* 置 1 → 朝堂切到大地图（確定后） */
static int          g_month = 1;
/* 待执行命令（原版指令流：朝堂点命令 → 切大地图 → 点我方城 → 该城武将执行）。
 * -1 = 无。 */
static int          g_pending_group = -1, g_pending_item = -1;
static char         g_pending_label[32] = "";
static int          g_pending_city = -1;    /* 已选定、正在挑执行者的城 */
/* 命令阶段的两个修饰位：
 *   g_pending_enemy —— 目标必须是**非我方**城池（計略 調查/情報）
 *   g_pending_query —— 选完武将只**展示情报**，不执行、不消耗月度行动（定稿 F2） */
static int          g_pending_enemy = 0, g_pending_query = 0;

/* 大地图的按压/拖动状态（原为函数内 static，2026-09-17 提到文件作用域）。
 * ⚠ **必须在视图切换时复位**：长按（=右键）会先产生一次 lclick 边沿 → drag=1，
 *   紧接着 rclick 把视图切回朝堂，那次按压的"抬起"永远不会被地图分支处理
 *   → drag 一直停在 1；等下次再回到地图，第一帧就拿着**旧坐标**触发一次幽灵点击
 *   （实测：进入「情報」命令阶段时凭空选中了"永安"）。
 * 对策：切换视图的边沿统一清零。 */
static int     g_map_drag = 0, g_map_moved = 0, g_map_menu = 0;
static int32_t g_map_px = -1, g_map_py = -1;
static S3Roster    *g_roster = NULL;        /* 武将名册（执行者 + 月度行动限制） */
static S3GenPicker *g_picker = NULL;        /* 执行者选择界面 */
static S3LordPick  *g_lp = NULL;            /* 選擇君主（大地图版，I2） */
static int          g_lp_ready = 0;         /* 1 = 该界面已装载地图+城池（避免重复载入） */

static void admin_say(const char *fmt, ...) {
    char b[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    s3_admin_set_hint(g_adm, b);
    printf("admin: %s\n", b);
    ALOG("admin: %s", b);
}

/* 註：原先这里有个 admin_city_idx()（"选中的城 → 否则我方第一座城"）。
 * 計略 的 調查/情報 在 2026-09-17 改成"选非我方城"的挂起流程后它失去了调用点，
 * 已删除（选中城改由 g_pending_city 携带）。后续 移動/人才 若需要"当前城"语义，
 * 从 git 历史取回即可。 */

/* 情報（定稿 F2）：展示一名武将/军师的详情（目标可为**敌将**）。
 * 纯查询 —— 不消耗月度行动、不需要执行者（定稿 F2 原文未提执行者，
 * 与 F1「指派一名武将/军师执行」的写法不同，故按"纯查询"实现，实测再校准）。 */
static void admin_show_officer(int off_idx) {
    const S3Officer *o = (off_idx >= 0) ? s3_roster_at(g_roster, off_idx) : NULL;
    if (!o) { admin_say("情報：查無此人"); return; }
    admin_say("情報：%s · 所屬 %s · 武力 %d · 智力 %d · 等級 %d · 帶兵上限 %d · %s",
              o->name, o->city[0] ? o->city : "（無屬地）",
              o->str, o->intel, o->level, s3_officer_troop_limit(o),
              o->wild ? "在野" : (o->mine ? "我方" : "他方"));
}

/* 在选定城池上、由选定的执行者执行待执行命令。
 * 执行者能力口径（定稿 A1/B3/C1/C2/C3）：
 *   · 開發/搜索 **不花金钱**，效果量看执行者**智力 + 等级**；
 *   · 徵兵数量看执行者**武力 + 等级**（暂用 等级×40 = 其带兵上限）；
 *   · 訓練 提升**城池整体士气**、不扣钱，量与武力+等级相关（武力权重大）；
 *   · 执行完毕该执行者**本月不可再执行**（o->acted = 1，回合结束清空）。 */
static void admin_execute_pending(int idx, int off_idx) {
    const char *cname = s3_strategy_city_name(g_ast, idx);
    S3CityDetail *cd = s3_strategy_city_detail_mut(g_ast, idx);
    if (!cd || !cname) return;

    const S3Officer *o  = (off_idx >= 0) ? s3_roster_at(g_roster, off_idx) : NULL;
    S3Officer       *om = (off_idx >= 0) ? s3_roster_mut(g_roster, off_idx) : NULL;
    /* 没选到人时退化为"该城最佳者"（仅容错，正常流程不会走到） */
    const char *who_i = o ? o->name : (cd->worker_int[0] ? cd->worker_int : "（無人）");
    const char *who_s = o ? o->name : (cd->worker_str[0] ? cd->worker_str : "（無人）");
    const int   iq    = o ? o->intel : cd->worker_int_val;
    const int   sq    = o ? o->str   : cd->worker_str_val;
    const int   lv    = o ? o->level : 1;

    if (g_pending_group == 0 && g_pending_item == 1) {              /* 搜索 */
        int need = 45 + iq / 4 + lv * 2;                            /* 智力为主，等级加成 */
        if (rand() % 100 < need) {
            switch (rand() % 3) {
            case 0: { int gain = 100 + rand() % 200;
                      cd->money += gain;
                      admin_say("%s「搜索」：%s（智%d 級%d）發現金錢 +%d", cname, who_i, iq, lv, gain); } break;
            case 1:
                admin_say("%s「搜索」：%s（智%d 級%d）發現在野人才（招募需相性，未開放）",
                          cname, who_i, iq, lv); break;
            default:
                admin_say("%s「搜索」：%s（智%d 級%d）發現物品（已入庫，配裝未開放）",
                          cname, who_i, iq, lv); break;
            }
        } else {
            admin_say("%s「搜索」：%s（智%d 級%d）一無所獲", cname, who_i, iq, lv);
        }
    } else if (g_pending_group == 0 && g_pending_item == 2) {       /* 開發：不花钱 */
        int up = 5 + iq / 5 + lv * 2;                               /* 智力 + 等级按比例 */
        cd->dev += up; if (cd->dev > 999) cd->dev = 999;
        admin_say("%s「開發」：%s（智%d 級%d）開發度 +%d → %d", cname, who_i, iq, lv, up, cd->dev);
    } else if (g_pending_group == 1 && g_pending_item == 0) {       /* 徵兵 */
        int nr = lv * S3_TROOPS_PER_LEVEL;                          /* 定稿 C3：等级×40 */
        /* 定稿 J7（2026-09-17 用户终裁）：**每兵 2 金**（原版 10 金，我们刻意更便宜） */
        int cost = nr * S3_RECRUIT_GOLD_PER_TROOP;
        if (cd->people <= nr + 1000) { admin_say("%s人口不足，徵兵中止（人口 %d）", cname, cd->people); return; }
        if (cd->money < cost) { admin_say("%s金錢不足（徵 %d 兵需 %d，當前 %d）", cname, nr, cost, cd->money); return; }
        cd->money -= cost; cd->people -= nr; cd->reserve += nr;
        admin_say("%s「徵兵」：%s（武%d 級%d）征得 %d 兵（兵士 %d，人口 -%d，金錢 -%d）",
                  cname, who_s, sq, lv, nr, cd->reserve, nr, cost);
    } else if (g_pending_group == 1 && g_pending_item == 1) {       /* 訓練：不扣钱，升士气 */
        /* 定稿 C2/J4（C4 裁决）：公式 5 + 武力/5 + 等级，结果**钳到 25~30**（对齐甲 §4.3） */
        int up = 5 + sq / 5 + lv;                                   /* 武力权重大 */
        if (up < S3_TRAIN_MORALE_MIN) up = S3_TRAIN_MORALE_MIN;
        if (up > S3_TRAIN_MORALE_MAX) up = S3_TRAIN_MORALE_MAX;
        cd->morale += up; if (cd->morale > 100) cd->morale = 100;
        admin_say("%s「訓練」：%s（武%d 級%d）士氣 +%d → %d（城池整體）",
                  cname, who_s, sq, lv, up, cd->morale);
    } else if (g_pending_group == 4 && g_pending_item == 0) {       /* 調查（定稿 F1） */
        /* 有效期 6 个月：含调查当月 → 有效至 当月+5。到期后城池详情重新变回"未調查"。 */
        const int until = g_month + 5;
        if (s3_strategy_city_investigate(g_ast, idx, until) == 0)
            admin_say("%s「調查」：%s（智%d 級%d）完成 —— 城池詳情可見，有效至第 %d 月",
                      cname, who_i, iq, lv, until);
        else
            admin_say("%s「調查」失敗（我方城池無需調查）", cname);
    }
    if (om) om->acted = 1;      /* 定稿 A2：本月不可再执行（回朝堂后可看到其置灰） */
    /* 不自动返回朝堂，可连续选城（定稿 A5） */
}

/* 结束命令阶段：清挂起 + 恢复菜单 + 清信息条引导 */
static void admin_cancel_pending(void) {
    if (g_pending_group < 0) return;
    admin_say("已取消「%s」", g_pending_label);
    g_pending_group = g_pending_item = -1;
    g_pending_label[0] = '\0';
    g_pending_enemy = g_pending_query = 0;
    s3_strategy_set_banner(g_ast, "");
    s3_admin_set_visible(g_adm, 1);
    s3_admin_collapse(g_adm);       /* 回朝堂时子选单必须是收起态（2026-09-22 用户实测指正） */
}

/* ---------------------------------------------------- 回主選單（唯一出口）
 * 所有退出到主菜单首界面（開始遊戲/讀取進度…）的路径都必须走这里 ——
 * 清场景栈 + 复位根 + 清挂起命令/选择器，避免再出现"停在選擇劇本"的路径遗漏。
 *
 * 背景（BUG-2，2026-09-17 用户口径）：
 *   · 朝堂**没有返回功能**，只能走「系統 → 回主選單」；
 *   · **只有「開始遊戲」才进選擇劇本**（執行遊戲阶段留在场景栈里的 SC_AGE 不得再被显示）。
 * 此前朝堂分支直接 `mode = 0`，app.roots 仍指向上一个场景根（SC_AGE 選擇時期）
 * → 长按返回跳到了選擇劇本界面。 */
static void goto_main_menu(App *app, int *mode) {
    admin_cancel_pending();
    if (g_picker) s3_picker_close(g_picker);
    g_admin_quit = 0;
    g_admin_map  = 0;
    g_pending_city = -1;
    if (g_adm) s3_admin_set_visible(g_adm, 1);      /* 命令阶段收起的菜单恢复 */
    if (g_ast) s3_strategy_set_banner(g_ast, "");
    app->sp = 0;                                    /* 清场景栈（防再落到中途场景） */
    app->roots = SC_MAIN; app->n_roots = NARR(SC_MAIN);
    app->hover = app->press = 0;
    *mode = 0;
    printf("goto main menu (SC_MAIN)\n");
    ALOG("goto main menu (SC_MAIN)");
}

/* 朝堂点「需要城池+执行者」的命令 → 挂起，切大地图等玩家选城。
 * 菜单**收起**（否则遮住城池没法选，2026-09-16 用户实测反馈），
 * 引导文案走信息条（banner）。 */
static int admin_set_pending_x(int group, int item, const char *label,
                               int want_enemy, int query);
static int admin_set_pending(int group, int item, const char *label) {
    return admin_set_pending_x(group, item, label, 0, 0);
}

/* 同上，带修饰位：
 *   want_enemy = 1 → 只接受**非我方**城池（定稿 F1/F2 的計略）
 *   query      = 1 → 选完只展示情报，不执行命令、不消耗执行者行动（定稿 F2） */
static int admin_set_pending_x(int group, int item, const char *label,
                               int want_enemy, int query) {
    g_pending_group = group; g_pending_item = item;
    g_pending_enemy = want_enemy ? 1 : 0;
    g_pending_query = query ? 1 : 0;
    snprintf(g_pending_label, sizeof g_pending_label, "%s", label);
    g_admin_map = 1;
    s3_admin_set_visible(g_adm, 0);          /* 收起菜单 */
    s3_admin_collapse(g_adm);                /* 子选单也收起（回到朝堂时不应还展着） */
    s3_admin_set_hint(g_adm, "");
    {
        char b[168];
        snprintf(b, sizeof b, "「%s」命令階段：請點選%s城池（可連續執行，長按返回朝堂）",
                 label, want_enemy ? "**非我方**" : "我方");
        s3_strategy_set_banner(g_ast, b);
    }
    admin_say("進入「%s」命令階段：請點選%s城池", label, want_enemy ? "非我方" : "我方");
    return 1;
}

static int admin_cmd_cb(void *ud, int group, int item, const char *label) {
    (void)ud;
    /* ---- 系統（不需要选城） ---- */
    if (group == 5) {
        switch (item) {
        case 0:                                             /* 進度存檔 */
            if (g_start_path && g_kd && save_start_state(g_start_path, g_kd) == 0)
                admin_say("已存檔：%s（%s）", s3_kingdom_lord_name(g_kd, s3_kingdom_selected(g_kd)),
                          g_start_path);
            else admin_say("存檔失敗");
            return 1;
        case 3:                                             /* 回主選單 */
            g_admin_quit = 1;
            admin_say("回主選單");
            return 1;
        default: return 0;                                  /* 進度讀取 / 設定調整 */
        }
    }
    /* ---- 休息「確定」= 结束本月：对我方全部城做月度结算，然后切到大地图 ---- */
    if (group == 6) {
        int n = s3_strategy_count(g_ast);
        long long money = 0, people = 0, troop = 0;
        for (int i = 0; i < n; ++i) {
            S3CityDetail *d = s3_strategy_city_detail_mut(g_ast, i);
            if (!d || !s3_strategy_city_mine(g_ast, i)) continue;
            /* 定稿 J1/J2（C1 裁决）：原版是**每半年**结算，我们按月摊平 → 各除以 6 */
            int income = (d->people / 100 + d->dev * 2) / 6;         /* 税收 */
            int growth = (int)((long long)d->people * d->dev * 3 / 100000) / 6;  /* 人口成长 */
            d->money  += income;
            d->people += growth;
            money += d->money; people += d->people; troop += d->reserve;
        }
        ++g_month;
        s3_strategy_set_month(g_ast, g_month);
        /* 定稿 M2/M3：月度自动经验 + 自动升级（A1 比例制）—— 需求①「我方将领自动升级」 */
        int ups = g_roster ? s3_roster_monthly_growth(g_roster) : 0;
        if (g_roster) s3_roster_end_turn(g_roster);   /* 定稿 A2：回合结束清空"本月已行动" */
        admin_say("第 %d 月開始 —— 金錢 %lld · 人口 %lld · 兵士 %lld（稅收/成長按月攤平）",
                  g_month, money, people, troop);
        if (ups > 0) admin_say("本月自動升級：%d 名武將（月度自動經驗 · 方案 A1）", ups);
        g_admin_map = 1;                        /* 结算完切到大地图看局面 */
        return 1;
    }

    /* ---- 計略（定稿 F1/F2）：三个指令都是"选**非我方**城池"的流程 ----
     * 調査 = 我方全軍任選执行者 → 解锁该城详情，有效期 6 个月
     * 情報 = 在**已调查（有效期内）**的城里点一名武将 → 看其详情（纯查询，不消耗行动）
     * 離間 = 尚未实现 */
    if (group == 4) {
        if (item == 0) return admin_set_pending_x(4, 0, label, 1, 0);   /* 調查 */
        if (item == 2) return admin_set_pending_x(4, 2, label, 1, 1);   /* 情報 */
        return 0;                                                       /* 離間 */
    }

    /* ---- 需要城池 + 执行者的命令 → 挂起，切大地图选城（原版指令流） ----
     * 內政 搜索/開發 · 軍政 徵兵/訓練（移動/人才/物品/戰爭/整備/調兵等占位） */
    if ((group == 0 && (item == 1 || item == 2)) ||
        (group == 1 && (item == 0 || item == 1)))
        return admin_set_pending(group, item, label);
    return 0;   /* 其余：占位，菜单自动提示「尚未實現」 */
}

/* 呈现模式切换（幂等，记在静态 cur 里）：
 *   0 = 菜单（640×480 逻辑画布 + EXTEND 条带 + NEAREST）
 *   1 = 朝堂（640×480 逻辑画布 + **STRETCH 整图拉伸** + NEAREST）
 *       —— 2026-09-16 用户实测：朝堂用 EXTEND 会镜像出重复柱子、比例别扭，
 *          原版（Winlator 实测截图）就是整图拉伸铺满，照做。
 *   2 = 大地图（素材原生分辨率视口 + COVER + BILINEAR） */
static int32_t g_vw = 1024, g_vh = 768;    /* 地图视口尺寸（mode 3 进入时计算） */
static void apply_present(Sango3Presenter *p, int kind, int32_t cw, int32_t ch) {
    static int cur = -1;
    if (cur == kind) return;
    if (kind == 2) {
        sango3_presenter_set_logical_size(p, g_vw, g_vh);
        sango3_presenter_set_aspect(p, SANGO3_ASPECT_COVER);
        sango3_presenter_set_filter(p, SANGO3_FILTER_BILINEAR);
    } else {
        sango3_presenter_set_logical_size(p, cw, ch);
        sango3_presenter_set_aspect(p, kind == 1 ? SANGO3_ASPECT_STRETCH
                                                 : SANGO3_ASPECT_EXTEND);
        sango3_presenter_set_filter(p, SANGO3_FILTER_NEAREST);
    }
    cur = kind;
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
            /* ================= 选择君主（**大地图版**，定稿 I2）=================
             * 地图 + 左侧君主列表（君主/相/武/智/忠/士）+ 选中君主的城池高亮插旗 +
             * 右侧城池面板（strategy 的 7400）+ 右侧肖像 + 底部统计栏。
             * 旧的 kingdom_scene 纯列表退役（模块保留，供其它入口复用）。 */
            if (!st) st = s3_strategy_new(read_asset_cb, &ctx, draw_text_cb, &fc);
            if (!g_lp) g_lp = s3_lordpick_new(draw_text_cb, &fc, read_asset_cb, &ctx);
            /* 只在**首次进入**装载地图/城池（选君主期间反复点行不重载） */
            if (!g_lp_ready) {
                if (s3_strategy_set_map(st, "Shape\\AD\\Base\\Map.shp") != 0)
                    ALOG("strategy map load FAILED");
                if (s3_strategy_set_panel(st, "Shape\\AD\\Base\\CityInfo.shp") != 0)
                    ALOG("city panel load FAILED");
                s3_strategy_clear_cities(st);
                if (kd) {
                    int sid = s3_kingdom_scenario(kd);
                    /* my_lord 传 NULL：此时还没有我方 → 全部城"他方"，只靠旗色区分势力 */
                    load_cities(st, &ctx, sid, NULL, NULL);
                }
                {   /* 视口与大地图一致（1:1 裁取，COVER 铺满） */
                    int32_t ww = 0, wh = 0;
                    sango3_presenter_window_size(p, &ww, &wh);
                    int32_t mw = 1024, mh = 768, vw = mw, vh = mh;
                    if (ww > 0 && wh > 0) {
                        if ((int64_t)ww * mh >= (int64_t)wh * mw) vh = (int32_t)((int64_t)mw * wh / ww);
                        else                                        vw = (int32_t)((int64_t)mh * ww / wh);
                    }
                    if (vw < 64) vw = 64;
                    if (vh < 64) vh = 64;
                    s3_strategy_set_viewport(st, vw, vh);
                    if (!cv_map || cv_map->w != vw || cv_map->h != vh) {
                        if (cv_map) sango3_canvas_free(cv_map);
                        cv_map = sango3_canvas_new(vw, vh, 0, 0, 0);
                    }
                    s3_strategy_set_panel_zoom(st, 1);      /* 给肖像留位置，用原版尺寸 */
                    s3_strategy_set_view(st, 0);            /* 地图视图（非朝堂） */
                    s3_lordpick_bind(g_lp, kd);
                    s3_lordpick_reset(g_lp);
                    g_lp_ready = 1;
                    printf("lord select (map) ready: %d cities\n", s3_strategy_count(st));
                    ALOG("lord select (map) ready: %d cities", s3_strategy_count(st));
                }
            }

            apply_present(p, 2, cw, ch);        /* 与大地图同款：COVER + BILINEAR */
            s3_strategy_set_month(st, 1);
            s3_strategy_render(st, cv_map);
            /* 底部统计 + 肖像：按当前选中君主算 */
            {
                int si = s3_lordpick_selected(g_lp);
                const char *lord = (si >= 0) ? s3_kingdom_lord_name(kd, si) : NULL;
                int cities = 0, forts = 0, gens = 0;
                long long troops = 0, people = 0, money = 0;
                if (lord) {
                    for (int i = 0; i < s3_strategy_count(st); ++i) {
                        const S3CityDetail *d = s3_strategy_city_detail(st, i);
                        if (!d || strcmp(d->lord, lord)) continue;
                        const char *cn = s3_strategy_city_name(st, i);
                        /* 關口判定走权威名单（`LoadCity.txt` 的 19 座關隘），
                         * 不再用"以「關」结尾" —— 见文件头部 city_is_fort() 说明。 */
                        if (city_is_fort(cn)) ++forts;
                        else                  ++cities;
                        gens   += d->n_generals;
                        troops += d->reserve;
                        people += d->people;
                        money  += d->money;
                    }
                }
                s3_lordpick_set_stats(g_lp, cities, forts, gens, troops, people, money);
            }
            s3_lordpick_render(g_lp, cv_map);
            sango3_presenter_upload(p, cv_map->px);
            if (sango3_presenter_frame(p, &drawn)) break;
            if (frames && drawn >= frames) break;

            S3Pointer pt;
            sango3_presenter_pointer(p, &pt);
            s3_lordpick_on_move(g_lp, pt.inside ? (int32_t)pt.lx : -1,
                                      pt.inside ? (int32_t)pt.ly : -1);
            if (pt.rclick) {                       /* 长按/返回 = 回到選擇時期 */
                mode = 0; app.hover = app.press = 0;
            } else {
                static int lp_down = 0;
                static int32_t lp_x = -1, lp_y = -1;
                if (pt.lclick) { lp_down = 1; lp_x = (int32_t)pt.lx; lp_y = (int32_t)pt.ly; }
                if (!pt.ldown && lp_down) {
                    lp_down = 0;
                    int idx = -1, ok = 0, cancel = 0;
                    int consumed = s3_lordpick_on_click(g_lp, lp_x, lp_y, &idx, &ok, &cancel);
                    if (idx >= 0) {
                        /* 换君主：重算城池归属 + 选中其主城（面板随之显示） */
                        const char *lord = s3_kingdom_lord_name(kd, idx);
                        /* ⚠ 必须回写 kingdom_scene 的选中下标：进战略层（mode 3）时
                         * my_lord 取自 s3_kingdom_selected(kd)，不回写就会拿旧值/空值
                         * → 全城「我方 0」（我方金框、我方全軍名册全失效）。
                         * 2026-09-17 实机发现并修复。 */
                        s3_kingdom_select(kd, idx);
                        int mine = s3_strategy_remark_owner(st, lord);
                        s3_strategy_select(st, s3_strategy_first_city_of(st, lord));
                        char b[160];
                        snprintf(b, sizeof b, "已選 %s：%d 城（我方城池已高亮，點其它城可查看）",
                                 lord, mine);
                        s3_strategy_set_banner(st, b);
                        printf("lord select: %s (%d cities)\n", lord, mine);
                        ALOG("lord select: %s (%d cities)", lord, mine);
                    } else if (!consumed) {
                        s3_strategy_on_click(st, lp_x, lp_y);   /* 点地图选城看面板 */
                    }
                    if (ok) {
                        int si = s3_lordpick_selected(g_lp);
                        if (si >= 0) {
                            int sc = s3_kingdom_scenario(kd);
                            snprintf(start_msg, sizeof start_msg, "%s · 君主 %s（%d 城）",
                                     (sc >= 1 && sc <= 7) ? SCENARIO_NAMES[sc] : "",
                                     s3_kingdom_lord_name(kd, si),
                                     s3_kingdom_lord_cities(kd, si));
                            if (save_start_state(start_path, kd) == 0) {
                                printf("start state saved: %s -> %s\n", start_msg, start_path);
                                ALOG("start saved: %s -> %s", start_msg, start_path);
                            }
                            g_lp_ready = 0;      /* 下次进来重新装载（重开局） */
                            mode = 3;
                        }
                    } else if (cancel) {
                        g_lp_ready = 0;
                        mode = 0; app.hover = app.press = 0;
                    }
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
            /* 城池信息面板底图（原版 7400 / AD\Base\CityInfo.shp，标签烘焙在图内） */
            if (s3_strategy_set_panel(st, "Shape\\AD\\Base\\CityInfo.shp") != 0)
                ALOG("city panel load FAILED");
            s3_strategy_clear_cities(st);
            /* ⚠ 名册（roster）必须在 load_cities **之前**建好并清空 —— BUG-1 根因：
             * load_cities 内部会 s3_roster_add() 逐将装名册、末尾 s3_roster_mark_city()
             * 标归属；原先 new/clear 写在 load_cities 之后，
             *   ① 首次进战略层时 g_roster 仍是 NULL → load_cities 里所有 `if (roster)`
             *      分支全跳过 → 名册一条都不装；
             *   ② 重开局时虽有名册，但 clear 在装完之后 → 刚装好的名册被清空。
             * 两条路都让 s3_roster_workers() 返回 0 → 执行者选择窗永远为空。 */
            if (!g_roster) g_roster = s3_roster_new();
            else           s3_roster_clear(g_roster);      /* 重开局 → 名册重建 */
            if (kd) {
                int sid = s3_kingdom_scenario(kd);
                int si  = s3_kingdom_selected(kd);
                const char *my_lord = si >= 0 ? s3_kingdom_lord_name(kd, si) : NULL;
                /* 定稿 Q 区 / 甲 §16.2：**第 N 个剧本 → 武将初始等级 = N**（新君主暂不区分）。
                 * 必须在 load_cities（内部 add 逐将）之前设好。 */
                s3_roster_set_base_level(g_roster, sid);
                s3_strategy_set_my_lord(st, my_lord);
                load_cities(st, &ctx, sid, my_lord, g_roster);
                printf("roster: %d officers (base level %d)\n",
                       s3_roster_count(g_roster), s3_roster_base_level(g_roster));
                ALOG("roster: %d officers (base level %d)",
                     s3_roster_count(g_roster), s3_roster_base_level(g_roster));
                /* 逐城统计可执行者（只列我方城）—— 实机 logcat 可据此复核 BUG-1 是否修好 */
                for (int i = 0; i < s3_strategy_count(st); ++i) {
                    if (!s3_strategy_city_mine(st, i)) continue;
                    const char *cn = s3_strategy_city_name(st, i);
                    if (!cn) continue;
                    int tot  = s3_roster_worker_count(g_roster, cn, 0);
                    int idle = s3_roster_worker_count(g_roster, cn, 1);
                    printf("roster: %d officers / %d idle in %s\n", tot, idle, cn);
                    ALOG("roster: %d officers / %d idle in %s", tot, idle, cn);
                }
                /* 朝堂背景轮换（自拟口径：按我方城池数分档 —— 用户确认原版"按发展
                 * 规模"换背景，但判断方式未逆向；先 1-5 城 BG001 / 6-15 BG002 /
                 * ≥16 BG003，实测再调） */
                int mine = 0;
                for (int i = 0; i < s3_strategy_count(st); ++i)
                    if (s3_strategy_city_mine(st, i)) ++mine;
                char bgp[80];
                snprintf(bgp, sizeof bgp, "Shape\\AD\\Background\\%s.shp",
                         mine >= 16 ? "BG003" : mine >= 6 ? "BG002" : "BG001");
                s3_strategy_set_court_bg(st, bgp);
            }
            /* 视口尺寸按屏幕宽高比取（保持地图原生像素 1:1 → 不放大不缩水）；
             * 视口比例 == 屏幕比例 → COVER 下正好铺满，多余部分靠拖动查看。 */
            {
                int32_t ww = 0, wh = 0;
                sango3_presenter_window_size(p, &ww, &wh);
                int32_t mw = 1024, mh = 768;
                int32_t vw = mw, vh = mh;
                if (ww > 0 && wh > 0) {
                    if ((int64_t)ww * mh >= (int64_t)wh * mw) {     /* 屏幕更宽 → 宽满 */
                        vh = (int32_t)((int64_t)mw * wh / ww);
                    } else {                                        /* 屏幕更窄 → 高满 */
                        vw = (int32_t)((int64_t)mh * ww / wh);
                    }
                }
                if (vw < 64) vw = 64;
                if (vh < 64) vh = 64;
                s3_strategy_set_viewport(st, vw, vh);
                if (!cv_map || cv_map->w != vw || cv_map->h != vh) {
                    if (cv_map) sango3_canvas_free(cv_map);
                    cv_map = sango3_canvas_new(vw, vh, 0, 0, 0);
                }
                g_vw = vw; g_vh = vh;
                ALOG("strategy viewport %dx%d (window %dx%d)", vw, vh, ww, wh);
            }
            /* 行政主選單（原版 8000）：常驻左侧，位置按原版 (45,36) × zoom。
             * 朝堂 / 大地图两个视图共用。 */
            if (!g_adm) g_adm = s3_admin_new(read_asset_cb, &ctx, draw_text_cb, &fc,
                                            admin_cmd_cb, NULL);
            if (!g_picker) g_picker = s3_picker_new(draw_text_cb, &fc);
            /* 名册的 new/clear 已提前到 load_cities 之前（BUG-1） */
            g_ast = st; g_kd = kd; g_start_path = start_path; g_admin_quit = 0;
            g_pending_group = g_pending_item = -1; g_pending_label[0] = '\0';
            g_pending_city = -1;
            g_pending_enemy = g_pending_query = 0;
            s3_strategy_set_banner(st, "");     /* 清掉"选君主"界面的残留文案 */
            s3_picker_close(g_picker);
            s3_admin_set_origin(g_adm, 45 * 2, 36 * 2);
            s3_admin_set_visible(g_adm, 1);
            s3_admin_collapse(g_adm);       /* 新开局：子选单从收起态开始 */
            /* 朝堂背景已在 load_cities 后按势力规模选定 */
            s3_strategy_set_month(st, g_month);
            mode = 5;                        /* 先进朝堂（内政阶段），確定后再看地图 */
        }
        if (mode == 5 || mode == 4) {
            /* ============ 战略层：朝堂（5，内政阶段）/ 大地图（4） ============ */
            const int court = (mode == 5);
            apply_present(p, court ? 1 : 2, cw, ch);
            s3_strategy_set_view(st, court);
            s3_strategy_set_panel_zoom(st, court ? 1 : 2);   /* 640 画布用原版尺寸 */
            /* 菜单几何随视图：朝堂 640×480 用原版尺寸（zoom 1，2026-09-16 用户反馈
             * zoom 2 比例过大观感差）；大地图 1024×768 视口用 zoom 2。 */
            s3_admin_set_zoom(g_adm, court ? 1 : 2);
            s3_admin_set_origin(g_adm, 45 * (court ? 1 : 2), 36 * (court ? 1 : 2));
            /* ⚠ 只在"切进朝堂"的**边沿**恢复菜单 —— 不能每帧 set_visible(1)，
             *   否则命令阶段刚收起的菜单下一帧又被顶回来（2026-09-16 实测踩坑）。 */
            {
                static int last_court = -1;
                if (last_court != court) {
                    /* 朝堂 = 内政阶段 → 显示行政主選單；大地图 = 看局面/选城 → **收起**。
                     * 2026-09-17 用户实机反馈：走「休息→確定」进地图后菜单又冒出来了
                     * （原先只在切进朝堂时 set_visible(1)，切到地图时没复位）。 */
                    s3_admin_set_visible(g_adm, court ? 1 : 0);
                    /* 进朝堂时从"无展开"开始；离开朝堂（去地图）时子选单也收起 ——
                     * 否则下次回朝堂会沿用旧展开态（2026-09-22 用户实测指正）。 */
                    s3_admin_collapse(g_adm);
                    /* 视图切换 → 地图按压状态清零（否则会带着旧坐标"幽灵点击"，见上面注释） */
                    g_map_drag = g_map_moved = g_map_menu = 0;
                    g_map_px = g_map_py = -1;
                    last_court = court;
                }
            }
            if (court) {
                /* 朝堂里没有地图可点 → "当前城"固定为我方第一座城（主城） */
                int first = -1;
                for (int i = 0; i < s3_strategy_count(st); ++i)
                    if (s3_strategy_city_mine(st, i)) { first = i; break; }
                s3_strategy_select(st, first);
            }
            Sango3Canvas *cvc = court ? cv : cv_map;
            s3_strategy_render(st, cvc);
            s3_admin_render(g_adm, cvc);
            s3_picker_render(g_picker, cvc);
            sango3_presenter_upload(p, cvc->px);
            if (sango3_presenter_frame(p, &drawn)) break;

            S3Pointer pt;
            sango3_presenter_pointer(p, &pt);
            s3_admin_on_move(g_adm, pt.inside ? (int32_t)pt.lx : -1,
                                   pt.inside ? (int32_t)pt.ly : -1);
            /* ---- 执行者选择界面：激活时独占事件（定稿 A1） ---- */
            if (s3_picker_active(g_picker)) {
                static int pk_down = 0;
                static int32_t pk_x = -1, pk_y = -1;
                s3_picker_on_move(g_picker, pt.inside ? (int32_t)pt.lx : -1,
                                            pt.inside ? (int32_t)pt.ly : -1);
                if (pt.rclick) {                        /* 长按/返回 = 取消本次命令 */
                    s3_picker_close(g_picker);
                    admin_cancel_pending();
                    mode = 5;
                } else {
                    int off = -1, cancel = 0;
                    if (pt.lclick) { pk_down = 1; pk_x = (int32_t)pt.lx; pk_y = (int32_t)pt.ly; }
                    if (!pt.ldown && pk_down) {
                        pk_down = 0;
                        s3_picker_on_click(g_picker, pk_x, pk_y, &off, &cancel);
                        if (off >= 0) {
                            if (g_pending_query) {
                                /* 定稿 F2「情報」：纯查询 —— 展示该武将详情，
                                 * 不消耗月度行动、不结束命令阶段（可继续看别人） */
                                admin_show_officer(off);
                                char b[168];
                                snprintf(b, sizeof b,
                                         "「情報」：已查看，可繼續點選其它城池（長按返回朝堂）");
                                s3_strategy_set_banner(g_ast, b);
                            } else {
                                admin_execute_pending(g_pending_city, off);
                                char b[168];
                                snprintf(b, sizeof b,
                                         "「%s」命令階段：已執行，可繼續點選其它城池（長按返回朝堂）",
                                         g_pending_label);
                                s3_strategy_set_banner(g_ast, b);
                            }
                            g_pending_city = -1;
                        } else if (cancel) {
                            admin_cancel_pending();
                            mode = 5;
                        }
                    }
                }
                SDL_Delay(16);
                continue;                               /* 选择器激活时不响应地图 */
            }
            if (g_admin_quit) {
                /* 「系統 → 回主選單」= 主菜单首界面（開始遊戲/讀取進度…），不是中途场景
                 * （2026-09-16 用户指正）—— 走唯一出口 goto_main_menu() */
                goto_main_menu(&app, &mode);
            } else if (g_admin_map) {
                g_admin_map = 0;
                if (court) mode = 4;         /* 「確定」月度结算完 → 切大地图 */
            } else if (pt.rclick) {
                if (g_pending_group >= 0) admin_cancel_pending();   /* 结束命令阶段 */
                /* 朝堂**没有返回功能**（2026-09-17 用户口径）：只能走「系統 → 回主選單」，
                 * 右键/长按/返回键在朝堂一律忽略。此前这里 `mode = 0` 会让它落到场景栈里
                 * 的選擇劇本界面（BUG-2）。 */
                if (!court) mode = 5;                               /* 地图右键 → 回朝堂 */
            } else if (!court) {
                /* 拖动查看地图：按下→移动（超阈值算拖动，地图随手走）→
                 * 抬起且未移动过才算点击选城（避免拖动误选）。
                 * ⚠ 落在行政主選單上的按压一律归菜单，既不拖动地图也不选城。
                 * 状态用文件作用域的 g_map_*（视图切换时要能复位，见其定义处注释）。 */
                if (pt.lclick) {
                    g_map_menu = s3_admin_hit(g_adm, (int32_t)pt.lx, (int32_t)pt.ly) ? 1 : 0;
                    g_map_drag = 1; g_map_moved = 0;
                    g_map_px = (int32_t)pt.lx; g_map_py = (int32_t)pt.ly;
                } else if (pt.ldown && g_map_drag && !g_map_menu) {
                    int32_t dx = (int32_t)pt.lx - g_map_px;
                    int32_t dy = (int32_t)pt.ly - g_map_py;
                    if (dx > 1 || dx < -1 || dy > 1 || dy < -1) {
                        s3_strategy_pan_view(st, -dx, -dy);   /* 地图反向移动 */
                        g_map_px = (int32_t)pt.lx; g_map_py = (int32_t)pt.ly;
                        g_map_moved = 1;
                    }
                }
                if (!pt.ldown && g_map_drag) {
                    g_map_drag = 0;
                    if (!g_map_moved) {
                        const int32_t spx = g_map_px, spy = g_map_py;
                        if (g_map_menu) s3_admin_on_click(g_adm, spx, spy);
                        else {
                            s3_strategy_on_click(st, spx, spy);
                            /* 指令流：命令阶段点城 → 我方城执行；**不返回朝堂**，
                             * 可连续点选下一座城（2026-09-16 用户反馈），
                             * 右键才结束命令阶段回朝堂。 */
                            if (g_pending_group >= 0) {
                                int cidx = s3_strategy_selected(st);
                                if (cidx >= 0) {
                                    const char *cn = s3_strategy_city_name(st, cidx);
                                    const int mine = s3_strategy_city_mine(st, cidx);
                                    /* 目标阵营校验：計略 需非我方城；其余需我方城 */
                                    if (g_pending_enemy ? mine : !mine) {
                                        admin_say(g_pending_enemy
                                                  ? "「%s」的目標必須是**非我方**城池"
                                                  : "「%s」只能對我方城池執行",
                                                  g_pending_label);
                                    } else if (g_pending_query) {
                                        /* 定稿 F2「情報」：前提 —— 该城在调查有效期内 */
                                        if (!s3_strategy_city_known(st, cidx)) {
                                            admin_say("「情報」需先「調查」%s（調查有效期 6 個月）",
                                                      cn ? cn : "?");
                                        } else {
                                            int cnt = s3_roster_officer_count_in_city(g_roster, cn);
                                            g_pending_city = cidx;
                                            s3_picker_open(g_picker, g_roster, cn,
                                                           g_pending_label, 0, S3_PICK_ANY_CITY);
                                            char b[168];
                                            snprintf(b, sizeof b,
                                                     "「情報」：請選擇要查看的武將/軍師（%s · 共 %d 人）",
                                                     cn ? cn : "?", cnt);
                                            s3_strategy_set_banner(g_ast, b);
                                            printf("picker open(info): city=%s n=%d\n",
                                                   cn ? cn : "?", cnt);
                                            ALOG("picker open(info): city=%s n=%d",
                                                 cn ? cn : "?", cnt);
                                        }
                                    } else {
                                        /* 定稿 A1：选城后 → 打开执行者选择界面。
                                         * 調査（計略）目标是敌城 → 执行者从**我方全軍**里挑；
                                         * 其余指令的执行者就是该城的武将。 */
                                        const int my_all = (g_pending_group == 4 && g_pending_item == 0);
                                        int idle = my_all
                                                 ? s3_roster_worker_count_mine_all(g_roster, 1)
                                                 : s3_roster_worker_count(g_roster, cn, 1);
                                        g_pending_city = cidx;
                                        /* only_idle = 0：**列出全部**，本月已行动者由界面置灰
                                         * 且不可点（定稿 A2 原文是"置灰"，不是"隐藏"） */
                                        s3_picker_open(g_picker, g_roster, my_all ? NULL : cn,
                                                       g_pending_label, 0,
                                                       my_all ? S3_PICK_MY_ALL : S3_PICK_MY_CITY);
                                        printf("picker open: city=%s idle=%d my_all=%d\n",
                                               cn ? cn : "?", idle, my_all);
                                        ALOG("picker open: city=%s idle=%d my_all=%d",
                                             cn ? cn : "?", idle, my_all);
                                        char b[168];
                                        /* 可执行人数直接写进横幅：0 人时用户一眼能分清
                                         * "此地無武將（關隘）/ 本月均已行動"（不再是 BUG-1 那种空） */
                                        snprintf(b, sizeof b,
                                                 "「%s」：請選擇執行此指令的武將/軍師（%s · 可執行 %d 人）",
                                                 g_pending_label,
                                                 my_all ? "我方全軍" : (cn ? cn : "?"), idle);
                                        s3_strategy_set_banner(g_ast, b);
                                    }
                                }
                            }
                        }
                    }
                    g_map_menu = 0;
                }
            } else {
                /* 朝堂：点击只归行政主選單 */
                static int32_t spx = -1, spy = -1;
                static int sdown = 0;
                if (pt.lclick) { sdown = 1; spx = (int32_t)pt.lx; spy = (int32_t)pt.ly; }
                if (!pt.ldown && sdown) { sdown = 0; s3_admin_on_click(g_adm, spx, spy); }
            }
            SDL_Delay(16);
            continue;
        }

        /* ================= 菜单场景 ================= */
        apply_present(p, 0, cw, ch);
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
                /* 场景上下文：同一命令号在不同场景含义不同
                 * （存檔畫面的「確定/取消」cmd=1/2，与主選單的 開始遊戲/讀取進度 撞号；
                 *   存檔槽位 cmd=11..20 与選擇時期的剧本按钮 11..17 撞号 ——
                 *   不按场景区分会点取消又进存档、点存档槽跳剧本，2026-09-16 实测踩坑） */
                const int in_save = (app.n_roots > 0 && app.roots[0] == 220u);
                const int in_age  = (app.n_roots > 0 && app.roots[0] == 100u);
                if (cmd == 6) { printf("command=6 (quit)\n"); break; }
                if (in_save && (cmd == 1 || cmd == 2)) {
                    /* 存/取進度畫面的 確定/取消 → 返回上一場景
                     * （存取功能尚未實現；先保证取消能返回 —— 2026-09-16 用户反馈） */
                    printf("save scene: cmd=%d -> back\n", (int)cmd);
                    ALOG("save scene back (cmd=%d)", (int)cmd);
                    if (app.sp > 0) {
                        --app.sp;
                        app.roots = app.stack[app.sp].roots;
                        app.n_roots = app.stack[app.sp].n;
                    }
                    app.hover = app.press = 0;
                } else if (cmd == 3) {
                    /* 登錄武將 → 创建自定义武将表单 */
                    mode = 1;
                    app.hover = app.press = 0;
                    printf("editor mode (create general)\n");
                    ALOG("editor mode (create general)");
                } else if (cmd >= 11 && cmd <= 17 && in_age) {
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
                } else if (cmd == 40) {
                    printf("click cmd=40 (auto save/load: not implemented)\n");
                    ALOG("click cmd=40 (not implemented)");
                } else {
                    Scene next = scene_for_command(cmd);
                    /* 只有主選單的「開始遊戲」才进選擇劇本（2026-09-17 用户口径）：
                     * 原版命令号是**场景内局部语义**，别的场景里出现 cmd=1 时不得落到
                     * SC_AGE（選擇時期）—— 否则会像 BUG-2 那样"莫名其妙跳到選擇劇本"。 */
                    if (next.roots == SC_AGE &&
                        !(app.n_roots > 0 && app.roots[0] == 1u)) {
                        ALOG("cmd=1 outside main menu -> ignore (roots[0]=%u)",
                             app.n_roots > 0 ? app.roots[0] : 0u);
                        next.n = 0;
                    }
                    if (next.n > 0) {
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
    if (cv_map) sango3_canvas_free(cv_map);
    s3_ui_free(&L);
    s3_ini_free(ini);
    for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
    s3_text_shutdown();
    return 0;
}
