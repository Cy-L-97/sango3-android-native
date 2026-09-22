/*
 * officer_ui.c —— 武将信息块 / 整备·学技列表实现（见 officer_ui.h）
 */
#include "officer_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- 几何（基准单位）
 * 渲染与命中共用同一套 layout()，避免两处漂移（gen_picker / lord_picker 同做法）。 */
#define PAGE_ROWS     8
#define BASE_ROW_H    22
#define BASE_HEAD_H   26
#define BASE_TITLE_H  32
#define BASE_FOOT_H   34

#define CARD_W        470
#define CARD_LINE_H   28
#define CARD_LINES    5
#define CARD_TOP      12                       /* 标题与首行之间的留白 */

#define LEARN_W       480

#define COL_BG     12, 14, 24
#define COL_FRAME  150, 130, 80
#define COL_TITLE  0xF0DCA0u
#define COL_TEXT   0xDCDCDCu
#define COL_DIM    0x808080u
#define COL_OK     0x7CD47Cu          /* 可学（功勋够） */
#define COL_NO     0xC05050u          /* 功勋不足 */
#define COL_HOVER  0xD25915u

struct S3OfficerUI {
    S3OuiDrawText draw_text; void *text_ud;

    int  mode;                        /* S3OuiMode */
    char title[64];

    /* 卡模式：预先把 5 行文本拼好（原版 9045 的字段顺序） */
    char line[CARD_LINES][128];

    /* 学技模式：直接引用调用方的**已过滤可学技数组**（不复制，生命周期由调用方保证） */
    const S3Magic *const *learn_list;
    int  n;                           /* 可学技总数 */
    int  page, hover, is_sf;
    int  officer_merit;               /* 当前功勋（判断「功勳不足」） */
    char officer[64];                 /* 「張角（等級 5 · 武力 72 · 智力 95）功勳 500」 */
    char note[64];                    /* 表名文案，如「武將技 125 種」 */

    /* 渲染时缓存（命中用同一套） */
    int32_t px, py, pw, ph, z;
};

S3OfficerUI *s3_oui_new(S3OuiDrawText draw_text, void *text_ud) {
    S3OfficerUI *u = (S3OfficerUI *)calloc(1, sizeof *u);
    if (!u) return NULL;
    u->draw_text = draw_text; u->text_ud = text_ud;
    u->z = 1; u->hover = -1;
    return u;
}

void s3_oui_free(S3OfficerUI *u) { free(u); }

void s3_oui_close(S3OfficerUI *u) { if (u) { u->mode = S3_OUI_NONE; u->hover = -1; } }
int  s3_oui_active(const S3OfficerUI *u) { return u ? (u->mode != S3_OUI_NONE) : 0; }
int  s3_oui_mode(const S3OfficerUI *u) { return u ? u->mode : S3_OUI_NONE; }

/* -------------------------------------------------------------- 弹信息块（Text 9045） */
void s3_oui_show_card(S3OfficerUI *u, const S3Officer *o, const char *lord) {
    if (!u || !o) return;
    u->mode = S3_OUI_CARD;
    u->hover = -1;
    const char *ld = (lord && *lord) ? lord : "—";
    const int need = s3_officer_exp_need(o->level);
    snprintf(u->title, sizeof u->title, "武將情報");
    /* 原版 9045 的五行；體力/技力 用「當前/上限」，經驗值用「已累積/本級所需」。
     * ⚠ 原版把 忠誠/士氣/體力/技力 用 %-10s 输出（可能是空串），我们一律显示数值 —— 更有用。 */
    snprintf(u->line[0], sizeof u->line[0], "%s          戰績 %d勝%d敗",
             o->name, o->wins, o->losses);
    snprintf(u->line[1], sizeof u->line[1], "君主 %-8s 武力 %-4d 體力 %d/%d",
             ld, o->str, o->hp, o->hp_max);
    snprintf(u->line[2], sizeof u->line[2], "等級 %-8d 智力 %-4d 技力 %d/%d",
             o->level, o->intel, o->mp, o->mp_max);
    snprintf(u->line[3], sizeof u->line[3], "忠誠 %-8d 士氣 %-4d 帶兵數 %d/%d",
             o->loyalty, 70, o->troops, s3_officer_troop_limit(o));
    snprintf(u->line[4], sizeof u->line[4], "功勳 %-8d 經驗值 %d/%d",
             o->merit, o->exp, need);
}

