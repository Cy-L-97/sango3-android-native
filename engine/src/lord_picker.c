/*
 * lord_picker.c —— 選擇君主（大地图版）实现（见 lord_picker.h）
 *
 * 布局全部**按画布尺寸算**（地图视口 1024×556 起，随屏幕宽高比变化），
 * render 与 on_click 共用同一套 layout()，避免两处漂移（同 admin_menu / gen_picker 的做法）。
 */
#include "lord_picker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shp.h"

#define LP_PAGE_ROWS   9        /* 每页行数 */
#define LP_ROW_H       22
#define LP_PANEL_Y     64       /* 让开顶部信息条 */
#define LP_PANEL_W     322      /* 左列表面板宽 */
#define LP_TITLE_H     22       /* 面板标题行 */
#define LP_HEAD_H      20       /* 表头行（在标题之下、数据行之上） */
#define LP_ROWS_Y0     (LP_TITLE_H + LP_HEAD_H + 2)   /* 数据行起点（相对面板顶） */
#define LP_RIGHT_W     240      /* 右侧（肖像）面板宽 */
#define LP_BOTTOM_H    34
#define LP_BTN_W       74
#define LP_BTN_H       24
/* 列 x 偏移（相对面板左）：君主 / 武 / 智 / 忠 / 士
 * ⚠ 2026-09-17 用户裁决（对照清单 C11）：**原版的「忠」就是忠诚度、「士」就是士气**；
 *   **相性（Personality）与义理（Justice）都是隐藏属性，界面不展示**（仅内部用于
 *   招募/离间/招降的判定）。故本表去掉原来的「相」列。
 *   忠诚度是运行时值（数据层无字段）→ 初值取**义理**（实测：呂布 27 / 關羽·夏侯惇 100，
 *   正好对应甲文档"吕布剩 30 忠诚就投降、夏侯惇要等到个位数"的口径）。 */
#define LP_COL_NAME    6        /* 名字列最宽（公孫瓚/夏侯惇 等三字名要放得下） */
#define LP_COL_STR    80
#define LP_COL_INTEL 114
#define LP_COL_JUST  148
#define LP_COL_MORAL 182

#define COL_PANEL   12, 14, 24
#define COL_FRAME   150, 130, 80
#define COL_TITLE   0xF0DCA0u
#define COL_TEXT    0xDCDCDCu
#define COL_DIM     0x8A8A8Au
#define COL_SEL     0xFFD070u
#define COL_MINE    0xA8E0A0u

struct S3LordPick {
    S3KingDrawText   draw_text;   void *text_ud;
    S3LordReadAsset  read_asset;  void *asset_ud;
    const S3Kingdom *k;
    int     page, sel, hover, active;
    /* 底部统计（调用方传入） */
    int       st_cities, st_forts, st_generals;
    long long st_troops, st_people, st_money;
    /* 缓存几何（render 与 hit 共用） */
    int32_t lx, ly, lw, lh;          /* 列表面板 */
    int32_t rx, ry, rw, rh;          /* 右侧面板 */
    int32_t bx, by, bw, bh;          /* 底部统计栏 */
    int32_t ok_x, ok_y, cc_x, cc_y;  /* 決定 / 取消 */
    int     n_rows;
    /* 肖像缓存（按号换） */
    ShpImage face;
    int      face_ok, face_no;
    char     face_path[96];
};

S3LordPick *s3_lordpick_new(S3KingDrawText draw_text, void *text_ud,
                            S3LordReadAsset read_asset, void *asset_ud) {
    S3LordPick *lp = (S3LordPick *)calloc(1, sizeof *lp);
    if (!lp) return NULL;
    lp->draw_text  = draw_text;  lp->text_ud  = text_ud;
    lp->read_asset = read_asset; lp->asset_ud = asset_ud;
    lp->sel = -1; lp->hover = -1; lp->active = 1;
    return lp;
}

void s3_lordpick_free(S3LordPick *lp) {
    if (!lp) return;
    if (lp->face_ok) shp_free(&lp->face);
    free(lp);
}

