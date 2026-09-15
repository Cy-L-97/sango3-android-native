/*
 * menu_scene.c —— 控件树运行时 + 场景渲染（实现，见 menu_scene.h）
 */
#include "menu_scene.h"
#include "shp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 缓存条目：资源路径 → 已解码图像（图像内存归缓存所有） */
struct S3MenuCacheEnt {
    char     path[512];
    ShpImage img;
};

const char *s3_menu_state_name(int state) {
    switch (state) {
        case 0: return "normal";
        case 1: return "focus";
        case 2: return "down";
        case 3: return "disable";
        default: return "?";
    }
}

/* 取四态中的某一态。
 * 原版语义：某态值为 "Normal" 表示**沿用 normal 态素材**（不是"无素材"），
 * 因此非 normal 态遇到 "Normal" 必须回退到 normal —— 否则背景/按钮会整块消失。
 * 真正的"无素材"写作 "Null"。 */
static const S3UiState *pick_state(const S3UiIcon *ic, int state) {
    const S3UiState *st;
    switch (state) {
        case 1: st = &ic->focus;   break;
        case 2: st = &ic->down;    break;
        case 3: st = &ic->disable; break;
        default: st = &ic->normal; break;
    }
    if (state != 0 && st->raw && strcmp(st->raw, "Normal") == 0)
        return &ic->normal;
    return st;
}

/* 素材名可能形如 "LogoFire01,"（尾部逗号表示序列），只取第一段并去空白。 */
static void first_token(const char *raw, char *out, size_t cap) {
    out[0] = '\0';
    if (!raw || cap == 0) return;
    size_t n = 0;
    while (raw[n] && raw[n] != ',' && n + 1 < cap) { out[n] = raw[n]; ++n; }
    out[n] = '\0';
    size_t a = 0, b = n;
    while (a < b && (out[a] == ' ' || out[a] == '\t')) ++a;
    while (b > a && (out[b - 1] == ' ' || out[b - 1] == '\t')) --b;
    if (a > 0) memmove(out, out + a, b - a);
    out[b - a] = '\0';
}

/* 占位素材名：原版用 "Normal"/"Null"/"None" 表示"无独立素材/沿用"。 */
static int is_placeholder(const char *name) {
    if (!name || !*name) return 1;
    if (strcmp(name, "Normal") == 0) return 1;
    if (strcmp(name, "Null")   == 0) return 1;
    if (strcmp(name, "None")   == 0) return 1;
    return 0;
}

/* 拼素材路径：`Shape` + Dir + Name(+.shp)，容忍 Dir 首尾反斜杠的有无。 */
static void build_asset_path(char *out, size_t cap, const char *dir, const char *name) {
    char buf[600];
    size_t n = 0;
    buf[0] = '\0';
    n += (size_t)snprintf(buf + n, sizeof buf - n, "Shape");
    if (dir && *dir) {
        if (dir[0] != '\\' && dir[0] != '/')
            n += (size_t)snprintf(buf + n, sizeof buf - n, "\\");
        n += (size_t)snprintf(buf + n, sizeof buf - n, "%s", dir);
        if (n && buf[n - 1] != '\\' && buf[n - 1] != '/')
            n += (size_t)snprintf(buf + n, sizeof buf - n, "\\");
    } else {
        n += (size_t)snprintf(buf + n, sizeof buf - n, "\\");
    }
    n += (size_t)snprintf(buf + n, sizeof buf - n, "%s", name);
    if (!strchr(name, '.'))
        n += (size_t)snprintf(buf + n, sizeof buf - n, ".shp");
    snprintf(out, cap, "%s", buf);
}

/* ---------------------------------------------------------------- 缓存 */
static const ShpImage *cache_find(S3MenuScene *ms, const char *path) {
    for (int i = 0; i < ms->n_cache; ++i)
        if (strcmp(ms->cache[i]->path, path) == 0) return &ms->cache[i]->img;
    return NULL;
}

