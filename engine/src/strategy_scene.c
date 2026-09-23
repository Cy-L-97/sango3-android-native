/*
 * strategy_scene.c —— 战略层地图渲染（实现，见 strategy_scene.h）
 *
 * 地图整图 1024×768 → 缩放到逻辑画布（640×480，4:3 同比）；
 * 城市按地图像素坐标等比映射；支持拖动平移（地图比画布大时）——
 * 简化版：整图铺满画布（缩放 0.625），后续若需要局部放大再加视口。
 */
#include "strategy_scene.h"
#include "shp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char    name[32];
    int32_t mx, my;      /* 城池图标中心的地图像素坐标（1024×768 空间） */
    int32_t mw, mh;      /* 图标尺寸（地图像素） */
    int     mine;
    int     flag;        /* 势力旗号（Nation.ini 的 Flag，1~35；0 = 未知/无主） */
    S3CityDetail det;    /* 面板九行数据 */
    int     has_detail;
} StratCity;

/* ---------------------------------------------------------------- 势力旗色板
 * 原版旗位图（Shape\AD\Btn\Flag\Flag{势力}{状态}.SHP）是 **type=1** 的特殊版式
 * （29 行行表 + 32 位像素流，与常规 type=0「一行一帧」不同，尚未破解 —— 见 T13）。
 * 故这里先用**程序化旗帜**：旗杆 + 三角旗，颜色按 Flag 号取自本表（32 色，便于分辨）。
 * 纯观感细节，按约定「玩法优先，不逐像素复刻」可降级；后续若破解了原版旗，
 * 只需把 draw_city_flag() 换成贴图，其余逻辑不动。 */
static const uint8_t FLAG_PAL[36][3] = {
    {  0,  0,  0},                                              /* 0 = 未用 */
    {214, 178,  94}, {126, 176, 232}, {232, 120, 120}, {168, 216, 128},
    {226, 146, 214}, {140, 220, 214}, {240, 200, 120}, {176, 156, 232},
    {232, 168, 128}, {150, 200, 160}, {216, 128, 168}, {128, 168, 208},
    {224, 216, 128}, {188, 132, 232}, {128, 224, 176}, {232, 156,  96},
    {160, 176, 208}, {208, 120, 136}, {144, 208, 232}, {232, 184, 168},
    {168, 200, 120}, {200, 148, 200}, {136, 196, 200}, {240, 168, 200},
    {176, 168, 128}, {204, 204, 204}, {136, 144, 160}, {220, 196, 232},
    {152, 168, 136}, {232, 208, 176}, {196, 176, 148}, {188, 188, 220},
    {212, 212, 160}, {164, 188, 200}, {224, 176, 148}
};

struct S3Strategy {
    S3StratReadAsset read_asset; void *asset_ud;
    S3StratDrawText  draw_text;  void *text_ud;

    ShpImage map;                /* 地图整图（RGBA，1024×768） */
    int      map_ok;
    char     map_path[128];

    ShpImage panel;              /* 城池信息面板底图（CityInfo.shp，标签烘焙在图内） */
    int      panel_ok;
    char     panel_path[128];
    int32_t  panel_zoom;         /* 1 = 原版尺寸；高清画布下用 2 才看得清 */
    char     my_lord[32];        /* 我方君主（信息条文案用） */

    ShpImage court;              /* 朝堂背景（AD\Background\BG001~003，640×480） */
    int      court_ok;
    int      is_court;           /* 1 = 朝堂视图（内政阶段） */
    int      hud_on;             /* 1 = 在**本画布**上画信息条/城池面板（2026-09-23：朝堂改由 UI 层单独画） */

    StratCity city[S3_STRAT_MAX_CITIES];
    int       n_cities;
    int       sel;               /* 选中城市下标，-1 = 无 */

    int32_t   off_x, off_y;      /* 平移（逻辑像素，当前版本 0） */
    int32_t   cv_w, cv_h;        /* 最近一次渲染用的画布尺寸（on_click 反算用） */
    /* 视口（地图坐标）：画布只显示这一块，拖动改变 vp_x/vp_y */
    int32_t   vp_x, vp_y, vp_w, vp_h;
    int       vp_set;
    int       month;               /* 当前月份（朝堂信息条显示；確定 = 结束本月） */
    char      banner[128];         /* 信息条临时文案（命令阶段引导），空 = 默认 */
};

static void draw_hud(S3Strategy *s, Sango3Canvas *cv);
/* I1 插旗 / I3 城名（定义在 render 之后，这里前置声明） */
static void draw_city_flag(S3Strategy *s, Sango3Canvas *cv, int32_t x, int32_t y,
                           int32_t hw, int32_t hh, int flag, int mine,
                           const char *lord);
static void draw_city_name(S3Strategy *s, Sango3Canvas *cv, int32_t x, int32_t y,
                           int32_t hw, int32_t hh, const char *name, int mine, int sel);

