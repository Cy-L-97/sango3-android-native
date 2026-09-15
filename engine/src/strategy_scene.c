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
    int32_t mx, my;      /* 地图像素坐标（1024×768 空间） */
    int     mine;
} StratCity;

struct S3Strategy {
    S3StratReadAsset read_asset; void *asset_ud;
    S3StratDrawText  draw_text;  void *text_ud;

    ShpImage map;                /* 地图整图（RGBA，1024×768） */
    int      map_ok;
    char     map_path[128];

    StratCity city[S3_STRAT_MAX_CITIES];
    int       n_cities;
    int       sel;               /* 选中城市下标，-1 = 无 */

    int32_t   off_x, off_y;      /* 平移（逻辑像素，当前版本 0） */
    int32_t   cv_w, cv_h;        /* 最近一次渲染用的画布尺寸（on_click 反算用） */
};

S3Strategy *s3_strategy_new(S3StratReadAsset read_asset, void *asset_ud,
                            S3StratDrawText draw_text, void *text_ud) {
    S3Strategy *s = (S3Strategy *)calloc(1, sizeof *s);
    if (!s) return NULL;
    s->read_asset = read_asset; s->asset_ud = asset_ud;
    s->draw_text = draw_text;   s->text_ud = text_ud;
    s->sel = -1;
    return s;
}

void s3_strategy_free(S3Strategy *s) {
    if (!s) return;
    if (s->map_ok) shp_free(&s->map);
    free(s);
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
    return 0;
}

void s3_strategy_clear_cities(S3Strategy *s) {
    if (s) { s->n_cities = 0; s->sel = -1; }
}

void s3_strategy_add_city(S3Strategy *s, const char *name,
                          int32_t mx, int32_t my, int mine) {
    if (!s || !name || s->n_cities >= S3_STRAT_MAX_CITIES) return;
    StratCity *c = &s->city[s->n_cities++];
    snprintf(c->name, sizeof c->name, "%s", name);
    c->mx = mx; c->my = my; c->mine = mine;
}

/* 地图坐标 → 逻辑坐标（等比例铺满画布） */
static int mine_count(const S3Strategy *s) {
    int n = 0;
    for (int i = 0; i < s->n_cities; ++i) if (s->city[i].mine) ++n;
    return n;
}

static void map_to_logical(const S3Strategy *s, Sango3Canvas *cv,
                           int32_t mx, int32_t my, int32_t *lx, int32_t *ly) {
    int32_t mw = s->map_ok ? s->map.width : 1024;
    int32_t mh = s->map_ok ? s->map.height : 768;
    if (mw <= 0) mw = 1024;
    if (mh <= 0) mh = 768;
    *lx = (int32_t)((int64_t)mx * cv->w / mw) - s->off_x;
    *ly = (int32_t)((int64_t)my * cv->h / mh) - s->off_y;
}

void s3_strategy_render(S3Strategy *s, Sango3Canvas *cv) {
    if (!s || !cv) return;
    s->cv_w = cv->w; s->cv_h = cv->h;
    sango3_canvas_fill(cv, 0, 0, cv->w, cv->h, 10, 12, 20);

    /* 地图整图：最近邻缩放到画布 */
    if (s->map_ok) {
        int32_t mw = s->map.width, mh = s->map.height;
        const uint8_t *src = s->map.rgba;
        for (int32_t y = 0; y < cv->h; ++y) {
            int32_t sy = (int32_t)((int64_t)y * mh / cv->h);
            if (sy < 0 || sy >= mh) continue;
            const uint8_t *srow = src + (size_t)sy * mw * 4;
            uint8_t *drow = cv->px + (size_t)y * cv->w * 4;
            for (int32_t x = 0; x < cv->w; ++x) {
                int32_t sx = (int32_t)((int64_t)x * mw / cv->w);
                if (sx < 0 || sx >= mw) continue;
                const uint8_t *sp = srow + (size_t)sx * 4;
                uint8_t *dp = drow + (size_t)x * 4;
                dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
            }
        }
    }

    /* 城市标记：己方金色方框；选中红框。其余城不额外标记 —— 地图整图自带城池图标。 */
    for (int i = 0; i < s->n_cities; ++i) {
        int sel = (i == s->sel);
        int mine = s->city[i].mine;
        if (!sel && !mine) continue;
        int32_t lx, ly;
        map_to_logical(s, cv, s->city[i].mx, s->city[i].my, &lx, &ly);
        if (sel) {
            sango3_canvas_frame(cv, lx - 8, ly - 8, 16, 16, 2, 255, 60, 60);
            sango3_canvas_frame(cv, lx - 10, ly - 10, 20, 20, 1, 255, 200, 120);
        } else {
            sango3_canvas_frame(cv, lx - 6, ly - 6, 12, 12, 2, 255, 214, 90);
        }
    }

    /* 顶部信息条（下移 10px 避开手机状态栏） */
    sango3_canvas_fill(cv, 0, 10, cv->w, 26, 12, 14, 24);
    sango3_canvas_frame(cv, 0, 35, cv->w, 1, 1, 150, 130, 80);
    if (s->draw_text) {
        char buf[96];
        if (s->sel >= 0 && s->sel < s->n_cities)
            snprintf(buf, sizeof buf, "%s（%s）—— 点击其它城查看",
                     s->city[s->sel].name, s->city[s->sel].mine ? "我方" : "他方");
        else
            snprintf(buf, sizeof buf, "战略层：点击城市查看（%d 城，我方 %d）",
                     s->n_cities, mine_count(s));
        s->draw_text(s->text_ud, cv, buf, 8, 10, cv->w - 16, 26, 0xF0DCA0, 1, 0x4u);
    }
}

void s3_strategy_on_click(S3Strategy *s, int32_t lx, int32_t ly) {
    if (!s || !s->map_ok) return;
    int32_t mw = s->map.width, mh = s->map.height;
    int32_t cw = s->cv_w > 0 ? s->cv_w : 640;
    int32_t ch = s->cv_h > 0 ? s->cv_h : 480;
    int32_t hit = -1, best_d = 0;
    for (int i = 0; i < s->n_cities; ++i) {
        /* 城市在地图坐标 → 画布坐标（与 render 同一套变换）；命中半径放宽到 14px */
        int32_t ox = (int32_t)((int64_t)s->city[i].mx * cw / mw) - s->off_x;
        int32_t oy = (int32_t)((int64_t)s->city[i].my * ch / mh) - s->off_y;
        int32_t dx = lx - ox, dy = ly - oy;
        int32_t d = dx * dx + dy * dy;
        if (d <= 14 * 14 && (hit < 0 || d < best_d)) { hit = i; best_d = d; }
    }
    if (hit >= 0) s->sel = hit;
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
