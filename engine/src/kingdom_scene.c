/*
 * kingdom_scene.c —— 选择君主列表（实现，见 kingdom_scene.h）
 *
 * 自绘表格：君主 | 城池 | 人口 | 金；10 行一页，行列点击选中，底部分页/決定/取消。
 * 数值口径：城池/人口/金钱来自 Setting\City0N.ini 的 [ITEM] 段，同名君主合并累加。
 */
#include "kingdom_scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KD_MAX 64
#define KD_PER_PAGE 9

typedef struct {
    char name[64];
    int  cities, people, money, custom;
} KdLord;

struct S3Kingdom {
    S3KingDrawText draw_text; void *text_ud;
    KdLord  lords[KD_MAX];
    int     n;
    int     page;
    int     sel;          /* 当前高亮下标，-1 = 无 */
    int     result;       /* 0/1/2 */
    int     scenario;     /* 1..7 */
    char    scen_name[64];
};

/* ---- 布局（640×480 安全区）---- */
static const int32_t KD_ROW_Y0  = 130;
static const int32_t KD_ROW_H   = 32;
typedef struct { int32_t x, y, w, h; } KdRect;
static const KdRect R_PREV   = {  60, 430, 90, 34 };
static const KdRect R_NEXT   = { 160, 430, 90, 34 };
static const KdRect R_OK     = { 420, 430, 90, 34 };
static const KdRect R_CANCEL = { 524, 430, 90, 34 };