void s3_lordpick_bind(S3LordPick *lp, const S3Kingdom *k) { if (lp) lp->k = k; }

void s3_lordpick_reset(S3LordPick *lp) {
    if (!lp) return;
    lp->page = 0; lp->hover = -1;
}

void s3_lordpick_set_stats(S3LordPick *lp, int cities, int forts, int generals,
                           long long troops, long long people, long long money) {
    if (!lp) return;
    lp->st_cities = cities; lp->st_forts = forts; lp->st_generals = generals;
    lp->st_troops = troops; lp->st_people = people; lp->st_money = money;
}

int s3_lordpick_page(const S3LordPick *lp)     { return lp ? lp->page : 0; }
int s3_lordpick_selected(const S3LordPick *lp) { return lp ? lp->sel : -1; }
void s3_lordpick_select(S3LordPick *lp, int idx) { if (lp) lp->sel = idx; }

/* ---------------------------------------------------------------- 几何 */
static void layout(S3LordPick *lp, Sango3Canvas *cv) {
    if (!lp || !cv) return;
    const int32_t W = cv->w, H = cv->h;
    const int32_t bottom_h = LP_BOTTOM_H;
    lp->bx = 0; lp->by = H - bottom_h; lp->bw = W; lp->bh = bottom_h;

    int32_t pw = LP_PANEL_W; if (pw > W / 2 - 12) pw = W / 2 - 12;
    lp->lx = 8; lp->ly = LP_PANEL_Y; lp->lw = pw;
    lp->lh = H - LP_PANEL_Y - bottom_h - 8;

    int32_t rwp = LP_RIGHT_W; if (rwp > W / 3) rwp = W / 3;
    lp->rw = rwp; lp->rx = W - rwp - 8; lp->ry = LP_PANEL_Y;
    lp->rh = lp->lh;

    /* 按钮放列表页脚 */
    lp->ok_x = lp->lx + lp->lw - LP_BTN_W - 6;
    lp->ok_y = lp->ly + lp->lh - LP_BTN_H - 6;
    lp->cc_x = lp->ok_x - LP_BTN_W - 6;
    lp->cc_y = lp->ok_y;

    lp->n_rows = 0;
    if (lp->k) {
        int n = s3_kingdom_count(lp->k);
        int rows = (n + LP_PAGE_ROWS - 1) / LP_PAGE_ROWS;
        if (rows < 1) rows = 1;
        if (lp->page >= rows) lp->page = rows - 1;
        if (lp->page < 0) lp->page = 0;
        int left = n - lp->page * LP_PAGE_ROWS;
        lp->n_rows = (left < LP_PAGE_ROWS) ? left : LP_PAGE_ROWS;
    }
}

static void row_rect(const S3LordPick *lp, int r, int32_t *x, int32_t *y,
                     int32_t *w, int32_t *h) {
    *x = lp->lx + 4;
    *y = lp->ly + LP_ROWS_Y0 + r * LP_ROW_H;
    *w = lp->lw - 8;
    *h = LP_ROW_H - 2;
}

/* ------------------------------------------------------------ 肖像加载 */
static int ensure_face(S3LordPick *lp, int no) {
    if (!lp || !lp->read_asset || no <= 0) return 0;
    if (lp->face_ok && lp->face_no == no) return 1;
    if (lp->face_ok) { shp_free(&lp->face); lp->face_ok = 0; }
    snprintf(lp->face_path, sizeof lp->face_path, "Shape\\Portrait\\Portrait%03d.SHP", no);
    uint32_t len = 0;
    uint8_t *raw = lp->read_asset(lp->asset_ud, lp->face_path, &len);
    if (!raw) return 0;
    const char *err = NULL;
    int ok = shp_decode(raw, len, &lp->face, &err);
    free(raw);
    if (!ok) return 0;
    lp->face_ok = 1; lp->face_no = no;
    return 1;
}

