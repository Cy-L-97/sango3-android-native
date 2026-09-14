/*
 * menu_scene.c —— 控件树运行时 + 场景渲染（实现，见 menu_scene.h）
 */
#include "menu_scene.h"
#include "shp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *s3_menu_state_name(int state) {
    switch (state) {
        case 0: return "normal";
        case 1: return "focus";
        case 2: return "down";
        case 3: return "disable";
        default: return "?";
    }
}

/* 取四态中的某一态 */
static const S3UiState *pick_state(const S3UiIcon *ic, int state) {
    switch (state) {
        case 1: return &ic->focus;
        case 2: return &ic->down;
        case 3: return &ic->disable;
        default: return &ic->normal;
    }
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
            const S3UiState *st = pick_state(ic, ms->state);
            char name[256];
            first_token(st->raw, name, sizeof name);
            if (!is_placeholder(name)) {
                char path[600];
                build_asset_path(path, sizeof path, ic->dir, name);
                uint32_t len = 0;
                uint8_t *raw = ms->read_asset ? ms->read_asset(ms->asset_ud, path, &len) : NULL;
                if (!raw || !len) {
                    if (ms->n_asset_missing < 8)
                        snprintf(ms->log_asset_missing[ms->n_asset_missing], 512, "%s", path);
                    ++ms->n_asset_missing;
                    if (raw) free(raw);
                } else {
                    ShpImage im;
                    const char *err = NULL;
                    if (shp_decode(raw, len, &im, &err)) {
                        int32_t dx = w->range.x + ic->pos_x;
                        int32_t dy = w->range.y + ic->pos_y;
                        sango3_canvas_blit(cv, im.rgba,
                                           (int32_t)im.width, (int32_t)im.height,
                                           dx, dy, 1);
                        ++ms->n_drawn;
                        shp_free(&im);
                    } else {
                        if (ms->n_decode_fail < 8)
                            snprintf(ms->log_decode_fail[ms->n_decode_fail], 512,
                                     "%s (%s)", path, err ? err : "?");
                        ++ms->n_decode_fail;
                    }
                    free(raw);
                }
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
    if (ms->root_id <= 0) ms->root_id = 1;
    draw_window(ms, cv, (uint32_t)ms->root_id, 0);
    return ms->n_drawn;
}
