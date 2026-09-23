/*
 * officer_ui.c —— 整备界面（ARRAY）/ 武将信息块实现（见 officer_ui.h）
 *
 * 坐标一律用**原版 640×480 空间**（与 ui.json 的 rect 一一对应，便于日后逐像素核对）。
 */
#include "officer_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------- 原版布局（绝对坐标，640×480） */
#define AR_W 640
#define AR_H 480

#define PT_X   6
#define PT_Y   6
#define PT_W   100
#define PT_H   120

#define IF_X   119          /* 資訊欄 9100 */
#define IF_Y   6
#define IF_W   515
#define IF_H   120
#define NAV_Y  8            /* 9101/9102 相对 9100 的 [461,2]/[487,2] */
#define NAV_X1 580
#define NAV_X2 606
#define NAV_W  26
#define NAV_H  18

#define IT_X   5            /* 物品欄 9200 */
#define IT_Y   169
#define IT_W   110
#define IT_H   306
#define SLOT_X 29           /* 9201/9202/9203 = 9200 + [24,19|115|211] */
#define SLOT_W 58
#define SLOT_H 58
#define SLOT_Y0 188
#define SLOT_Y1 284
#define SLOT_Y2 380
#define NOTE_Y0 247         /* 9221/9222/9223 = 9200 + [24,78|174|270] */
#define NOTE_Y1 343
#define NOTE_Y2 439
#define NOTE_W 58
#define NOTE_H 30

#define OPT_X  123          /* 選項 9300 */
#define OPT_Y  134
#define OPT_W  121
#define OPT_H  161
#define OPT_IH 33           /* 9301~9305 相对 [0, 0|33|66|99|132, 121, 29] */
#define OPT_BH 29

#define SUB_X  251          /* 9311~9319 子選單 */
#define SUB_Y  139
#define SUB_W  103
#define SUB_ROW 26          /* H = 17 + 26*项数（1 项 43 … 9 项 251） */

#define SQ_X   401          /* 9400 陣形（8 小队） */
#define SQ_Y   139
#define SQ_W   233
#define SQ_H   335
#define SQ_CW  75
#define SQ_CH  60
#define SQ_CX0 10
#define SQ_CX1 100
#define SQ_CY0 10
#define SQ_CY1 80
#define SQ_CY2 150
#define SQ_CY3 220

#define BIG_X  274          /* 9500 學技大列表（原版自右滑入；我们直接放可视区） */
#define BIG_Y  139
#define BIG_W  366
#define BIG_H  298
#define BIG_ROW 24

#define MSG_X  271          /* 9520 訊息欄（原版相对 9500 = [-3,311]） */
#define MSG_Y  450
#define MSG_W  173
#define MSG_H  19

#define YES_X  431          /* 9531/9532 */
#define BST_Y  447
#define BST_W  38
#define BST_H  26

/* ---------------------------------------------------------------------- 配色 */
#define C_BG      8, 10, 18
#define C_FRAME   150, 130, 80
#define C_PANEL   22, 26, 40
#define C_TITLE   0xF0DCA0u
#define C_TEXT    0xDCDCDCu
#define C_DIM     0x909090u
#define C_OK      0x7CD47Cu
#define C_NO      0xC05050u
#define C_HOVER   0xD25915u
#define C_SEL     60, 120, 200      /* 选中行底色（rect 用三元组） */
#define C_ROW     40, 60, 110       /* 悬停行底色 */
#define C_BTN     45, 62, 112       /* 按钮底色 */
#define C_SLOT    34, 30, 24        /* 空装备槽底色 */
#define C_SQ      30, 34, 52        /* 小队格底色 */

#define TAB_COUNT 5

struct S3OfficerUI {
    S3OuiDrawText  draw_text; void *text_ud;
    S3OuiReadAsset read_asset; void *asset_ud;

    int mode;                       /* S3OuiMode */

    /* --- CARD --- */
    char ttl[64];
    char line[5][128];