/* ------------------------------------------------------------------ 渲染 */
void s3_lordpick_render(S3LordPick *lp, Sango3Canvas *cv) {
    if (!lp || !cv || !lp->active) return;
    layout(lp, cv);
    const int z = (cv->w <= 640) ? 1 : 2;
    const int fnt = (z >= 2) ? 2 : 1;
    char buf[160];

    /* ---- 左侧：君主列表 ---- */
    sango3_canvas_fill(cv, lp->lx, lp->ly, lp->lw, lp->lh, 12, 14, 24);
    sango3_canvas_frame(cv, lp->lx, lp->ly, lp->lw, lp->lh, 2, 150, 130, 80);
    if (lp->draw_text) {
        lp->draw_text(lp->text_ud, cv, "選擇君主", lp->lx + LP_COL_NAME, lp->ly,
                      lp->lw - 16, LP_TITLE_H, COL_TITLE, fnt, 0x4u);

        /* 表头：君主 / 武 / 智 / 忠 / 士（口径见文件头 LP_COL_* 注释） */
        const int32_t hy = lp->ly + LP_TITLE_H;
        struct { const char *t; int x; } H[5] = {
            { "君主", LP_COL_NAME }, { "武", LP_COL_STR }, { "智", LP_COL_INTEL },
            { "忠", LP_COL_JUST },   { "士", LP_COL_MORAL }
        };
        for (int i = 0; i < 5; ++i)
            lp->draw_text(lp->text_ud, cv, H[i].t, lp->lx + H[i].x, hy,
                          40, LP_HEAD_H, 0xB0C4E0u, fnt, 0x4u);

        int n = s3_kingdom_count(lp->k);
        for (int r = 0; r < lp->n_rows; ++r) {
            int idx = lp->page * LP_PAGE_ROWS + r;
            if (idx >= n) break;
            int32_t x, y, w, h;
            row_rect(lp, r, &x, &y, &w, &h);
            if (idx == lp->sel)
                sango3_canvas_fill(cv, x, y, w, h, 58, 48, 30);
            else if (idx == lp->hover)
                sango3_canvas_fill(cv, x, y, w, h, 36, 40, 62);
            uint32_t col = s3_kingdom_lord_custom(lp->k, idx) ? COL_MINE
                         : (idx == lp->sel ? COL_SEL : COL_TEXT);
            lp->draw_text(lp->text_ud, cv, s3_kingdom_lord_name(lp->k, idx),
                          x + LP_COL_NAME, y, LP_COL_STR - LP_COL_NAME - 4, h, col, fnt, 0x4u);
            snprintf(buf, sizeof buf, "%d", s3_kingdom_lord_str(lp->k, idx));
            lp->draw_text(lp->text_ud, cv, buf, x + LP_COL_STR, y, 40, h, col, fnt, 0x4u);
            snprintf(buf, sizeof buf, "%d", s3_kingdom_lord_intel(lp->k, idx));
            lp->draw_text(lp->text_ud, cv, buf, x + LP_COL_INTEL, y, 40, h, col, fnt, 0x4u);
            /* 忠 = **忠诚度**（C11 裁决）；运行时值尚未实现 → 暂以义理(Justice)作初值显示 */
            snprintf(buf, sizeof buf, "%d", s3_kingdom_lord_justice(lp->k, idx));
            lp->draw_text(lp->text_ud, cv, buf, x + LP_COL_JUST, y, 40, h, col, fnt, 0x4u);
            snprintf(buf, sizeof buf, "%d", s3_kingdom_lord_morale(lp->k, idx));
            lp->draw_text(lp->text_ud, cv, buf, x + LP_COL_MORAL, y, 40, h, col, fnt, 0x4u);
        }
        /* 页码 + 提示（放在按钮**上方**，否则被按钮压住 —— 实测重叠） */
        int rows = (n + LP_PAGE_ROWS - 1) / LP_PAGE_ROWS; if (rows < 1) rows = 1;
        snprintf(buf, sizeof buf, "第 %d / %d 頁 · 共 %d 勢力",
                 lp->page + 1, rows, n);
        lp->draw_text(lp->text_ud, cv, buf, lp->lx + 8, lp->ok_y - LP_ROW_H,
                      lp->lw - 16, 18, COL_DIM, fnt, 0x4u);
        /* 決定 / 取消 */
        const struct { int32_t x, y; const char *t; uint32_t c; } B[2] = {
            { lp->cc_x, lp->cc_y, "取消", 0xC8C8C8u },
            { lp->ok_x, lp->ok_y, "決定", lp->sel >= 0 ? 0xFFE9A8u : COL_DIM }
        };
        for (int i = 0; i < 2; ++i) {
            sango3_canvas_fill(cv, B[i].x, B[i].y, LP_BTN_W, LP_BTN_H, 28, 26, 40);
            sango3_canvas_frame(cv, B[i].x, B[i].y, LP_BTN_W, LP_BTN_H, 1, 120, 116, 108);
            lp->draw_text(lp->text_ud, cv, B[i].t, B[i].x, B[i].y, LP_BTN_W, LP_BTN_H,
                          B[i].c, fnt, 0x4u | 0x8u);
        }
    }

    /* ---- 右侧：所选君主的肖像 + 概况 ---- */
    sango3_canvas_fill(cv, lp->rx, lp->ry, lp->rw, lp->rh, 12, 14, 24);
    sango3_canvas_frame(cv, lp->rx, lp->ry, lp->rw, lp->rh, 2, 150, 130, 80);
    if (lp->draw_text) {
        int idx = lp->sel;
        const int32_t iy = lp->ry + 6;
        if (idx < 0) {
            lp->draw_text(lp->text_ud, cv, "（請先選一位君主）", lp->rx + 8, iy,
                          lp->rw - 16, 24, COL_DIM, fnt, 0x4u);
        } else {
            /* 肖像：按整数倍放大贴图（blit 只支持整数 zoom），适配面板宽度 */
            int no = s3_kingdom_lord_portrait(lp->k, idx);
            if (ensure_face(lp, no) && lp->face_ok) {
                const int32_t maxw = lp->rw - 20;
                int32_t z2 = 1;
                while ((int32_t)(lp->face.width * (z2 + 1)) <= maxw && z2 < 4) ++z2;
                const int32_t dw = (int32_t)lp->face.width * z2;
                const int32_t dh = (int32_t)lp->face.height * z2;
                sango3_canvas_blit(cv, lp->face.rgba, (int32_t)lp->face.width,
                                   (int32_t)lp->face.height, lp->rx + 10, iy, z2);
                sango3_canvas_frame(cv, lp->rx + 9, iy - 1, dw + 2, dh + 2, 1, 150, 130, 80);
                int32_t ty = iy + dh + 6;
                snprintf(buf, sizeof buf, "%s", s3_kingdom_lord_name(lp->k, idx));
                lp->draw_text(lp->text_ud, cv, buf, lp->rx + 8, ty, lp->rw - 16, 22,
                              COL_SEL, fnt, 0x4u);
                snprintf(buf, sizeof buf, "武 %d · 智 %d",
                         s3_kingdom_lord_str(lp->k, idx), s3_kingdom_lord_intel(lp->k, idx));
                lp->draw_text(lp->text_ud, cv, buf, lp->rx + 8, ty + 22, lp->rw - 16, 20,
                              COL_TEXT, fnt, 0x4u);
                snprintf(buf, sizeof buf, "忠 %d · 士 %d",
                         s3_kingdom_lord_justice(lp->k, idx), s3_kingdom_lord_morale(lp->k, idx));
                lp->draw_text(lp->text_ud, cv, buf, lp->rx + 8, ty + 44, lp->rw - 16, 20,
                              COL_TEXT, fnt, 0x4u);
                snprintf(buf, sizeof buf, "城 %d · 人口 %d",
                         s3_kingdom_lord_cities(lp->k, idx), s3_kingdom_lord_people(lp->k, idx));
                lp->draw_text(lp->text_ud, cv, buf, lp->rx + 8, ty + 66, lp->rw - 16, 20,
                              COL_TEXT, fnt, 0x4u);
                snprintf(buf, sizeof buf, "金 %d", s3_kingdom_lord_money(lp->k, idx));
                lp->draw_text(lp->text_ud, cv, buf, lp->rx + 8, ty + 88, lp->rw - 16, 20,
                              COL_TEXT, fnt, 0x4u);
            } else {
                lp->draw_text(lp->text_ud, cv, "（無肖像素材）", lp->rx + 8, iy,
                              lp->rw - 16, 24, COL_DIM, fnt, 0x4u);
            }
        }
    }

    /* ---- 底部：统计栏（所属势力的 城數/關口數/武將數/總兵數/人口/金錢） ---- */
    sango3_canvas_fill(cv, lp->bx, lp->by, lp->bw, lp->bh, 12, 14, 24);
    sango3_canvas_frame(cv, lp->bx, lp->by + 0, lp->bw, 2, 1, 150, 130, 80);
    if (lp->draw_text) {
        snprintf(buf, sizeof buf,
                 "城數 %d · 關口數 %d · 武將數 %d · 總兵數 %lld · 人口 %lld · 金錢 %lld",
                 lp->st_cities, lp->st_forts, lp->st_generals,
                 lp->st_troops, lp->st_people, lp->st_money);
        lp->draw_text(lp->text_ud, cv, buf, lp->bx + 10, lp->by, lp->bw - 20, lp->bh,
                      lp->sel >= 0 ? COL_TITLE : COL_DIM, fnt, 0x4u);
    }
}