S3Strategy *s3_strategy_new(S3StratReadAsset read_asset, void *asset_ud,
                            S3StratDrawText draw_text, void *text_ud) {
    S3Strategy *s = (S3Strategy *)calloc(1, sizeof *s);
    if (!s) return NULL;
    s->read_asset = read_asset; s->asset_ud = asset_ud;
    s->draw_text = draw_text;   s->text_ud = text_ud;
    s->sel = -1;
    s->panel_zoom = 2;           /* 战略层画布 = 地图原生 1024×768 → 一档放大 */
    s->hud_on = 1;               /* 默认画 HUD（地图视图要；朝堂由 app 关掉并画到 UI 层） */
    return s;
}

void s3_strategy_free(S3Strategy *s) {
    if (!s) return;
    if (s->map_ok)   shp_free(&s->map);
    if (s->panel_ok) shp_free(&s->panel);
    if (s->court_ok) shp_free(&s->court);
    free(s);
}

void s3_strategy_set_view(S3Strategy *s, int court) { if (s) s->is_court = court ? 1 : 0; }

/* 见 .h：朝堂底图与 UI 分层渲染（2026-09-23） */
void s3_strategy_set_hud_enabled(S3Strategy *s, int on) { if (s) s->hud_on = on ? 1 : 0; }
void s3_strategy_draw_court_ui(S3Strategy *s, Sango3Canvas *cv) { if (s && cv) draw_hud(s, cv); }
int  s3_strategy_court(const S3Strategy *s) { return s ? s->is_court : 0; }

int s3_strategy_set_court_bg(S3Strategy *s, const char *pak_path) {
    if (!s || !pak_path) return -1;
    if (s->court_ok) shp_free(&s->court);
    s->court_ok = 0;
    uint32_t len = 0;
    uint8_t *raw = s->read_asset ? s->read_asset(s->asset_ud, pak_path, &len) : NULL;
    if (!raw) return -2;
    const char *err = NULL;
    int ok = shp_decode(raw, len, &s->court, &err);
    free(raw);
    if (!ok) {
        printf("WARN : court bg decode failed: %s (%s)\n", pak_path, err ? err : "?");
        return -3;
    }
    s->court_ok = 1;
    printf("court bg ok: %s %ux%u\n", pak_path, (unsigned)s->court.width, (unsigned)s->court.height);
    return 0;
}

void s3_strategy_select(S3Strategy *s, int idx) {
    if (!s) return;
    if (idx >= 0 && idx < s->n_cities) s->sel = idx;
}

void s3_strategy_set_month(S3Strategy *s, int month) { if (s) s->month = month; }
int  s3_strategy_month(const S3Strategy *s) { return s ? s->month : 0; }

void s3_strategy_set_banner(S3Strategy *s, const char *text) {
    if (s) snprintf(s->banner, sizeof s->banner, "%s", text ? text : "");
}

int s3_strategy_set_map(S3Strategy *s, const char *pak_path) {
    if (!s || !pak_path) return -1;
    if (s->map_ok && strcmp(s->map_path, pak_path) == 0) return 0;
    if (s->map_ok) { shp_free(&s->map); s->map_ok = 0; }

    uint32_t len = 0;
    uint8_t *raw = s->read_asset ? s->read_asset(s->asset_ud, pak_path, &len) : NULL;
    if (!raw) return -2;
    const char *err = NULL;
    int ok = shp_decode(raw, len, &s->map, &err);
    free(raw);
    if (!ok) {
        printf("WARN : strategy map decode failed: %s (%s)\n", pak_path, err ? err : "?");
        return -3;
    }
    s->map_ok = 1;
    snprintf(s->map_path, sizeof s->map_path, "%s", pak_path);
    if (!s->vp_set) {                      /* 未设视口 → 默认全图 */
        s->vp_x = s->vp_y = 0;
        s->vp_w = s->map.width;
        s->vp_h = s->map.height;
    } else {
        s3_strategy_set_viewport(s, s->vp_w, s->vp_h);   /* 重新 clamp */
    }
    return 0;
}

void s3_strategy_set_viewport(S3Strategy *s, int32_t vw, int32_t vh) {
    if (!s) return;
    int32_t mw = s->map_ok ? s->map.width : 1024;
    int32_t mh = s->map_ok ? s->map.height : 768;
    if (vw <= 0) vw = mw;
    if (vh <= 0) vh = mh;
    if (vw > mw) vw = mw;
    if (vh > mh) vh = mh;
    s->vp_w = vw;
    s->vp_h = vh;
    s->vp_set = 1;
    if (s->vp_x > mw - vw) s->vp_x = mw - vw;
    if (s->vp_y > mh - vh) s->vp_y = mh - vh;
    if (s->vp_x < 0) s->vp_x = 0;
    if (s->vp_y < 0) s->vp_y = 0;
}