    /* --- ARRAY --- */
    S3Roster *roster;
    int  off;                       /* 当前武将 roster 下标 */
    int  tab;                       /* 0..4 */
    int  hover;
    int  sel;                       /* 子选单/大列表 选中项（-1 = 无） */
    int  confirm;                   /* 1 = 正在「是否」确认 */
    char msg[192];
    char lord[32];                  /* 君主名 */
    char rank[32];                  /* 官位名（空 = 未任命） */
    int  rank_soldiers;
    char sa_name[8][32];            /* 该武将的必杀技名（最多 8） */

    const S3Magic *list[256];
    int  list_sf[256];
    int  n_list;

    /* 学技结果 */
    char last_name[32];
    int  last_sf, last_cost, last_failed;

    /* 肖像 */
    ShpImage face;
    int  face_ok, face_no;

    const S3ArrayTables *T;
};

S3OfficerUI *s3_oui_new(S3OuiDrawText draw_text, void *text_ud,
                        S3OuiReadAsset read_asset, void *asset_ud) {
    S3OfficerUI *u = (S3OfficerUI *)calloc(1, sizeof *u);
    if (!u) return NULL;
    u->draw_text = draw_text; u->text_ud = text_ud;
    u->read_asset = read_asset; u->asset_ud = asset_ud;
    u->off = -1; u->sel = -1; u->hover = -1; u->face_no = -1;
    return u;
}

void s3_oui_free(S3OfficerUI *u) {
    if (!u) return;
    if (u->face_ok) shp_free(&u->face);
    free(u);
}

void s3_oui_set_tables(S3OfficerUI *u, const S3ArrayTables *t) { if (u) u->T = t; }

void s3_oui_close(S3OfficerUI *u) { if (u) { u->mode = S3_OUI_NONE; u->hover = -1; u->confirm = 0; } }
int  s3_oui_active(const S3OfficerUI *u) { return u ? (u->mode != S3_OUI_NONE) : 0; }
int  s3_oui_mode(const S3OfficerUI *u) { return u ? u->mode : S3_OUI_NONE; }
int  s3_oui_off(const S3OfficerUI *u) { return u ? u->off : -1; }

const char *s3_oui_last_learn_name(const S3OfficerUI *u) { return u ? u->last_name : ""; }
int s3_oui_last_learn_is_sf(const S3OfficerUI *u) { return u ? u->last_sf : 0; }
int s3_oui_last_learn_cost(const S3OfficerUI *u) { return u ? u->last_cost : 0; }
int s3_oui_last_learn_failed(const S3OfficerUI *u) { return u ? u->last_failed : 0; }

void s3_oui_set_lord(S3OfficerUI *u, const char *n) {
    if (u) snprintf(u->lord, sizeof u->lord, "%.31s", (n && *n) ? n : "—");
}
void s3_oui_set_rank(S3OfficerUI *u, const char *n, int soldiers) {
    if (!u) return;
    snprintf(u->rank, sizeof u->rank, "%.31s", (n && *n) ? n : "");
    u->rank_soldiers = soldiers;
}

/* ------------------------------------------------------------------ 肖像 */
static void load_face(S3OfficerUI *u) {
    const S3Officer *o = (u->roster && u->off >= 0) ? s3_roster_at(u->roster, u->off) : NULL;
    const int no = o ? o->portrait : 0;
    if (no <= 0 || !u->read_asset) return;
    if (u->face_ok && u->face_no == no) return;
    if (u->face_ok) { shp_free(&u->face); u->face_ok = 0; }
    char path[128];
    snprintf(path, sizeof path, "Shape\\Portrait\\Portrait%03d.SHP", no);
    uint32_t len = 0;
    uint8_t *raw = u->read_asset(u->asset_ud, path, &len);
    if (!raw) { u->face_no = -1; return; }
    const char *err = NULL;
    u->face_ok = (shp_decode(raw, len, &u->face, &err) == 0);
    u->face_no = u->face_ok ? no : -1;
}