static void log_missing(S3MenuScene *ms, const char *path) {
    if (ms->n_asset_missing < 8)
        snprintf(ms->log_asset_missing[ms->n_asset_missing], 512, "%s", path);
    ++ms->n_asset_missing;
}

static void log_decode(S3MenuScene *ms, const char *path, const char *err) {
    if (ms->n_decode_fail < 8)
        snprintf(ms->log_decode_fail[ms->n_decode_fail], 512, "%s (%s)", path, err ? err : "?");
    ++ms->n_decode_fail;
}

/* 取素材（带缓存）并贴到 (dx,dy)。
 * 返回 1=已绘制，-1=资源不存在，-2=解码失败。 */
static int draw_asset(S3MenuScene *ms, Sango3Canvas *cv, const char *path,
                      int32_t dx, int32_t dy) {
    const ShpImage *hit = cache_find(ms, path);
    if (hit) {
        sango3_canvas_blit(cv, hit->rgba, (int32_t)hit->width, (int32_t)hit->height, dx, dy, 1);
        return 1;
    }

    uint32_t len = 0;
    uint8_t *raw = ms->read_asset ? ms->read_asset(ms->asset_ud, path, &len) : NULL;
    if (!raw || !len) {
        log_missing(ms, path);
        if (raw) free(raw);
        return -1;
    }

    /* 优先入缓存（大素材每帧重解码会拖慢帧率） */
    if (ms->n_cache < S3_MENU_CACHE_MAX) {
        S3MenuCacheEnt *e = (S3MenuCacheEnt *)calloc(1, sizeof *e);
        if (e) {
            const char *err = NULL;
            if (shp_decode(raw, len, &e->img, &err)) {
                snprintf(e->path, sizeof e->path, "%s", path);
                ms->cache[ms->n_cache++] = e;
                free(raw);
                sango3_canvas_blit(cv, e->img.rgba,
                                   (int32_t)e->img.width, (int32_t)e->img.height, dx, dy, 1);
                return 1;
            }
            free(e);
            log_decode(ms, path, err);
            free(raw);
            return -2;
        }
    }

    /* 缓存不可用（满 / 分配失败）→ 临时解码后释放 */
    {
        ShpImage tmp;
        const char *err = NULL;
        if (!shp_decode(raw, len, &tmp, &err)) {
            log_decode(ms, path, err);
            free(raw);
            return -2;
        }
        free(raw);
        sango3_canvas_blit(cv, tmp.rgba, (int32_t)tmp.width, (int32_t)tmp.height, dx, dy, 1);
        shp_free(&tmp);
        return 1;
    }
}

/* ---------------------------------------------------------------- 渲染 */
static void draw_window(S3MenuScene *ms, Sango3Canvas *cv, uint32_t id, int depth) {
    if (depth > 16) return;
    const S3UiWindow *w = s3_ui_window(ms->layout, id);
    if (!w) return;

    int has_icon = ((w->style & S3_WS_ICON) && w->icon_id >= 0);
    int has_text = ((w->style & S3_WS_TEXT) && w->title && *w->title);
    if (!has_icon && !has_text) ++ms->n_containers;

    if (has_icon) {
        const S3UiIcon *ic = s3_ui_icon(ms->layout, (uint32_t)w->icon_id);
        if (!ic) {
            ++ms->n_icon_missing;
        } else {
            int st = ms->state_of ? ms->state_of(ms->state_ud, w->id) : ms->state;
            if (st < 0 || st > 3) st = 0;
            const S3UiState *state = pick_state(ic, st);
            char name[256];
            first_token(state->raw, name, sizeof name);
            if (!is_placeholder(name)) {
                char path[600];
                build_asset_path(path, sizeof path, ic->dir, name);
                if (draw_asset(ms, cv, path,
                               w->range.x + ic->pos_x, w->range.y + ic->pos_y) == 1)
                    ++ms->n_drawn;
            }
        }
    }

    if (has_text && ms->draw_text) {
        uint32_t rgb = 0xFFFFFF;
        if (w->fcolor >= 0) {
            const S3UiColor *co = s3_ui_color(ms->layout, (uint32_t)w->fcolor);
            if (co && co->normal.n_seg >= 3) {
                rgb = ((uint32_t)co->normal.v[0] << 16)
                    | ((uint32_t)co->normal.v[1] << 8)
                    | ((uint32_t)co->normal.v[2]);
            }
        }
        ms->draw_text(ms->text_ud, cv, w->title,
                      w->range.x, w->range.y, w->range.w, w->range.h,
                      rgb, w->font, w->style);
        ++ms->n_text;
    }

    for (uint32_t k = 0; k < w->child_count; ++k)
        draw_window(ms, cv, w->child_ids[k], depth + 1);
}

