/*
 * gen_picker.c —— 武将选择界面实现（见 gen_picker.h）
 */
#include "gen_picker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_ROWS     8        /* 每页行数 */
#define BASE_W        380      /* 基准面板宽（逻辑单位，渲染时 ×z） */
#define BASE_ROW_H    22
#define BASE_HEAD_H   26
#define BASE_TITLE_H  32
#define BASE_FOOT_H   34
/* 列基准 x 偏移（相对面板左） */
#define COL_NAME   10
#define COL_STR    150
#define COL_INTEL  200
#define COL_LEVEL  250
#define COL_STATE  300

#define COL_BG     12, 14, 24
#define COL_FRAME  150, 130, 80
#define COL_TITLE  0xF0DCA0u
#define COL_TEXT   0xDCDCDCu
#define COL_DIM    0x808080u    /* 已行动 → 置灰 */
#define COL_HOVER  0xD25915u

struct S3GenPicker {
    S3PickDrawText draw_text; void *text_ud;
    const S3Roster *roster;
    char   city[S3_CITY_NAME_CAP];
    char   title[48];
    int    only_idle, active, page, hover;
    /* 渲染时缓存的几何（命中测试与渲染同一套算法，避免两处漂移） */
    int32_t px, py, pw, ph, z;
    int     idx[PAGE_ROWS];      /* 当前页各行对应的 roster 下标，-1 = 空行 */
    int     n_rows;              /* 当前页有效行数 */
    int32_t n_total, n_pages;
};

S3GenPicker *s3_picker_new(S3PickDrawText draw_text, void *text_ud) {
    S3GenPicker *p = (S3GenPicker *)calloc(1, sizeof *p);
    if (!p) return NULL;
    p->draw_text = draw_text; p->text_ud = text_ud;
    p->z = 1;
    return p;
}

void s3_picker_free(S3GenPicker *p) { free(p); }

void s3_picker_open(S3GenPicker *p, const S3Roster *roster, const char *city,
                    const char *title, int only_idle) {
    if (!p) return;
    p->roster = roster;
    snprintf(p->city, sizeof p->city, "%s", city ? city : "");
    snprintf(p->title, sizeof p->title, "%s", title ? title : "");
    p->only_idle = only_idle ? 1 : 0;
    p->page = 0;
    p->hover = -1;
    p->active = 1;
}

void s3_picker_close(S3GenPicker *p) { if (p) { p->active = 0; p->hover = -1; } }
int  s3_picker_active(const S3GenPicker *p) { return p ? p->active : 0; }
const char *s3_picker_city(const S3GenPicker *p) { return p ? p->city : ""; }

/* 取该城可执行者列表（roster 下标） */
static int list_workers(const S3GenPicker *p, int *out, int out_max) {
    if (!p || !p->roster) return 0;
    return s3_roster_workers(p->roster, p->city, p->only_idle, out, out_max);
}

/* 该城"本月未行动"的执行者数（提示用；与列表是否过滤无关） */
static int idle_workers(const S3GenPicker *p) {
    int all[S3_ROSTER_MAX];
    if (!p || !p->roster) return 0;
    return s3_roster_workers(p->roster, p->city, 1, all, S3_ROSTER_MAX);
}

/* 计算并缓存面板几何 + 当前页内容（render 与 hit 共用） */
static void layout(S3GenPicker *p, Sango3Canvas *cv) {
    p->z  = (cv->w <= 640) ? 1 : 2;
    int all[S3_ROSTER_MAX];
    p->n_total = list_workers(p, all, S3_ROSTER_MAX);
    p->n_pages = (p->n_total + PAGE_ROWS - 1) / PAGE_ROWS;
    if (p->n_pages < 1) p->n_pages = 1;
    if (p->page < 0) p->page = 0;
    if (p->page >= p->n_pages) p->page = p->n_pages - 1;

    p->n_rows = 0;
    for (int i = 0; i < PAGE_ROWS; ++i) {
        int k = p->page * PAGE_ROWS + i;
        p->idx[i] = (k < p->n_total) ? all[k] : -1;
        if (p->idx[i] >= 0) ++p->n_rows;
    }
    p->pw = BASE_W * p->z;
    p->ph = (BASE_TITLE_H + BASE_HEAD_H + PAGE_ROWS * BASE_ROW_H + BASE_FOOT_H) * p->z;
    p->px = (cv->w - p->pw) / 2;
    p->py = (cv->h - p->ph) / 2;
    if (p->px < 0) p->px = 0;
    if (p->py < 0) p->py = 0;
}