/* ------------------------------------------------------ 弹学技列表（Text 9040~9044） */
void s3_oui_show_learn(S3OfficerUI *u, const S3Officer *o,
                       const S3Magic *const *list, int n, int is_sf) {
    if (!u || !o) return;
    u->mode = S3_OUI_LEARN;
    u->learn_list = list;
    u->n = (n > 0) ? n : 0;
    u->page = 0; u->hover = -1; u->is_sf = is_sf ? 1 : 0;
    u->officer_merit = o->merit;
    snprintf(u->officer, sizeof u->officer, "%s（等級 %d · 武力 %d · 智力 %d）功勳 %d",
             o->name, o->level, o->str, o->intel, o->merit);
    snprintf(u->note, sizeof u->note, "%s", is_sf ? "軍師技 23 種" : "武將技 125 種");
}

/* 当前页第 r 行的技（越界返回 NULL） */
static const S3Magic *row_magic(const S3OfficerUI *u, int r) {
    const int i = u->page * PAGE_ROWS + r;
    if (!u->learn_list || i < 0 || i >= u->n) return NULL;
    return u->learn_list[i];
}

/* ---------------------------------------------------------------- 布局（唯一出处） */
static void layout(S3OfficerUI *u, Sango3Canvas *cv) {
    u->z = (cv->w <= 640) ? 1 : 2;
    if (u->mode == S3_OUI_CARD) {
        u->pw = CARD_W * u->z;
        u->ph = (BASE_TITLE_H + CARD_TOP + CARD_LINES * CARD_LINE_H + BASE_FOOT_H) * u->z;
    } else {
        u->pw = LEARN_W * u->z;
        u->ph = (BASE_TITLE_H + BASE_HEAD_H + PAGE_ROWS * BASE_ROW_H + BASE_FOOT_H) * u->z;
    }
    u->px = (cv->w - u->pw) / 2;
    u->py = (cv->h - u->ph) / 2;
    if (u->px < 0) u->px = 0;
    if (u->py < 0) u->py = 0;
}

static void card_line_rect(const S3OfficerUI *u, int i, int32_t *x, int32_t *y,
                           int32_t *w, int32_t *h) {
    *x = u->px + 14 * u->z;
    *y = u->py + (BASE_TITLE_H + CARD_TOP + i * CARD_LINE_H) * u->z;
    *w = u->pw - 28 * u->z;
    *h = CARD_LINE_H * u->z;
}

static void learn_row_rect(const S3OfficerUI *u, int i, int32_t *x, int32_t *y,
                           int32_t *w, int32_t *h) {
    *x = u->px + 8 * u->z;
    *y = u->py + (BASE_TITLE_H + BASE_HEAD_H + i * BASE_ROW_H) * u->z;
    *w = u->pw - 16 * u->z;
    *h = BASE_ROW_H * u->z;
}

static void learn_btn_rect(const S3OfficerUI *u, int which, int32_t *x, int32_t *y,
                           int32_t *w, int32_t *h) {
    const int32_t bw = 84 * u->z, bh = 24 * u->z;
    *w = bw; *h = bh;
    *y = u->py + u->ph - (BASE_FOOT_H - 5) * u->z;
    if (which == 0)      *x = u->px + 10 * u->z;                  /* 上頁 */
    else if (which == 1) *x = u->px + 102 * u->z;                 /* 下頁 */
    else                 *x = u->px + u->pw - bw - 10 * u->z;     /* 取消 */
}