/* -------------------------------------------------------------- 武将信息块 */
static void build_card(S3OfficerUI *u, const S3Officer *o) {
    if (!o) return;
    const int cap = s3_officer_troop_limit_ex(o, u->rank_soldiers);
    const int need = s3_officer_exp_need(o->level);
    const char *ld = u->lord[0] ? u->lord : "—";
    const char *rk = u->rank[0] ? u->rank : "—";
    /* 原版 9045 五行；姓名行按原版截图再加「官位」 */
    snprintf(u->line[0], sizeof u->line[0], "%s  %s      戰績 %d勝%d敗",
             o->name, rk, o->wins, o->losses);
    snprintf(u->line[1], sizeof u->line[1], "君主 %-8s 武力 %-4d 體力 %d/%d",
             ld, o->str, o->hp, o->hp_max);
    snprintf(u->line[2], sizeof u->line[2], "等級 %-8d 智力 %-4d 技力 %d/%d",
             o->level, o->intel, o->mp, o->mp_max);
    snprintf(u->line[3], sizeof u->line[3], "忠誠 %-8d 士氣 %-4d 帶兵數 %d/%d",
             o->loyalty, 70, o->troops, cap);
    snprintf(u->line[4], sizeof u->line[4], "功勳 %-8d 經驗值 %d/%d",
             o->merit, o->exp, need);
}

/* 该武将持有的必杀技名（Text.ini 9031~9038 的 8 个） */
static void build_sa_names(S3OfficerUI *u, const S3Officer *o) {
    for (int i = 0; i < 8; ++i) u->sa_name[i][0] = '\0';
    if (!o || !u->T) return;
    for (int i = 0; i < o->n_super_attack && i < 8; ++i) {
        const int id = o->super_attack[i];
        const char *nm = (u->T->sa_names && id >= 1 && id <= 8 && u->T->sa_names[id - 1])
                             ? u->T->sa_names[id - 1] : "?";
        snprintf(u->sa_name[i], sizeof u->sa_name[i], "%s", nm);
    }
}

/* 重建"该页签的列表" */
static void rebuild_list(S3OfficerUI *u) {
    u->n_list = 0; u->sel = -1; u->confirm = 0;
    const S3Officer *o = (u->roster && u->off >= 0) ? s3_roster_at(u->roster, u->off) : NULL;
    if (!o || !u->T) return;
    if (u->tab == 3 || u->tab == 4) {                    /* 武將技 / 軍師技 */
        const S3Magic *arr = (u->tab == 3) ? u->T->bf : u->T->sf;
        const int n = (u->tab == 3) ? u->T->n_bf : u->T->n_sf;
        const int is_sf = (u->tab == 4);
        for (int i = 0; i < n && u->n_list < 256; ++i) {
            const S3Magic *m = &arr[i];
            const int known = is_sf ? s3_officer_knows_sf(o, m->no) : s3_officer_knows_bf(o, m->no);
            if (known) continue;                          /* 已学不再列出（定稿 P2） */
            if (!s3_magic_learnable(m, o->str, o->intel, o->level)) continue;
            u->list[u->n_list] = m; u->list_sf[u->n_list] = is_sf; ++u->n_list;
        }
    }
}

void s3_oui_open_array(S3OfficerUI *u, S3Roster *roster, int off_idx) {
    if (!u) return;
    u->roster = roster;
    u->off = off_idx;
    u->tab = 0;
    u->hover = -1;
    u->mode = S3_OUI_ARRAY;
    const S3Officer *o = (roster && off_idx >= 0) ? s3_roster_at(roster, off_idx) : NULL;
    build_sa_names(u, o);
    rebuild_list(u);
    load_face(u);
    build_card(u, o);
    snprintf(u->msg, sizeof u->msg, "%s（←→ 換武將）", o ? o->name : "無武將");
}