void s3_strategy_pan_view(S3Strategy *s, int32_t dx, int32_t dy) {
    if (!s) return;
    int32_t mw = s->map_ok ? s->map.width : 1024;
    int32_t mh = s->map_ok ? s->map.height : 768;
    /* 手指移动 dx → 地图内容反向移动（视口正向） */
    s->vp_x += dx;
    s->vp_y += dy;
    if (s->vp_x < 0) s->vp_x = 0;
    if (s->vp_y < 0) s->vp_y = 0;
    if (s->vp_x > mw - s->vp_w) s->vp_x = mw - s->vp_w;
    if (s->vp_y > mh - s->vp_h) s->vp_y = mh - s->vp_h;
}

int s3_strategy_view_w(const S3Strategy *s) {
    return s ? (s->vp_set ? s->vp_w : (s->map_ok ? s->map.width : 1024)) : 1024;
}
int s3_strategy_view_h(const S3Strategy *s) {
    return s ? (s->vp_set ? s->vp_h : (s->map_ok ? s->map.height : 768)) : 768;
}
int s3_strategy_view_x(const S3Strategy *s) { return s ? s->vp_x : 0; }
int s3_strategy_view_y(const S3Strategy *s) { return s ? s->vp_y : 0; }

void s3_strategy_clear_cities(S3Strategy *s) {
    if (s) { s->n_cities = 0; s->sel = -1; }
}

void s3_strategy_add_city(S3Strategy *s, const char *name,
                          int32_t mx, int32_t my, int32_t mw, int32_t mh, int mine) {
    if (!s || !name || s->n_cities >= S3_STRAT_MAX_CITIES) return;
    StratCity *c = &s->city[s->n_cities++];
    snprintf(c->name, sizeof c->name, "%s", name);
    c->mx = mx; c->my = my;
    c->mw = mw > 0 ? mw : 24;
    c->mh = mh > 0 ? mh : 19;
    c->mine = mine;
}

void s3_strategy_set_city_detail(S3Strategy *s, int idx, const S3CityDetail *d) {
    if (!s || idx < 0 || idx >= s->n_cities) return;
    if (!d) { s->city[idx].has_detail = 0; return; }
    StratCity *c = &s->city[idx];
    c->det = *d;                     /* 定长数组整体拷贝，无悬垂指针 */
    c->det.lord[sizeof c->det.lord - 1] = '\0';
    c->det.adviser[sizeof c->det.adviser - 1] = '\0';
    c->has_detail = 1;
}

void s3_strategy_set_panel_zoom(S3Strategy *s, int32_t zoom) {
    if (s && zoom >= 1 && zoom <= 4) s->panel_zoom = zoom;
}

void s3_strategy_set_my_lord(S3Strategy *s, const char *lord) {
    if (!s) return;
    snprintf(s->my_lord, sizeof s->my_lord, "%s", lord ? lord : "");
}

int s3_strategy_set_panel(S3Strategy *s, const char *pak_path) {
    if (!s || !pak_path) return -1;
    if (s->panel_ok && strcmp(s->panel_path, pak_path) == 0) return 0;
    if (s->panel_ok) { shp_free(&s->panel); s->panel_ok = 0; }

    uint32_t len = 0;
    uint8_t *raw = s->read_asset ? s->read_asset(s->asset_ud, pak_path, &len) : NULL;
    if (!raw) return -2;
    const char *err = NULL;
    int ok = shp_decode(raw, len, &s->panel, &err);
    free(raw);
    if (!ok) {
        printf("WARN : city panel decode failed: %s (%s)\n", pak_path, err ? err : "?");
        return -3;
    }
    s->panel_ok = 1;
    snprintf(s->panel_path, sizeof s->panel_path, "%s", pak_path);
    printf("panel ok: %s %ux%u\n", pak_path, (unsigned)s->panel.width, (unsigned)s->panel.height);
    return 0;
}

/* 千分位整数（人口/金钱这类数值用；缓冲需 >= 16 字节） */
static void fmt_thousands(int32_t v, char *out, int cap) {
    char tmp[16];
    int  n = 0;
    unsigned int u = (v < 0) ? (unsigned)(-v) : (unsigned)v;
    do { tmp[n++] = (char)('0' + (u % 10)); u /= 10; } while (u && n < (int)sizeof tmp);
    int o = 0;
    if (v < 0 && o < cap - 1) out[o++] = '-';
    for (int i = n - 1; i >= 0 && o < cap - 1; --i) {
        out[o++] = tmp[i];
        if (i > 0 && i % 3 == 0 && o < cap - 1) out[o++] = ',';
    }
    out[o] = '\0';
}

/* 地图坐标 → 逻辑坐标：先减去视口原点，再按画布/视口比例换算
 * （视口尺寸 == 画布尺寸时即为 1:1，零缩放） */