/* ---------------------------------------------------------------- 渲染 */
void s3_oui_render(S3OfficerUI *u, Sango3Canvas *cv) {
    if (!u || !cv || u->mode == S3_OUI_NONE) return;
    layout(u, cv);
    const int z = u->z;

    sango3_canvas_fill(cv, u->px, u->py, u->pw, u->ph, COL_BG);
    sango3_canvas_frame(cv, u->px, u->py, u->pw, u->ph, 2 * z, COL_FRAME);

    char buf[192];
    if (u->mode == S3_OUI_CARD) {
        snprintf(buf, sizeof buf, "%s", u->title);
        u->draw_text(u->text_ud, cv, buf, u->px + 12 * z, u->py,
                     u->pw - 24 * z, BASE_TITLE_H * z, COL_TITLE, 1, 0x4u);
        for (int i = 0; i < CARD_LINES; ++i) {
            int32_t lx, ly, lw, lh;
            card_line_rect(u, i, &lx, &ly, &lw, &lh);
            u->draw_text(u->text_ud, cv, u->line[i], lx, ly, lw, lh, COL_TEXT, 1, 0x4u);
        }
        u->draw_text(u->text_ud, cv, "點擊任意處關閉", u->px + 12 * z,
                     u->py + u->ph - BASE_FOOT_H * z, u->pw - 24 * z,
                     BASE_FOOT_H * z, COL_DIM, 1, 0x4u);
        return;
    }

    /* ---- 学技列表 ---- */
    snprintf(buf, sizeof buf, "整備 · 學習%s   %s", u->is_sf ? "軍師技" : "武將技", u->note);
    u->draw_text(u->text_ud, cv, buf, u->px + 10 * z, u->py,
                 u->pw - 20 * z, BASE_TITLE_H * z, COL_TITLE, 1, 0x4u);

    const int32_t hy = u->py + BASE_TITLE_H * z;
    sango3_canvas_fill(cv, u->px + 4 * z, hy, u->pw - 8 * z, BASE_HEAD_H * z, 24, 28, 44);
    u->draw_text(u->text_ud, cv, "名稱", u->px + 14 * z, hy, 120 * z, BASE_HEAD_H * z,
                 COL_TITLE, 1, 0x4u);
    u->draw_text(u->text_ud, cv, "等級", u->px + 150 * z, hy, 60 * z, BASE_HEAD_H * z,
                 COL_TITLE, 1, 0x4u);
    u->draw_text(u->text_ud, cv, "MP", u->px + 212 * z, hy, 60 * z, BASE_HEAD_H * z,
                 COL_TITLE, 1, 0x4u);
    u->draw_text(u->text_ud, cv, "所需功勳", u->px + 268 * z, hy, 110 * z, BASE_HEAD_H * z,
                 COL_TITLE, 1, 0x4u);
    u->draw_text(u->text_ud, cv, "狀態", u->px + 384 * z, hy, 84 * z, BASE_HEAD_H * z,
                 COL_TITLE, 1, 0x4u);

    const int pages = (u->n + PAGE_ROWS - 1) / PAGE_ROWS;
    for (int r = 0; r < PAGE_ROWS; ++r) {
        const S3Magic *m = row_magic(u, r);
        int32_t rx, ry, rw, rh;
        learn_row_rect(u, r, &rx, &ry, &rw, &rh);
        if (!m) continue;
        if (u->hover == r) sango3_canvas_fill(cv, rx + 4 * z, ry, rw - 8 * z, rh, 40, 60, 110);
        const int afford = (u->officer_merit >= m->contribution);
        uint32_t c = afford ? COL_OK : COL_NO;
        if (u->hover == r) c = COL_HOVER;
        snprintf(buf, sizeof buf, "%s", m->name);
        u->draw_text(u->text_ud, cv, buf, rx + 6 * z, ry, 140 * z, rh, c, 1, 0x4u);
        snprintf(buf, sizeof buf, "%d", m->level);
        u->draw_text(u->text_ud, cv, buf, rx + 142 * z, ry, 58 * z, rh, COL_TEXT, 1, 0x4u);
        snprintf(buf, sizeof buf, "%d", m->mp);
        u->draw_text(u->text_ud, cv, buf, rx + 204 * z, ry, 58 * z, rh, COL_TEXT, 1, 0x4u);
        snprintf(buf, sizeof buf, "%d", m->contribution);
        u->draw_text(u->text_ud, cv, buf, rx + 260 * z, ry, 110 * z, rh, c, 1, 0x4u);
        u->draw_text(u->text_ud, cv, afford ? "可學" : "功勳不足",
                     rx + 376 * z, ry, 90 * z, rh, c, 1, 0x4u);
    }

    for (int b = 0; b < 3; ++b) {
        int32_t bx, by, bw, bh;
        learn_btn_rect(u, b, &bx, &by, &bw, &bh);
        sango3_canvas_fill(cv, bx, by, bw, bh, 45, 62, 112);
        const char *t = (b == 0) ? "上頁" : (b == 1) ? "下頁" : "取消";
        u->draw_text(u->text_ud, cv, t, bx, by, bw, bh, COL_TEXT, 1, 0x4u);
    }
    snprintf(buf, sizeof buf, "%s   第 %d/%d 頁   可學 %d 種",
             u->officer, u->page + 1, pages > 0 ? pages : 1, u->n);
    u->draw_text(u->text_ud, cv, buf, u->px + 10 * z,
                 u->py + u->ph - BASE_FOOT_H * z, u->pw - 20 * z,
                 BASE_FOOT_H * z, COL_DIM, 1, 0x4u);
}