/* 武将信息块浮层（情報） */
void s3_oui_show_card(S3OfficerUI *u, const S3Officer *o, const char *lord,
                      int rank_soldiers) {
    if (!u || !o) return;
    u->mode = S3_OUI_CARD;
    u->hover = -1;
    u->rank_soldiers = rank_soldiers;
    s3_oui_set_lord(u, lord);
    u->rank[0] = '\0';
    build_card(u, o);
    snprintf(u->ttl, sizeof u->ttl, "武將情報");
}

/* ------------------------------------------------------- 换武将（← / →） */
static int step_officer(S3OfficerUI *u, int dir) {
    if (!u->roster) return 0;
    const int n = s3_roster_count(u->roster);
    int i = u->off;
    for (int k = 0; k < n; ++k) {
        i += dir;
        if (i < 0) i = n - 1;
        if (i >= n) i = 0;
        const S3Officer *o = s3_roster_at(u->roster, i);
        if (o && o->mine && !o->wild) {                   /* 只在我方武将之间轮转 */
            u->off = i;
            build_sa_names(u, o);
            rebuild_list(u);
            load_face(u);
            build_card(u, o);
            snprintf(u->msg, sizeof u->msg, "%s（←→ 換武將）", o->name);
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ 渲染 */
static void rect(Sango3Canvas *cv, int x, int y, int w, int h, int r, int g, int b) {
    sango3_canvas_fill(cv, x, y, w, h, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}
static void frame(Sango3Canvas *cv, int x, int y, int w, int h) {
    sango3_canvas_frame(cv, x, y, w, h, 1, C_FRAME);
}
static void txt(S3OfficerUI *u, Sango3Canvas *cv, const char *s, int x, int y,
                int w, int h, uint32_t c, int centered) {
    if (u->draw_text) u->draw_text(u->text_ud, cv, s, x, y, w, h, c, 1,
                                    centered ? 0x4u : 0u);
}

static const char *tab_label(int t) {
    static const char *N[TAB_COUNT] = { "陣形", "兵種", "必殺技", "武將技", "軍師技" };
    return (t >= 0 && t < TAB_COUNT) ? N[t] : "?";
}

static int sub_count(const S3OfficerUI *u, const S3Officer *o) {
    int n = 0;
    if (u->tab == 0) n = u->T ? u->T->n_form : 0;
    else if (u->tab == 1) n = u->T ? u->T->n_soldier : 0;
    else n = o ? o->n_super_attack : 0;
    return (n > 9) ? 9 : n;
}

void s3_oui_render(S3OfficerUI *u, Sango3Canvas *cv) {
    if (!u || !cv || u->mode == S3_OUI_NONE) return;

    /* ---------------- 信息块浮层（情報） ---------------- */
    if (u->mode == S3_OUI_CARD) {
        const int w = 470, h = 200;
        const int x = (cv->w - w) / 2, y = (cv->h - h) / 2;
        rect(cv, x, y, w, h, C_BG);
        frame(cv, x, y, w, h);
        txt(u, cv, u->ttl, x + 12, y, w - 24, 32, C_TITLE, 0);
        for (int i = 0; i < 5; ++i)
            txt(u, cv, u->line[i], x + 14, y + 34 + i * 28, w - 28, 28, C_TEXT, 0);
        txt(u, cv, "點擊任意處關閉", x + 12, y + h - 30, w - 24, 26, C_DIM, 1);
        return;
    }

    /* ---------------- 整备全屏界面 ---------------- */
    const S3Officer *o = (u->roster && u->off >= 0) ? s3_roster_at(u->roster, u->off) : NULL;
    if (!o) { txt(u, cv, "（無可整備的武將）", 20, 20, 400, 30, C_NO, 0); return; }

    rect(cv, 0, 0, AR_W, AR_H, C_BG);

    /* 9000 肖像 + 9001 框 */
    rect(cv, PT_X, PT_Y, PT_W, PT_H, 28, 24, 18);
    frame(cv, PT_X - 6, PT_Y - 6, PT_W + 12, PT_H + 12);
    if (u->face_ok && u->face.rgba)
        sango3_canvas_blit(cv, u->face.rgba, (int32_t)u->face.width, (int32_t)u->face.height,
                           PT_X, PT_Y, 1);
    else
        txt(u, cv, o->name, PT_X, PT_Y, PT_W, PT_H, C_DIM, 1);

    /* 9100 資訊欄 */
    rect(cv, IF_X, IF_Y, IF_W, IF_H, C_PANEL);
    frame(cv, IF_X, IF_Y, IF_W, IF_H);
    for (int i = 0; i < 5; ++i)
        txt(u, cv, u->line[i], IF_X + 10, IF_Y + 6 + i * 22, IF_W - 20, 22, C_TEXT, 0);

    /* 9101/9102 ← → */
    rect(cv, NAV_X1, NAV_Y, NAV_W, NAV_H, C_BTN);
    rect(cv, NAV_X2, NAV_Y, NAV_W, NAV_H, C_BTN);
    txt(u, cv, "←", NAV_X1, NAV_Y, NAV_W, NAV_H, C_TEXT, 1);
    txt(u, cv, "→", NAV_X2, NAV_Y, NAV_W, NAV_H, C_TEXT, 1);

    /* 9200 物品欄 + 三装备槽 */
    rect(cv, IT_X, IT_Y, IT_W, IT_H, C_PANEL);
    frame(cv, IT_X, IT_Y, IT_W, IT_H);
    txt(u, cv, "物品", IT_X + 8, IT_Y + 4, IT_W - 16, 20, C_TITLE, 0);
    {
        const char *names[3] = { o->weapon, o->book, o->horse };
        const char *labels[3] = { "武器", "書", "馬" };
        const int ys[3] = { SLOT_Y0, SLOT_Y1, SLOT_Y2 };
        const int ny[3] = { NOTE_Y0, NOTE_Y1, NOTE_Y2 };
        for (int i = 0; i < 3; ++i) {
            rect(cv, SLOT_X, ys[i], SLOT_W, SLOT_H, C_SLOT);
            frame(cv, SLOT_X, ys[i], SLOT_W, SLOT_H);
            txt(u, cv, labels[i], SLOT_X, ys[i] + SLOT_H / 2 - 10, SLOT_W, 20, C_DIM, 1);
            txt(u, cv, (names[i][0] ? names[i] : "—"), SLOT_X - 8, ny[i], SLOT_W + 16, NOTE_H,
                names[i][0] ? C_OK : C_DIM, 1);
        }
    }

    /* 9300 選項（五页签） */
    rect(cv, OPT_X, OPT_Y, OPT_W, OPT_H, C_PANEL);
    frame(cv, OPT_X, OPT_Y, OPT_W, OPT_H);
    for (int t = 0; t < TAB_COUNT; ++t) {
        const int by = OPT_Y + 6 + t * OPT_IH;
        const int on = (t == u->tab);
        rect(cv, OPT_X + 6, by, OPT_W - 12, OPT_BH, on ? 45 : 26, on ? 62 : 30, on ? 112 : 46);
        txt(u, cv, tab_label(t), OPT_X + 6, by, OPT_W - 12, OPT_BH, on ? C_TITLE : C_TEXT, 1);
    }

    /* 子選單（页签 0~2） / 学技大列表（页签 3~4） */
    if (u->tab <= 2) {
        const int n = sub_count(u, o);
        const int h = 17 + SUB_ROW * (n > 0 ? n : 1);
        rect(cv, SUB_X, SUB_Y, SUB_W, h, C_PANEL);
        frame(cv, SUB_X, SUB_Y, SUB_W, h);
        if (n == 0) txt(u, cv, "（無）", SUB_X, SUB_Y + 4, SUB_W, SUB_ROW, C_DIM, 1);
        for (int i = 0; i < n; ++i) {
            const char *nm = "?";
            if (u->tab == 0)      nm = (u->T && u->T->form_names[i]) ? u->T->form_names[i] : "?";
            else if (u->tab == 1) nm = (u->T && u->T->soldier_names[i]) ? u->T->soldier_names[i] : "?";
            else                  nm = u->sa_name[i];
            const int ry = SUB_Y + 4 + i * SUB_ROW;
            if (u->hover == i) rect(cv, SUB_X + 3, ry, SUB_W - 6, SUB_ROW - 2, C_ROW);
            if (u->sel == i)   rect(cv, SUB_X + 3, ry, SUB_W - 6, SUB_ROW - 2, C_SEL);
            txt(u, cv, nm, SUB_X + 6, ry, SUB_W - 12, SUB_ROW - 2, C_TEXT, 0);
        }
    } else {
        rect(cv, BIG_X, BIG_Y, BIG_W, BIG_H, C_PANEL);
        frame(cv, BIG_X, BIG_Y, BIG_W, BIG_H);
        txt(u, cv, "名稱", BIG_X + 10, BIG_Y, 130, BIG_ROW, C_TITLE, 0);
        txt(u, cv, "等級", BIG_X + 150, BIG_Y, 50, BIG_ROW, C_TITLE, 0);
        txt(u, cv, "MP", BIG_X + 206, BIG_Y, 46, BIG_ROW, C_TITLE, 0);
        txt(u, cv, "所需功勳", BIG_X + 256, BIG_Y, 96, BIG_ROW, C_TITLE, 0);
        txt(u, cv, "狀態", BIG_X + 356, BIG_Y, 60, BIG_ROW, C_TITLE, 0);
        const int rows = (BIG_H - BIG_ROW - 6) / BIG_ROW;
        for (int i = 0; i < rows && i < u->n_list; ++i) {
            const S3Magic *m = u->list[i];
            const int afford = (o->merit >= m->contribution);
            const int ry = BIG_Y + BIG_ROW + 2 + i * BIG_ROW;
            if (u->hover == i) rect(cv, BIG_X + 3, ry, BIG_W - 6, BIG_ROW - 2, C_ROW);
            if (u->sel == i)   rect(cv, BIG_X + 3, ry, BIG_W - 6, BIG_ROW - 2, C_SEL);
            uint32_t c = afford ? C_OK : C_NO;
            if (u->hover == i) c = C_HOVER;
            char b[64];
            snprintf(b, sizeof b, "%s", m->name);
            txt(u, cv, b, BIG_X + 10, ry, 130, BIG_ROW - 2, c, 0);
            snprintf(b, sizeof b, "%d", m->level);
            txt(u, cv, b, BIG_X + 150, ry, 50, BIG_ROW - 2, C_TEXT, 0);
            snprintf(b, sizeof b, "%d", m->mp);
            txt(u, cv, b, BIG_X + 206, ry, 46, BIG_ROW - 2, C_TEXT, 0);
            snprintf(b, sizeof b, "%d", m->contribution);
            txt(u, cv, b, BIG_X + 256, ry, 96, BIG_ROW - 2, c, 0);
            txt(u, cv, afford ? "可學" : "功勳不足", BIG_X + 356, ry, 70, BIG_ROW - 2, c, 0);
        }
        if (u->n_list == 0)
            txt(u, cv, "（無可學之技 —— 等級/武力/智力未達區間，或已全部學會）",
                BIG_X + 10, BIG_Y + BIG_ROW + 8, BIG_W - 20, BIG_ROW, C_DIM, 0);
    }

    /* 9400 陣形（8 个小队） */
    rect(cv, SQ_X, SQ_Y, SQ_W, SQ_H, C_PANEL);
    frame(cv, SQ_X, SQ_Y, SQ_W, SQ_H);
    txt(u, cv, "部隊（8 小隊）", SQ_X + 8, SQ_Y + 3, SQ_W - 16, 18, C_TITLE, 0);
    {
        const int cx[2] = { SQ_X + SQ_CX0, SQ_X + SQ_CX1 };
        const int cy[4] = { SQ_Y + SQ_CY0, SQ_Y + SQ_CY1, SQ_Y + SQ_CY2, SQ_Y + SQ_CY3 };
        for (int i = 0; i < 8; ++i) {
            const int x = cx[i % 2], y = cy[i / 2] + 16;
            rect(cv, x, y, SQ_CW, SQ_CH, C_SQ);
            frame(cv, x, y, SQ_CW, SQ_CH);
            const int st = (i < o->n_soldier_type) ? o->soldier_type[i] : -1;
            const char *nm = "—";
            if (st >= 0 && u->T && st < u->T->n_soldier && u->T->soldier_names[st])
                nm = u->T->soldier_names[st];
            char b[32];
            snprintf(b, sizeof b, "%s", nm);
            txt(u, cv, b, x + 4, y + 2, SQ_CW - 8, 20, C_TEXT, 1);
            txt(u, cv, "★", x + 8, y + 24, SQ_CW - 16, 14, C_TITLE, 1);   /* 进阶兵种为 ★★ */
            snprintf(b, sizeof b, "%d", o->troops / 8);
            txt(u, cv, b, x + 4, y + 40, SQ_CW - 8, 16, C_TEXT, 1);
        }
    }

    /* 9520 訊息欄 / 9531·9532 是否 */
    if (u->msg[0]) txt(u, cv, u->msg, MSG_X, MSG_Y, MSG_W + 200, MSG_H, C_TITLE, 0);
    if (u->confirm) {
        rect(cv, YES_X, BST_Y, BST_W, BST_H, C_BTN);
        rect(cv, YES_X + 40, BST_Y, BST_W, BST_H, C_BTN);
        txt(u, cv, "是", YES_X, BST_Y, BST_W, BST_H, C_TEXT, 1);
        txt(u, cv, "否", YES_X + 40, BST_Y, BST_W, BST_H, C_TEXT, 1);
    }
}

/* ------------------------------------------------------------------ 命中 */
static int inbox(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void s3_oui_on_move(S3OfficerUI *u, int32_t x, int32_t y) {
    if (!u || u->mode != S3_OUI_ARRAY) return;
    u->hover = -1;
    const S3Officer *o = (u->roster && u->off >= 0) ? s3_roster_at(u->roster, u->off) : NULL;
    if (!o) return;
    if (u->tab <= 2) {
        const int n = sub_count(u, o);
        for (int i = 0; i < n; ++i) {
            const int ry = SUB_Y + 4 + i * SUB_ROW;
            if (inbox(x, y, SUB_X + 3, ry, SUB_W - 6, SUB_ROW - 2)) { u->hover = i; return; }
        }
    } else {
        const int rows = (BIG_H - BIG_ROW - 6) / BIG_ROW;
        for (int r = 0; r < rows && r < u->n_list; ++r) {
            const int ry = BIG_Y + BIG_ROW + 2 + r * BIG_ROW;
            if (inbox(x, y, BIG_X + 3, ry, BIG_W - 6, BIG_ROW - 2)) { u->hover = r; return; }
        }
    }
}

int s3_oui_on_click(S3OfficerUI *u, int32_t x, int32_t y) {
    if (!u || u->mode == S3_OUI_NONE) return -1;
    if (u->mode == S3_OUI_CARD) { s3_oui_close(u); return -2; }

    const S3Officer *o = (u->roster && u->off >= 0) ? s3_roster_at(u->roster, u->off) : NULL;
    if (!o) { s3_oui_close(u); return -2; }

    /* 是否确认框优先 */
    if (u->confirm) {
        if (inbox(x, y, YES_X, BST_Y, BST_W, BST_H)) {
            S3Officer *om = s3_roster_mut(u->roster, u->off);
            const S3Magic *m = (u->sel >= 0 && u->sel < u->n_list) ? u->list[u->sel] : NULL;
            u->last_name[0] = '\0'; u->last_sf = u->last_cost = u->last_failed = 0;
            if (om && m) {
                snprintf(u->last_name, sizeof u->last_name, "%s", m->name);
                u->last_sf = u->list_sf[u->sel];
                u->last_cost = m->contribution;
                if (om->merit < m->contribution) {
                    u->last_failed = 1;
                    snprintf(u->msg, sizeof u->msg, "功勳不足（需 %d，目前 %d）",
                             m->contribution, om->merit);
                } else {
                    s3_officer_spend_merit(om, m->contribution);
                    if (u->last_sf) s3_officer_learn_sf(om, m->no);
                    else            s3_officer_learn_bf(om, m->no);
                    snprintf(u->msg, sizeof u->msg, "獲得%s %s（剩餘功勳 %d）",
                             u->last_sf ? "軍師技" : "武將技", m->name, om->merit);
                }
                u->confirm = 0;
                rebuild_list(u);
                build_card(u, s3_roster_at(u->roster, u->off));
                return u->last_failed ? -3 : 1;
            }
            u->confirm = 0;
            return -3;
        }
        if (inbox(x, y, YES_X + 40, BST_Y, BST_W, BST_H)) {
            u->confirm = 0; u->sel = -1;
            snprintf(u->msg, sizeof u->msg, "已取消");
            return -3;
        }
        return -3;
    }

    /* 9101/9102 ← → 换武将 */
    if (inbox(x, y, NAV_X1, NAV_Y, NAV_W, NAV_H)) { step_officer(u, -1); return 0; }
    if (inbox(x, y, NAV_X2, NAV_Y, NAV_W, NAV_H)) { step_officer(u, +1); return 0; }

    /* 9301~9305 五页签 */
    for (int t = 0; t < TAB_COUNT; ++t) {
        const int by = OPT_Y + 6 + t * OPT_IH;
        if (inbox(x, y, OPT_X + 6, by, OPT_W - 12, OPT_BH)) {
            u->tab = t;
            rebuild_list(u);
            snprintf(u->msg, sizeof u->msg, "%s：%d 項（功勳 %d）",
                     tab_label(t), u->n_list, o->merit);
            return -3;
        }
    }

    /* 子選單 / 学技大列表 */
    if (u->tab <= 2) {
        const int n = sub_count(u, o);
        for (int i = 0; i < n; ++i) {
            const int ry = SUB_Y + 4 + i * SUB_ROW;
            if (inbox(x, y, SUB_X + 3, ry, SUB_W - 6, SUB_ROW - 2)) {
                u->sel = i;
                if (u->tab == 2) snprintf(u->msg, sizeof u->msg, "必殺技：%s", u->sa_name[i]);
                else snprintf(u->msg, sizeof u->msg, "%s：%s（變更待接）", tab_label(u->tab),
                              (u->tab == 0 && u->T) ? u->T->form_names[i]
                                                    : (u->T ? u->T->soldier_names[i] : "?"));
                return -3;
            }
        }
    } else {
        const int rows = (BIG_H - BIG_ROW - 6) / BIG_ROW;
        for (int r = 0; r < rows && r < u->n_list; ++r) {
            const int ry = BIG_Y + BIG_ROW + 2 + r * BIG_ROW;
            if (inbox(x, y, BIG_X + 3, ry, BIG_W - 6, BIG_ROW - 2)) {
                u->sel = r; u->confirm = 1;
                const S3Magic *m = u->list[r];
                if (o->merit < m->contribution)
                    snprintf(u->msg, sizeof u->msg, "功勳不足：%s 需 %d，目前 %d",
                             m->name, m->contribution, o->merit);
                else
                    snprintf(u->msg, sizeof u->msg, "學習%s？所需功勳：%d",
                             m->name, m->contribution);
                return -3;
            }
        }
    }
    return -3;      /* 面板内其它位置：吞掉点击，不落到朝堂 */
}