static int mine_count(const S3Strategy *s) {
    int n = 0;
    for (int i = 0; i < s->n_cities; ++i) if (s->city[i].mine) ++n;
    return n;
}

static void map_to_logical(const S3Strategy *s, Sango3Canvas *cv,
                           int32_t mx, int32_t my, int32_t *lx, int32_t *ly) {
    int32_t vw = s->vp_w > 0 ? s->vp_w : (s->map_ok ? s->map.width : 1024);
    int32_t vh = s->vp_h > 0 ? s->vp_h : (s->map_ok ? s->map.height : 768);
    *lx = (int32_t)((int64_t)(mx - s->vp_x) * cv->w / vw);
    *ly = (int32_t)((int64_t)(my - s->vp_y) * cv->h / vh);
}

/* 城池信息面板在画布上的矩形（坐标空间 = 调用时的画布）。返回 0 = 当前无面板。
 * render 与 on_click 共用同一套布局，避免两处算法漂移。 */
static int panel_rect(const S3Strategy *s, int32_t cw, int32_t ch,
                      int32_t *px, int32_t *py, int32_t *pw, int32_t *ph) {
    if (!s || !s->panel_ok || s->sel < 0 || s->sel >= s->n_cities) return 0;
    const int32_t z = s->panel_zoom > 0 ? s->panel_zoom : 1;
    *pw = (int32_t)s->panel.width  * z;
    *ph = (int32_t)s->panel.height * z;
    *px = cw - *pw - 24;                       /* 右上角，右边距 24 */
    *py = 64;                                  /* 让开顶部信息条 */
    if (*px < 0) *px = 0;
    if (*py + *ph > ch) *py = ch - *ph;
    if (*py < 0) *py = 0;
    return 1;
}

void s3_strategy_render(S3Strategy *s, Sango3Canvas *cv) {
    if (!s || !cv) return;
    s->cv_w = cv->w; s->cv_h = cv->h;
    sango3_canvas_fill(cv, 0, 0, cv->w, cv->h, 10, 12, 20);

    /* 朝堂视图（内政阶段）：640×480 全屏 CG 铺底，不画地图/城池标记。 */
    if (s->is_court) {
        if (s->court_ok) {
            int32_t bw = (int32_t)s->court.width, bh = (int32_t)s->court.height;
            if (bw == cv->w && bh == cv->h) {          /* 1:1 直拷 */
                for (int32_t y = 0; y < cv->h; ++y)
                    memcpy(cv->px + (size_t)y * cv->w * 4,
                           s->court.rgba + (size_t)y * bw * 4, (size_t)bw * 4);
            } else {                                    /* 尺寸不符 → 最近邻拉伸铺满 */
                for (int32_t y = 0; y < cv->h; ++y) {
                    const uint8_t *srow = s->court.rgba
                        + (size_t)((int64_t)y * bh / cv->h) * bw * 4;
                    uint8_t *drow = cv->px + (size_t)y * cv->w * 4;
                    for (int32_t x = 0; x < cv->w; ++x)
                        memcpy(drow + (size_t)x * 4,
                               srow + (size_t)((int64_t)x * bw / cv->w) * 4, 4);
                }
            }
        }
        if (s->hud_on) draw_hud(s, cv);                /* 信息条 + 城池面板（朝堂视图由 UI 层单独画） */
        return;
    }

    /* 地图：从整图裁取视口区域；视口尺寸 == 画布尺寸时逐行 1:1 直拷（零重采样）。 */
    if (s->map_ok) {
        int32_t mw = s->map.width, mh = s->map.height;
        const uint8_t *src = s->map.rgba;
        int32_t vw = s->vp_w > 0 ? s->vp_w : mw;
        int32_t vh = s->vp_h > 0 ? s->vp_h : mh;
        if (vw == cv->w && vh == cv->h) {
            for (int32_t y = 0; y < cv->h; ++y) {
                int32_t sy = s->vp_y + y;
                if (sy < 0 || sy >= mh) continue;
                memcpy(cv->px + (size_t)y * cv->w * 4,
                       src + (size_t)sy * mw * 4 + (size_t)s->vp_x * 4,
                       (size_t)vw * 4);
            }
        } else {                            /* 尺寸不匹配 → 最近邻兜底 */
            for (int32_t y = 0; y < cv->h; ++y) {
                int32_t sy = s->vp_y + (int32_t)((int64_t)y * vh / cv->h);
                if (sy < 0 || sy >= mh) continue;
                const uint8_t *srow = src + (size_t)sy * mw * 4;
                uint8_t *drow = cv->px + (size_t)y * cv->w * 4;
                for (int32_t x = 0; x < cv->w; ++x) {
                    int32_t sx = s->vp_x + (int32_t)((int64_t)x * vw / cv->w);
                    if (sx < 0 || sx >= mw) continue;
                    memcpy(drow + (size_t)x * 4, srow + (size_t)sx * 4, 4);
                }
            }
        }
    }

    /* 城市标记：己方金色框；选中红框。其余城不额外标记 —— 地图整图自带城池图标。
     * 框大小按该城图标尺寸等比换算（大城/中城/小城/关卡尺寸不同）。
     * I1/I3：**所有城**都要插旗 + 画城名，故不再 `continue` 跳过非我方城。 */
    int32_t vw = s->vp_w > 0 ? s->vp_w : (s->map_ok ? s->map.width : 1024);
    int32_t vh = s->vp_h > 0 ? s->vp_h : (s->map_ok ? s->map.height : 768);
    for (int i = 0; i < s->n_cities; ++i) {
        int sel = (i == s->sel);
        int mine = s->city[i].mine;
        int32_t lx, ly;
        map_to_logical(s, cv, s->city[i].mx, s->city[i].my, &lx, &ly);
        int32_t hw = (int32_t)((int64_t)s->city[i].mw * cv->w / vw / 2) + 3;
        int32_t hh = (int32_t)((int64_t)s->city[i].mh * cv->h / vh / 2) + 3;
        if (hw < 6) hw = 6;
        if (hh < 6) hh = 6;
        if (sel) {
            sango3_canvas_frame(cv, lx - hw, ly - hh, hw * 2, hh * 2, 2, 255, 60, 60);
            sango3_canvas_frame(cv, lx - hw - 2, ly - hh - 2, (hw + 2) * 2, (hh + 2) * 2,
                                1, 255, 200, 120);
        } else if (mine) {
            sango3_canvas_frame(cv, lx - hw, ly - hh, hw * 2, hh * 2, 2, 255, 214, 90);
        }
        /* 城名放图标下方；旗**插在城池点位本身**（与选中框同一中心点）—— 都不遮城名 */
        draw_city_name(s, cv, lx, ly, hw, hh, s->city[i].name, mine, sel);
        draw_city_flag(s, cv, lx, ly, hw, hh, s->city[i].flag, mine,
                       s->city[i].det.lord);
    }

    if (s->hud_on) draw_hud(s, cv);
}