static void row_rect(const S3GenPicker *p, int i, int32_t *x, int32_t *y,
                     int32_t *w, int32_t *h) {
    *x = p->px;
    *y = p->py + (BASE_TITLE_H + BASE_HEAD_H + i * BASE_ROW_H) * p->z;
    *w = p->pw;
    *h = BASE_ROW_H * p->z;
}

static void btn_rect(const S3GenPicker *p, int which, int32_t *x, int32_t *y,
                     int32_t *w, int32_t *h) {
    const int32_t bw = 84 * p->z, bh = 24 * p->z;
    *y = p->py + p->ph - (BASE_FOOT_H - 5) * p->z;
    *w = bw; *h = bh;
    if (which == 0)      *x = p->px + 10 * p->z;                 /* 上頁 */
    else if (which == 1) *x = p->px + 102 * p->z;                /* 下頁 */
    else                 *x = p->px + p->pw - bw - 10 * p->z;    /* 取消 */
}

void s3_picker_render(S3GenPicker *p, Sango3Canvas *cv) {
    if (!p || !cv || !p->active) return;
    layout(p, cv);
    const int z = p->z;

    sango3_canvas_fill(cv, p->px, p->py, p->pw, p->ph, 12, 14, 24);
    sango3_canvas_frame(cv, p->px, p->py, p->pw, p->ph, 2 * z, 150, 130, 80);
    if (!p->draw_text) return;

    const int fnt = (z >= 2) ? 2 : 1;
    char buf[128];

    /* 标题：人数放这里（原先塞在页脚，文字会溢出到「取消」按钮下面 —— 实测显示重叠） */
    snprintf(buf, sizeof buf, "%s —— 選擇執行者（%s · 共 %d 人）",
             p->title, p->city, (int)p->n_total);
    p->draw_text(p->text_ud, cv, buf, p->px + 10 * z, p->py,
                 p->pw - 20 * z, BASE_TITLE_H * z, COL_TITLE, fnt, 0x4u);

    /* 表头 */
    const int32_t hy = p->py + BASE_TITLE_H * z;
    sango3_canvas_fill(cv, p->px + 4 * z, hy, p->pw - 8 * z, BASE_HEAD_H * z, 24, 28, 44);
    struct { const char *t; int x; } H[5] = {
        { "姓名", COL_NAME }, { "武力", COL_STR }, { "智力", COL_INTEL },
        { "等級", COL_LEVEL }, { "狀態", COL_STATE }
    };
    for (int i = 0; i < 5; ++i)
        p->draw_text(p->text_ud, cv, H[i].t, p->px + H[i].x * z, hy,
                     70 * z, BASE_HEAD_H * z, 0xB0C4E0u, fnt, 0x4u);

    /* 数据行 */
    for (int i = 0; i < PAGE_ROWS; ++i) {
        int32_t rx, ry, rw, rh;
        row_rect(p, i, &rx, &ry, &rw, &rh);
        if (p->idx[i] < 0) continue;
        const S3Officer *o = s3_roster_at(p->roster, p->idx[i]);
        if (!o) continue;
        /* 定稿 A2：**本月已行动者置灰、不可选**（不是从列表里隐藏 —— 隐藏会让玩家
         * 看不出"这人为什么不在"，2026-09-17 按定稿对齐）。
         * only_idle 仍保留"只列未行动者"的过滤选项（守城支援等场合可能要用）。 */
        int selectable = !o->acted;
        if (p->hover == i && selectable)
            sango3_canvas_fill(cv, rx + 4 * z, ry, rw - 8 * z, rh, 40, 60, 110);
        uint32_t rgb = selectable ? COL_TEXT : COL_DIM;
        if (p->hover == i && selectable) rgb = COL_HOVER;

        p->draw_text(p->text_ud, cv, o->name, rx + COL_NAME * z, ry, 130 * z, rh, rgb, fnt, 0x4u);
        snprintf(buf, sizeof buf, "%d", o->str);
        p->draw_text(p->text_ud, cv, buf, rx + COL_STR * z, ry, 46 * z, rh, rgb, fnt, 0x4u);
        snprintf(buf, sizeof buf, "%d", o->intel);
        p->draw_text(p->text_ud, cv, buf, rx + COL_INTEL * z, ry, 46 * z, rh, rgb, fnt, 0x4u);
        snprintf(buf, sizeof buf, "%d", o->level);
        p->draw_text(p->text_ud, cv, buf, rx + COL_LEVEL * z, ry, 46 * z, rh, rgb, fnt, 0x4u);
        p->draw_text(p->text_ud, cv, o->acted ? "本月已行動" : "可執行",
                     rx + COL_STATE * z, ry, 76 * z, rh, rgb, fnt, 0x4u);
    }
    if (p->n_total == 0) {
        p->draw_text(p->text_ud, cv, "此城暫無可執行指令的武將/軍師（或本月均已行動）",
                     p->px + 12 * z, p->py + (BASE_TITLE_H + BASE_HEAD_H) * z,
                     p->pw - 24 * z, BASE_ROW_H * z, COL_DIM, fnt, 0x4u);
    } else if (idle_workers(p) == 0) {
        /* 有武将但本月全部已行动：明确告知，避免看起来像"界面坏了" */
        p->draw_text(p->text_ud, cv, "本月武將/軍師均已行動 —— 請下月再來（或長按返回）",
                     p->px + 12 * z, p->py + (BASE_TITLE_H + BASE_HEAD_H) * z,
                     p->pw - 24 * z, BASE_ROW_H * z, 0xD0A060u, fnt, 0x4u);
    }

    /* 页脚：上頁 / 下頁 / 取消 + 页码 */
    const char *B[3] = { "上頁", "下頁", "取消" };
    for (int i = 0; i < 3; ++i) {
        int32_t bx, by, bw, bh;
        btn_rect(p, i, &bx, &by, &bw, &bh);
        int dim = (i == 0 && p->page <= 0) || (i == 1 && p->page >= p->n_pages - 1);
        sango3_canvas_fill(cv, bx, by, bw, bh, dim ? 26 : 45, dim ? 30 : 62, dim ? 46 : 112);
        sango3_canvas_frame(cv, bx, by, bw, bh, 1, 150, 130, 80);
        p->draw_text(p->text_ud, cv, B[i], bx, by, bw, bh,
                     dim ? COL_DIM : COL_TEXT, fnt, 0x4u | 0x8u /* 居中 */);
    }
    /* 页脚：只留页码（人数已移到标题；图例由"狀態"列的"可執行/本月已行動"自明） */
    snprintf(buf, sizeof buf, "第 %d / %d 頁", p->page + 1, p->n_pages);
    p->draw_text(p->text_ud, cv, buf, p->px + 210 * z, p->py + p->ph - BASE_FOOT_H * z,
                 140 * z, BASE_FOOT_H * z, 0x9AA4B8u, fnt, 0x4u);
}