int32_t s3_menu_render(S3MenuScene *ms, Sango3Canvas *cv) {
    if (!ms) return 0;
    ms->n_drawn = ms->n_icon_missing = ms->n_asset_missing = 0;
    ms->n_decode_fail = ms->n_text = ms->n_containers = 0;
    if (!ms->layout || !cv) return 0;
    /* 每次渲染 = 一整帧：先清空画布。
     * 场景往往不是全屏覆盖（如登錄武將只有面板 + 列表框架），交互主程序又复用
     * 同一块画布 —— 不清屏的话，上一个场景的像素会全部残留（2026-09-15 实测踩坑）。 */
    sango3_canvas_fill(cv, 0, 0, cv->w, cv->h, 0, 0, 0);
    if (ms->roots && ms->n_roots > 0) {
        for (int i = 0; i < ms->n_roots; ++i)      /* 数组顺序 = 叠加顺序 */
            draw_window(ms, cv, ms->roots[i], 0);
    } else {
        if (ms->root_id <= 0) ms->root_id = 1;
        draw_window(ms, cv, (uint32_t)ms->root_id, 0);
    }
    return ms->n_drawn;
}

/* ---------------------------------------------------------------- 命中测试 */
static uint32_t hit_rec(const S3UiLayout *L, uint32_t id, int32_t lx, int32_t ly) {
    const S3UiWindow *w = s3_ui_window(L, id);
    if (!w) return 0;
    /* 子在上（后画的优先），从后往前测 */
    for (int k = (int)w->child_count - 1; k >= 0; --k) {
        uint32_t h = hit_rec(L, w->child_ids[k], lx, ly);
        if (h) return h;
    }
    if (w->range.w > 0 && w->range.h > 0 &&
        lx >= w->range.x && lx < w->range.x + w->range.w &&
        ly >= w->range.y && ly < w->range.y + w->range.h)
        return w->id;
    return 0;
}

uint32_t s3_menu_hit_test(const S3UiLayout *L, uint32_t root_id,
                          int32_t lx, int32_t ly) {
    if (!L) return 0;
    if (root_id == 0) root_id = 1;
    return hit_rec(L, root_id, lx, ly);
}

uint32_t s3_menu_hit_test_multi(const S3UiLayout *L,
                                const uint32_t *roots, int n_roots,
                                int32_t lx, int32_t ly) {
    if (!L || !roots) return 0;
    for (int i = n_roots - 1; i >= 0; --i) {   /* 后面的窗口在上，优先命中 */
        uint32_t h = hit_rec(L, roots[i], lx, ly);
        if (h) return h;
    }
    return 0;
}

void s3_menu_scene_release(S3MenuScene *ms) {
    if (!ms) return;
    for (int i = 0; i < ms->n_cache; ++i) {
        shp_free(&ms->cache[i]->img);
        free(ms->cache[i]);
        ms->cache[i] = NULL;
    }
    ms->n_cache = 0;
}