/* ----------------------------------------------------- I1：城池插所属势力旗
 * 位置（2026-09-17 用户二次指正）：**旗杆底下插在城池点位，旗面立在城池上方** ——
 * 绝不能整面旗盖住城池图标（第一版把旗画在城池中心，把城整个糊掉了）。
 * 旗面书势力代表字（君主名首字，如曹操势力写「曹」），颜色仍按 Flag 号取 FLAG_PAL
 * 作第二重区分，我方城加金边。 */
static void draw_city_flag(S3Strategy *s, Sango3Canvas *cv, int32_t x, int32_t y,
                           int32_t hw, int32_t hh, int flag, int mine,
                           const char *lord) {
    if (flag <= 0 || flag >= (int)(sizeof FLAG_PAL / sizeof *FLAG_PAL)) return;
    const uint8_t *c = FLAG_PAL[flag];

    int32_t fw = hw * 2;                        /* 旗面宽 ≈ 城池图标宽 */
    if (fw < 18) fw = 18;
    if (fw > 34) fw = 34;                       /* 上限：别盖到相邻城 */
    int32_t fh = (fw * 7) / 10;
    if (fh < 12) fh = 12;

    const int32_t fy = y - hh - fh - 3;         /* 旗面**整体在图标上沿之上** */
    const int32_t fx = x - fw / 2;              /* 水平居中于城池点位 */
    const int32_t pole_top = fy + 2;            /* 杆顶略低于旗面上沿，露一点旗杆头 */

    /* 旗杆：从旗面往下插到**城池点位本身**（x,y） */
    sango3_canvas_fill(cv, x - 1, pole_top, 2, y - pole_top, 74, 56, 34);
    sango3_canvas_fill(cv, x - 2, y - 4, 4, 4, 56, 42, 26);   /* 杆底加粗一点，像插进城里 */

    sango3_canvas_fill(cv, fx, fy, fw, fh, c[0], c[1], c[2]);               /* 旗面 */
    sango3_canvas_frame(cv, fx, fy, fw, fh, 1,
                        (uint8_t)(c[0] / 2), (uint8_t)(c[1] / 2), (uint8_t)(c[2] / 2));
    if (mine) sango3_canvas_frame(cv, fx - 1, fy - 1, fw + 2, fh + 2, 1, 255, 240, 180);

    /* 势力代表字：君主名首字（复姓如公孫/司馬同样取首字，与原版一致） */
    if (s->draw_text && lord && *lord) {
        char g[8];
        unsigned char c0 = (unsigned char)lord[0];
        int n = 1;
        if (c0 >= 0xF0) n = 4; else if (c0 >= 0xE0) n = 3; else if (c0 >= 0xC0) n = 2;
        if (n > (int)sizeof g - 1) n = (int)sizeof g - 1;
        memcpy(g, lord, (size_t)n); g[n] = '\0';
        const int fnt = (fh >= 20) ? 2 : 1;     /* 20px / 16px 两档，随旗面高度选 */
        s->draw_text(s->text_ud, cv, g, fx, fy, fw, fh, 0x101010u, fnt, 0x8u | 0x4u);
    }
}