/* ---------------------------------------------------------------- 命中 */
void s3_oui_on_move(S3OfficerUI *u, int32_t x, int32_t y) {
    if (!u || u->mode != S3_OUI_LEARN) return;
    u->hover = -1;
    for (int r = 0; r < PAGE_ROWS; ++r) {
        int32_t rx, ry, rw, rh;
        learn_row_rect(u, r, &rx, &ry, &rw, &rh);
        if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) { u->hover = r; return; }
    }
}

int s3_oui_hit(const S3OfficerUI *u, int32_t x, int32_t y) {
    /* 语义 = "面板激活即独占事件"（卡模式下点任意处就是关闭，故不需要按几何判定）。
     * 调用方据此把事件优先给本面板，避免同帧又落到地图/选择器上。 */
    (void)x; (void)y;
    if (!u || u->mode == S3_OUI_NONE) return 0;
    return 1;
}

int s3_oui_on_click(S3OfficerUI *u, int32_t x, int32_t y) {
    if (!u || u->mode == S3_OUI_NONE) return -1;

    if (u->mode == S3_OUI_CARD) {           /* 信息块：任意点击关闭 */
        s3_oui_close(u);
        return -2;
    }

    /* 学技：翻页/取消按钮 */
    for (int b = 0; b < 3; ++b) {
        int32_t bx, by, bw, bh;
        learn_btn_rect(u, b, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            const int pages = (u->n + PAGE_ROWS - 1) / PAGE_ROWS;
            if (b == 0)      { if (u->page > 0) --u->page; }
            else if (b == 1) { if (u->page < pages - 1) ++u->page; }
            else             { s3_oui_close(u); return -2; }
            u->hover = -1;
            return -3;
        }
    }
    /* 列表行 */
    for (int r = 0; r < PAGE_ROWS; ++r) {
        int32_t rx, ry, rw, rh;
        learn_row_rect(u, r, &rx, &ry, &rw, &rh);
        if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) {
            const int i = u->page * PAGE_ROWS + r;
            if (i < u->n) return i;          /* 选中第 i 项（由调用方扣功勋） */
            return -3;
        }
    }
    s3_oui_close(u);                         /* 面板外/空白 → 关闭 */
    return -2;
}