void s3_lordpick_on_move(S3LordPick *lp, int32_t x, int32_t y) {
    if (!lp || !lp->active) return;
    lp->hover = -1;
    for (int r = 0; r < lp->n_rows; ++r) {
        int32_t rx, ry, rw, rh;
        row_rect(lp, r, &rx, &ry, &rw, &rh);
        if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) { lp->hover = r; return; }
    }
}

int s3_lordpick_on_click(S3LordPick *lp, int32_t x, int32_t y,
                         int *out_lord, int *out_ok, int *out_cancel) {
    if (out_lord) *out_lord = -1;
    if (out_ok) *out_ok = 0;
    if (out_cancel) *out_cancel = 0;
    if (!lp || !lp->active) return 0;

    /* 按鈕 */
    if (x >= lp->ok_x && x < lp->ok_x + LP_BTN_W &&
        y >= lp->ok_y && y < lp->ok_y + LP_BTN_H) {
        if (out_ok) *out_ok = 1;
        return 1;
    }
    if (x >= lp->cc_x && x < lp->cc_x + LP_BTN_W &&
        y >= lp->cc_y && y < lp->cc_y + LP_BTN_H) {
        if (out_cancel) *out_cancel = 1;
        return 1;
    }
    /* 行 = 选中；列表空白处左半翻上页 / 右半翻下页 */
    for (int r = 0; r < lp->n_rows; ++r) {
        int32_t rx, ry, rw, rh;
        row_rect(lp, r, &rx, &ry, &rw, &rh);
        if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) {
            int idx = lp->page * LP_PAGE_ROWS + r;
            if (idx < s3_kingdom_count(lp->k)) {
                lp->sel = idx;
                if (out_lord) *out_lord = idx;
            }
            return 1;
        }
    }
    if (x >= lp->lx && x < lp->lx + lp->lw && y >= lp->ly && y < lp->ly + lp->lh) {
        /* 列表空白处：左半翻上页 / 右半翻下页（页码自行 clamp，不依赖 cv） */
        int n = lp->k ? s3_kingdom_count(lp->k) : 0;
        int rows = (n + LP_PAGE_ROWS - 1) / LP_PAGE_ROWS;
        if (rows < 1) rows = 1;
        if (x < lp->lx + lp->lw / 2) { if (lp->page > 0) --lp->page; }
        else                         { if (lp->page + 1 < rows) ++lp->page; }
        return 1;
    }
    return 0;   /* 面板外：交给地图（可点城看面板） */
}