/* ------------------------------------------------------- I3：城池名显示层
 * 原版是烘焙位图（Shape\AD\Base\CitiesName.shp，版式特殊未破解）；这里**改用引擎字体
 * 直接画文本** —— 好处是跟着「简中显示层」走，无需再解包，也避免了繁体位图与简中不一致。
 * 见 docs/战略地图格式.md 的取舍说明。 */
static void draw_city_name(S3Strategy *s, Sango3Canvas *cv, int32_t x, int32_t y,
                           int32_t hw, int32_t hh, const char *name,
                           int mine, int sel) {
    if (!s->draw_text || !name || !*name) return;
    (void)hw;                                   /* 名字条宽度固定，与图标宽度无关 */
    const int32_t bw = 58, bh = 16;             /* 底衬与文字框（画布坐标） */
    const int32_t bx = x - bw / 2;
    const int32_t by = y + hh + 2;
    sango3_canvas_fill(cv, bx, by, bw, bh, 10, 12, 20);
    if (sel)              sango3_canvas_frame(cv, bx, by, bw, bh, 1, 255, 120, 120);
    else if (mine)        sango3_canvas_frame(cv, bx, by, bw, bh, 1, 200, 170, 90);
    s->draw_text(s->text_ud, cv, name, bx, by, bw, bh,
                 sel ? 0xFFD0C0u : (mine ? 0xFFE9A8u : 0xE6E6E6u), 0, 0x8u | 0x4u);
}