int s3_picker_hit(const S3GenPicker *p, int32_t x, int32_t y) {
    if (!p || !p->active) return 0;
    return (x >= p->px && x < p->px + p->pw && y >= p->py && y < p->py + p->ph);
}

void s3_picker_on_move(S3GenPicker *p, int32_t x, int32_t y) {
    if (!p || !p->active) return;
    p->hover = -1;
    for (int i = 0; i < PAGE_ROWS; ++i) {
        if (p->idx[i] < 0) continue;
        int32_t rx, ry, rw, rh;
        row_rect(p, i, &rx, &ry, &rw, &rh);
        if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) { p->hover = i; return; }
    }
}

int s3_picker_on_click(S3GenPicker *p, int32_t x, int32_t y,
                       int *out_idx, int *out_cancel) {
    if (out_idx) *out_idx = -1;
    if (out_cancel) *out_cancel = 0;
    if (!p || !p->active) return 0;

    for (int i = 0; i < 3; ++i) {
        int32_t bx, by, bw, bh;
        btn_rect(p, i, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            if (i == 0) { if (p->page > 0) --p->page; }
            else if (i == 1) { if (p->page < p->n_pages - 1) ++p->page; }
            else { if (out_cancel) *out_cancel = 1; s3_picker_close(p); }
            return 1;
        }
    }
    for (int i = 0; i < PAGE_ROWS; ++i) {
        if (p->idx[i] < 0) continue;
        int32_t rx, ry, rw, rh;
        row_rect(p, i, &rx, &ry, &rw, &rh);
        if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) {
            const S3Officer *o = s3_roster_at(p->roster, p->idx[i]);
            if (!o) return 1;
            if (o->acted) return 1;                   /* 定稿 A2：已行动 → 不可选 */
            if (out_idx) *out_idx = p->idx[i];
            s3_picker_close(p);
            return 1;
        }
    }
    return 1;   /* 点在面板内 = 消费 */
}
