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
    /* 视口（地图坐标）：画布只显示这一块，拖动改变 vp_x/vp_y */
    int32_t   vp_x, vp_y, vp_w, vp_h;
    int       vp_set;
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

void s3_strategy_render(S3Strategy *s, Sango3Canvas *cv) {
    if (!s || !cv) return;
    s->cv_w = cv->w; s->cv_h = cv->h;
    sango3_canvas_fill(cv, 0, 0, cv->w, cv->h, 10, 12, 20);

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
     * 框大小按该城图标尺寸等比换算（大城/中城/小城/关卡尺寸不同）。 */
    int32_t vw = s->vp_w > 0 ? s->vp_w : (s->map_ok ? s->map.width : 1024);
    int32_t vh = s->vp_h > 0 ? s->vp_h : (s->map_ok ? s->map.height : 768);
    for (int i = 0; i < s->n_cities; ++i) {
        int sel = (i == s->sel);
        int mine = s->city[i].mine;
        if (!sel && !mine) continue;
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
        } else {
            sango3_canvas_frame(cv, lx - hw, ly - hh, hw * 2, hh * 2, 2, 255, 214, 90);
        }
    }

    /* 顶部信息条（下移 10px 避开手机状态栏）。
     * 高分辨率画布（1024×768）下用大字档 font=3，否则相对屏幕偏小。 */
    const int32_t bar_y = 10, bar_h = 44;
    sango3_canvas_fill(cv, 0, bar_y, cv->w, bar_h, 12, 14, 24);
    sango3_canvas_frame(cv, 0, bar_y + bar_h, cv->w, 2, 1, 150, 130, 80);
    if (s->draw_text) {
        char buf[96];
        if (s->sel >= 0 && s->sel < s->n_cities)
            snprintf(buf, sizeof buf, "%s（%s）—— 拖动查看地图 / 点击其它城",
                     s->city[s->sel].name, s->city[s->sel].mine ? "我方" : "他方");
        else
            snprintf(buf, sizeof buf, "战略层：拖动查看地图 · 点击城市（%d 城，我方 %d）",
                     s->n_cities, mine_count(s));
        s->draw_text(s->text_ud, cv, buf, 14, bar_y, cv->w - 28, bar_h, 0xF0DCA0, 3, 0x4u);
    }
}

void s3_strategy_on_click(S3Strategy *s, int32_t lx, int32_t ly) {
    if (!s || !s->map_ok) return;
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