/* 信息条 + 城池信息面板（地图 / 朝堂两个视图共用） */
static void draw_hud(S3Strategy *s, Sango3Canvas *cv) {
    /* 顶部信息条（下移 10px 避开手机状态栏）。
     * 尺寸随画布自适应：高分辨率画布（1024×768 地图）用大字档 font=3 + 44px 条；
     * 640×480（朝堂）用 16px 字 + 26px 条 —— 否则信息条占掉近 1/10 屏（用户实测）。 */
    const int      small = (cv->w <= 640);
    const int32_t  bar_y = 10, bar_h = small ? 26 : 44;
    const int      bar_f = small ? 1 : 3;
    sango3_canvas_fill(cv, 0, bar_y, cv->w, bar_h, 12, 14, 24);
    sango3_canvas_frame(cv, 0, bar_y + bar_h, cv->w, 2, 1, 150, 130, 80);
    if (s->draw_text) {
        char buf[128];
        if (s->banner[0])
            snprintf(buf, sizeof buf, "%s", s->banner);
        else if (s->is_court)
            snprintf(buf, sizeof buf, "朝堂 · 第 %d 月 —— 左侧行政主選單執行內政，「確定」結束本月",
                     s->month);
        else if (s->sel >= 0 && s->sel < s->n_cities)
            snprintf(buf, sizeof buf, "%s（%s）—— 拖动查看地图 / 点击其它城",
                     s->city[s->sel].name, s->city[s->sel].mine ? "我方" : "他方");
        else
            snprintf(buf, sizeof buf, "战略层：拖动查看地图 · 点击城市（%d 城，我方 %d）",
                     s->n_cities, mine_count(s));
        s->draw_text(s->text_ud, cv, buf, 14, bar_y, cv->w - 28, bar_h, 0xF0DCA0, bar_f, 0x4u);
    }

    /* ---------- 城池信息面板（原版 7400 / CityInfo.shp，标签烘焙在图内）----------
     * 位置：视口右上角（不照搬原版 640×480 的 408,16 —— 我们跑的是地图原生分辨率
     * 全屏 + 视口滚动，见 docs/城池信息面板与行政菜单.md 第一节注）。 */
    if (s->sel >= 0 && s->sel < s->n_cities && s->panel_ok) {
        const StratCity *c = &s->city[s->sel];
        const int32_t z  = s->panel_zoom > 0 ? s->panel_zoom : 1;
        const int   fnt  = (z >= 2) ? 2 : 1;   /* 行高 16×z：z=1 用 16px 字，z=2 用 20px 字 */
        int32_t px = 0, py = 0, pw = 0, ph = 0;
        panel_rect(s, cv->w, cv->h, &px, &py, &pw, &ph);

        sango3_canvas_blit(cv, s->panel.rgba, (int32_t)s->panel.width,
                           (int32_t)s->panel.height, px, py, z);

        if (s->draw_text) {
            /* 行几何取自 Menu.ini：7401..7409 的 Range 相对 7400，
             * 行高 16 / 间距 18，值区 x=35 宽 61（左侧 0..35 是烘焙标签）。 */
            static const int32_t row_y[9] = { 2, 20, 38, 56, 74, 92, 110, 128, 146 };
            const int32_t vx = px + 35 * z;
            const int32_t rw = 61 * z;
            const int32_t rh = 16 * z;
            /* 定稿 F1/F2：非我方且**未在調查有效期内** → 只露 城市/太守，
             * 其余数值一律不显示（否则「調查」这个指令就没有存在意义了）。 */
            const int known = s3_strategy_city_known(s, s->sel);
            char buf[64];
            for (int r = 0; r < 9; ++r) {
                buf[0] = '\0';
                if (!known && r >= 3) {                 /* 金錢/人口/開發/武將/兵士/友好 */
                    snprintf(buf, sizeof buf, "—");
                    s->draw_text(s->text_ud, cv, buf, vx + 4, py + row_y[r] * z, rw - 4, rh,
                                 0x808080u, fnt, 0x4u);
                    continue;
                }
                switch (r) {
                case 0: snprintf(buf, sizeof buf, "%s", c->name); break;
                case 1: snprintf(buf, sizeof buf, "%s", c->has_detail && c->det.lord[0]
                                                       ? c->det.lord : "—"); break;
                case 2: snprintf(buf, sizeof buf, "%s", c->has_detail && c->det.adviser[0]
                                                       ? c->det.adviser : "—"); break;
                case 3: if (c->has_detail) fmt_thousands(c->det.money, buf, sizeof buf); break;
                case 4: if (c->has_detail) fmt_thousands(c->det.people, buf, sizeof buf); break;
                case 5: if (c->has_detail) snprintf(buf, sizeof buf, "%d", c->det.dev); break;
                case 6: if (c->has_detail) snprintf(buf, sizeof buf, "%d", c->det.n_generals); break;
                case 7: if (c->has_detail) snprintf(buf, sizeof buf, "%d", c->det.reserve); break;
                case 8: if (c->has_detail) snprintf(buf, sizeof buf, "%d", c->det.friendliness); break;
                default: break;
                }
                if (!buf[0]) continue;
                /* FColor 5001 = 220,220,220（值文本）；城名用金色以示区分 */
                uint32_t rgb = (r == 0) ? 0xFFD65Au : 0xDCDCDCu;
                s->draw_text(s->text_ud, cv, buf, vx + 4, py + row_y[r] * z, rw - 4, rh,
                             rgb, fnt, 0x4u);
            }
            /* 面板下方一小条状态（面板自身九行已占满，故画在面板外） */
            if (!c->mine) {
                int until = s3_strategy_city_invest_until(s, s->sel);
                snprintf(buf, sizeof buf, known ? "已調查 · 有效至第 %d 月" : "未調查（計略 → 調查）",
                         until);
                const int32_t ny = py + s->panel.height * z + 2;
                sango3_canvas_fill(cv, px, ny, s->panel.width * z, 18 * z, 12, 14, 24);
                s->draw_text(s->text_ud, cv, buf, px + 6, ny, s->panel.width * z - 12, 18 * z,
                             known ? 0x9FE0A0u : (uint32_t)0xD0A060u, fnt, 0x4u);
            }
        }
    }
}

void s3_strategy_on_click(S3Strategy *s, int32_t lx, int32_t ly) {
    if (!s || !s->map_ok || s->is_court) return;   /* 朝堂无地图，不参与城市命中 */
    int32_t mw = s->map.width, mh = s->map.height;
    int32_t cw = s->cv_w > 0 ? s->cv_w : 640;
    int32_t ch = s->cv_h > 0 ? s->cv_h : 480;
    int32_t vw = s->vp_w > 0 ? s->vp_w : mw;
    int32_t vh = s->vp_h > 0 ? s->vp_h : mh;
    int32_t hit = -1, best_d = 0;
    for (int i = 0; i < s->n_cities; ++i) {
        /* 城市中心 → 画布坐标（与 render 同一套视口变换）；命中半径按图标尺寸 + 余量 */
        int32_t ox = (int32_t)((int64_t)(s->city[i].mx - s->vp_x) * cw / vw);
        int32_t oy = (int32_t)((int64_t)(s->city[i].my - s->vp_y) * ch / vh);
        int32_t r  = (int32_t)((int64_t)s->city[i].mw * cw / vw / 2) + 8;
        if (r < 12) r = 12;
        int32_t dx = lx - ox, dy = ly - oy;
        int32_t d = dx * dx + dy * dy;
        if (d <= r * r && (hit < 0 || d < best_d)) { hit = i; best_d = d; }
    }
    /* 点在面板上 → 维持原选择（面板是纯展示层，不参与命中） */
    if (hit < 0) {
        int32_t px = 0, py = 0, pw = 0, ph = 0;
        if (panel_rect(s, cw, ch, &px, &py, &pw, &ph) &&
            lx >= px && lx < px + pw && ly >= py && ly < py + ph) return;
    }
    s->sel = hit;                       /* 点空白 = 取消选择（面板随之收起） */
}