static int kd_in(const KdRect *r, int32_t x, int32_t y) {
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static void kd_text(S3Kingdom *k, Sango3Canvas *cv, const char *s,
                    int32_t x, int32_t y, int32_t w, int32_t h,
                    uint32_t rgb, int font, int hcenter, int vcenter) {
    if (!k->draw_text) return;
    uint32_t style = 0;
    if (hcenter) style |= 0x8u;   /* S3_WS_HCENTER */
    if (vcenter) style |= 0x4u;   /* S3_WS_VCENTER */
    k->draw_text(k->text_ud, cv, s, x, y, w, h, rgb, font, style);
}

S3Kingdom *s3_kingdom_new(S3KingDrawText draw_text, void *text_ud) {
    S3Kingdom *k = (S3Kingdom *)calloc(1, sizeof *k);
    if (!k) return NULL;
    k->draw_text = draw_text; k->text_ud = text_ud;
    k->sel = -1;
    return k;
}

void s3_kingdom_free(S3Kingdom *k) { free(k); }

void s3_kingdom_begin(S3Kingdom *k, int scenario_id, const char *scenario_name) {
    if (!k) return;
    memset(k->lords, 0, sizeof k->lords);
    k->n = 0; k->page = 0; k->sel = -1; k->result = 0;
    k->scenario = scenario_id;
    snprintf(k->scen_name, sizeof k->scen_name, "%s",
             scenario_name ? scenario_name : "");
}

void s3_kingdom_add_lord(S3Kingdom *k, const char *name,
                         int cities, int people, int money, int custom) {
    if (!k || !name || !*name || k->n >= KD_MAX) return;
    for (int i = 0; i < k->n; ++i) {           /* 同名合并（同一君主多座城） */
        if (strcmp(k->lords[i].name, name) == 0) {
            k->lords[i].cities += cities;
            k->lords[i].people += people;
            k->lords[i].money  += money;
            return;
        }
    }
    snprintf(k->lords[k->n].name, sizeof k->lords[k->n].name, "%s", name);
    k->lords[k->n].cities = cities;
    k->lords[k->n].people = people;
    k->lords[k->n].money  = money;
    k->lords[k->n].custom = custom;
    ++k->n;
}

void s3_kingdom_render(S3Kingdom *k, Sango3Canvas *cv) {
    if (!k || !cv) return;
    sango3_canvas_fill(cv, 20, 60, 600, 400, 16, 14, 26);
    sango3_canvas_frame(cv, 20, 60, 600, 400, 2, 168, 140, 76);

    /* 标题：選擇君主 + 剧本名 */
    kd_text(k, cv, "選擇君主", 220, 66, 200, 32, 0xF2E0B0, 2, 1, 1);
    if (k->scen_name[0])
        kd_text(k, cv, k->scen_name, 440, 70, 160, 26, 0xC8C8C8, 1, 1, 1);

    /* 表头 */
    kd_text(k, cv, "君主",   60, 100, 120, 26, 0xE8D8A8, 1, 0, 1);
    kd_text(k, cv, "城池",  200, 100,  80, 26, 0xE8D8A8, 1, 0, 1);
    kd_text(k, cv, "人口",  300, 100, 120, 26, 0xE8D8A8, 1, 0, 1);
    kd_text(k, cv, "金",    460, 100,  80, 26, 0xE8D8A8, 1, 0, 1);

    int start = k->page * KD_PER_PAGE;
    for (int r = 0; r < KD_PER_PAGE; ++r) {
        int idx = start + r;
        if (idx >= k->n) break;
        int32_t y = KD_ROW_Y0 + r * KD_ROW_H;
        int on = (idx == k->sel);
        if (on) {
            sango3_canvas_fill(cv, 50, y, 540, KD_ROW_H - 2, 58, 48, 30);
            sango3_canvas_frame(cv, 50, y, 540, KD_ROW_H - 2, 1, 214, 178, 94);
        }
        uint32_t col = on ? 0xFFF0C0 : (k->lords[idx].custom ? 0x9FE0A0 : 0xE0E0E0);
        char buf[64];
        snprintf(buf, sizeof buf, "%s%s",
                 k->lords[idx].custom ? "★ " : "", k->lords[idx].name);
        kd_text(k, cv, buf, 60, y, 140, KD_ROW_H, col, 1, 0, 1);

        snprintf(buf, sizeof buf, "%d", k->lords[idx].cities);
        kd_text(k, cv, buf, 200, y, 80, KD_ROW_H, col, 1, 0, 1);
        snprintf(buf, sizeof buf, "%d", k->lords[idx].people);
        kd_text(k, cv, buf, 300, y, 120, KD_ROW_H, col, 1, 0, 1);
        snprintf(buf, sizeof buf, "%d", k->lords[idx].money);
        kd_text(k, cv, buf, 460, y, 80, KD_ROW_H, col, 1, 0, 1);
    }

    /* 分页 / 決定 / 取消 */
    int pages = (k->n + KD_PER_PAGE - 1) / KD_PER_PAGE;
    if (pages < 1) pages = 1;
    char pbuf[32];
    snprintf(pbuf, sizeof pbuf, "◀ 上一页");
    sango3_canvas_fill(cv, R_PREV.x, R_PREV.y, R_PREV.w, R_PREV.h, 28, 26, 40);
    sango3_canvas_frame(cv, R_PREV.x, R_PREV.y, R_PREV.w, R_PREV.h, 1, 120, 116, 108);
    kd_text(k, cv, pbuf, R_PREV.x, R_PREV.y, R_PREV.w, R_PREV.h,
            k->page > 0 ? 0xFFE9A8 : 0x707070, 1, 1, 1);
    sango3_canvas_fill(cv, R_NEXT.x, R_NEXT.y, R_NEXT.w, R_NEXT.h, 28, 26, 40);
    sango3_canvas_frame(cv, R_NEXT.x, R_NEXT.y, R_NEXT.w, R_NEXT.h, 1, 120, 116, 108);
    kd_text(k, cv, "下一页 ▶", R_NEXT.x, R_NEXT.y, R_NEXT.w, R_NEXT.h,
            (k->page + 1 < pages) ? 0xFFE9A8 : 0x707070, 1, 1, 1);

    sango3_canvas_fill(cv, R_OK.x, R_OK.y, R_OK.w, R_OK.h, 28, 26, 40);
    sango3_canvas_frame(cv, R_OK.x, R_OK.y, R_OK.w, R_OK.h, 1, 120, 116, 108);
    kd_text(k, cv, "決定", R_OK.x, R_OK.y, R_OK.w, R_OK.h,
            k->sel >= 0 ? 0xFFE9A8 : 0x707070, 1, 1, 1);
    sango3_canvas_fill(cv, R_CANCEL.x, R_CANCEL.y, R_CANCEL.w, R_CANCEL.h, 28, 26, 40);
    sango3_canvas_frame(cv, R_CANCEL.x, R_CANCEL.y, R_CANCEL.w, R_CANCEL.h, 1, 120, 116, 108);
    kd_text(k, cv, "取消", R_CANCEL.x, R_CANCEL.y, R_CANCEL.w, R_CANCEL.h, 0xC8C8C8, 1, 1, 1);
}

void s3_kingdom_on_click(S3Kingdom *k, int32_t lx, int32_t ly) {
    if (!k) return;
    k->result = 0;

    int start = k->page * KD_PER_PAGE;
    for (int r = 0; r < KD_PER_PAGE; ++r) {
        int idx = start + r;
        if (idx >= k->n) break;
        int32_t y = KD_ROW_Y0 + r * KD_ROW_H;
        if (lx >= 50 && lx < 590 && ly >= y && ly < y + KD_ROW_H - 2) {
            k->sel = idx;
            return;
        }
    }
    int pages = (k->n + KD_PER_PAGE - 1) / KD_PER_PAGE;
    if (pages < 1) pages = 1;
    if (kd_in(&R_PREV, lx, ly)) { if (k->page > 0) --k->page; return; }
    if (kd_in(&R_NEXT, lx, ly)) { if (k->page + 1 < pages) ++k->page; return; }
    if (kd_in(&R_OK, lx, ly)) { if (k->sel >= 0) k->result = 1; return; }
    if (kd_in(&R_CANCEL, lx, ly)) { k->result = 2; return; }
}

int         s3_kingdom_result(const S3Kingdom *k)  { return k ? k->result : 2; }
int         s3_kingdom_selected(const S3Kingdom *k){ return k ? k->sel : -1; }
int         s3_kingdom_count(const S3Kingdom *k)   { return k ? k->n : 0; }
int         s3_kingdom_scenario(const S3Kingdom *k){ return k ? k->scenario : 0; }

const char *s3_kingdom_lord_name(const S3Kingdom *k, int idx) {
    return (k && idx >= 0 && idx < k->n) ? k->lords[idx].name : NULL;
}
int s3_kingdom_lord_cities(const S3Kingdom *k, int idx) {
    return (k && idx >= 0 && idx < k->n) ? k->lords[idx].cities : 0;
}
int s3_kingdom_lord_people(const S3Kingdom *k, int idx) {
    return (k && idx >= 0 && idx < k->n) ? k->lords[idx].people : 0;
}
int s3_kingdom_lord_money(const S3Kingdom *k, int idx) {
    return (k && idx >= 0 && idx < k->n) ? k->lords[idx].money : 0;
}
int s3_kingdom_lord_custom(const S3Kingdom *k, int idx) {
    return (k && idx >= 0 && idx < k->n) ? k->lords[idx].custom : 0;
}