void s3_strategy_pan(S3Strategy *s, int32_t dx, int32_t dy) {
    if (!s) return;
    s->off_x -= dx;      /* 拖动方向与地图移动相反 */
    s->off_y -= dy;
    if (s->off_x < 0) s->off_x = 0;
    if (s->off_y < 0) s->off_y = 0;
}

int s3_strategy_selected(const S3Strategy *s) { return s ? s->sel : -1; }
int s3_strategy_count(const S3Strategy *s) { return s ? s->n_cities : 0; }

const char *s3_strategy_city_name(const S3Strategy *s, int idx) {
    return (s && idx >= 0 && idx < s->n_cities) ? s->city[idx].name : NULL;
}
int s3_strategy_city_mine(const S3Strategy *s, int idx) {
    return (s && idx >= 0 && idx < s->n_cities) ? s->city[idx].mine : 0;
}
const S3CityDetail *s3_strategy_city_detail(const S3Strategy *s, int idx) {
    return (s && idx >= 0 && idx < s->n_cities && s->city[idx].has_detail)
         ? &s->city[idx].det : NULL;
}
S3CityDetail *s3_strategy_city_detail_mut(S3Strategy *s, int idx) {
    return (s && idx >= 0 && idx < s->n_cities && s->city[idx].has_detail)
         ? &s->city[idx].det : NULL;
}

/* ------------------------------- I1：势力旗号（Nation.ini 的 Flag） ------------------------------- */
void s3_strategy_set_city_flag(S3Strategy *s, int idx, int flag) {
    if (!s || idx < 0 || idx >= s->n_cities) return;
    s->city[idx].flag = (flag > 0) ? flag : 0;
}

int s3_strategy_city_flag(const S3Strategy *s, int idx) {
    return (s && idx >= 0 && idx < s->n_cities) ? s->city[idx].flag : 0;
}

/* I2 选君主界面用：换一个君主 → 重算各城"是否我方"（金框/高亮跟着变）。
 * 与 s3_strategy_set_my_lord 的区别：后者只记名字（信息条文案），这里改归属。 */
int s3_strategy_remark_owner(S3Strategy *s, const char *lord) {
    if (!s) return 0;
    int mine = 0;
    for (int i = 0; i < s->n_cities; ++i) {
        const char *l = s->city[i].det.lord;
        int is_mine = (lord && *lord && l[0] && !strcmp(l, lord)) ? 1 : 0;
        /* 城池表里 Lord 与势力君主同名即算它们同势力 —— 与 load_cities 口径一致
         * （有些剧本的城太守不是君主本人，那种城靠 Nation.ini 的 Flag 归色，
         *   归属高亮只认"太守==君主"，保持与开局后一致）。 */
        s->city[i].mine = is_mine;
        if (is_mine) ++mine;
    }
    snprintf(s->my_lord, sizeof s->my_lord, "%s", lord ? lord : "");
    return mine;
}

/* 取该君主的第一座城下标（用作"主城"，面板显示它）-1 = 无 */
int s3_strategy_first_city_of(const S3Strategy *s, const char *lord) {
    if (!s || !lord || !*lord) return -1;
    for (int i = 0; i < s->n_cities; ++i)
        if (s->city[i].mine || !strcmp(s->city[i].det.lord, lord)) return i;
    return -1;
}

/* ------------------------- 調查 / 情報（定稿 F1/F2） ------------------------- */
int s3_strategy_city_known(const S3Strategy *s, int idx) {
    if (!s || idx < 0 || idx >= s->n_cities) return 0;
    if (s->city[idx].mine) return 1;                 /* 我方城池永远可见 */
    if (!s->city[idx].has_detail) return 0;
    const int until = s->city[idx].det.invest_until;
    return (until > 0 && s->month <= until) ? 1 : 0; /* 仍在有效期内 */
}

int s3_strategy_city_investigate(S3Strategy *s, int idx, int until_month) {
    if (!s || idx < 0 || idx >= s->n_cities || !s->city[idx].has_detail) return -1;
    if (s->city[idx].mine) return -1;                /* 我方城无需调查 */
    s->city[idx].det.invest_until = until_month > 0 ? until_month : 0;
    return 0;
}

int s3_strategy_city_invest_until(const S3Strategy *s, int idx) {
    if (!s || idx < 0 || idx >= s->n_cities || !s->city[idx].has_detail) return 0;
    if (s->city[idx].mine) return 0;
    return s->city[idx].det.invest_until;
}
