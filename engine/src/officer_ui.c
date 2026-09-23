/*
 * officer_ui.c —— 整备界面（ARRAY）/ 武将名单（PICK）/ 武将信息块（CARD）
 *
 * 坐标一律用**原版 640×480 空间**（与 ui.json 的 rect 一一对应，便于日后逐像素核对）。
 *
 * 三层结构（用户 2026-09-23 要求）：
 *   朝堂 → 軍政 → 整備 →【PICK 武将名单】→ 点某将 →【ARRAY 整备页】
 *   长按：ARRAY → 回名单；名单 → 关界面回朝堂（逐层退，与原版"返回上一层"一致）。
 *
 * 技表（武將技/軍師技 页签）显示**全表**并区分四种状态（见 ST_*），
 * 可切换筛选「全部 / 已學 / 可學」，未学会的可点选学习（扣 Contribution 功勋）。
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
#define LST_X  536          /* 自加：「名單」按钮（回到武将名单；原版无此项） */
#define LST_W  40

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

#define BIG_X  274          /* 9500 技表大列表（原版自右滑入；我们直接放可视区） */
#define BIG_Y  139
#define BIG_W  366
#define BIG_H  298
#define BIG_ROW 24

#define BIG_FILT_Y  (BIG_Y + 2)     /* 筛选条 [全部|已學|可學] + 计数 */
#define BIG_FILT_H  20
#define BIG_FB_X    (BIG_X + 6)     /* 第一个筛选按钮 */
#define BIG_FB_W    46
#define BIG_FB_GAP  4
#define BIG_CNT_X   (BIG_X + 158)   /* 计数文案起点 */
#define BIG_HDR_Y   (BIG_Y + 24)    /* 列头 */
#define BIG_HDR_H   20
#define BIG_ROWS_Y  (BIG_Y + 46)    /* 数据行起点 */
#define BIG_ROWS    9               /* 每頁 9 行（留出底部分頁按钮） */
#define BIG_PAGE_Y  (BIG_ROWS_Y + BIG_ROWS * BIG_ROW + 2)
#define BIG_PAGE_H  20

#define MSG_X  271          /* 9520 訊息欄（原版相对 9500 = [-3,311]） */
#define MSG_Y  450
#define MSG_W  360          /* 原版 173 太窄，放不下"未達"原因 */
#define MSG_H  19

#define YES_X  431          /* 9531/9532 */
#define BST_Y  447
#define BST_W  38
#define BST_H  26

/* ---------------------------------------------------- 武将名单（PICK，自加） */
#define PK_X   80
#define PK_Y   50
#define PK_W   480
#define PK_H   380
#define PK_ROW 24
#define PK_ROWS 12
#define PK_HDR_Y (PK_Y + 34)
#define PK_ROWS_Y (PK_Y + 56)
#define PK_PAGE_Y (PK_ROWS_Y + PK_ROWS * PK_ROW + 4)
#define PK_PAGE_H 22
#define PK_BTN_W 68

/* ---------------------------------------------------------------------- 配色 */
#define C_BG      8, 10, 18
#define C_FRAME   150, 130, 80
#define C_PANEL   22, 26, 40
#define C_MASK    0, 0, 0
#define C_TITLE   0xF0DCA0u
#define C_TEXT    0xDCDCDCu
#define C_DIM     0x909090u
#define C_OK      0x7CD47Cu
#define C_NO      0xC05050u
#define C_KNOWN   0x9AC8FFu     /* 已學（与"可學"的绿明确区分） */
#define C_HOVER   0xD25915u
#define C_SEL     60, 120, 200      /* 选中行底色 */
#define C_ROW     40, 60, 110       /* 悬停行底色 */
#define C_BTN     45, 62, 112       /* 按钮底色 */
#define C_BTN_ON  90, 130, 200      /* 选中的按钮（筛选条） */
#define C_SLOT    34, 30, 24        /* 空装备槽底色 */
#define C_SQ      30, 34, 52        /* 小队格底色 */

#define TAB_COUNT 5

/* 技的四种状态（武將技/軍師技 页签） */
enum { ST_KNOWN = 0, ST_OK = 1, ST_POOR = 2, ST_LOCK = 3 };
/* 筛选：0 全部 · 1 已學 · 2 可學（含功勋不足，都是"符合条件、能学"的） */
enum { FILT_ALL = 0, FILT_KNOWN = 1, FILT_OPEN = 2 };

struct S3OfficerUI {
    S3OuiDrawText  draw_text; void *text_ud;
    S3OuiReadAsset read_asset; void *asset_ud;

    int mode;                       /* S3OuiMode */

    /* --- CARD --- */
    char ttl[64];
    char line[5][128];

    /* --- PICK（武将名单） --- */
    int  pick_idx[256];
    int  n_pick;
    int  pick_page;
    int  pick_hover;

    /* --- ARRAY --- */
    S3Roster *roster;
    int  off;                       /* 当前武将 roster 下标 */
    int  tab;                       /* 0..4 */
    int  hover;
    int  sel;                       /* 子选单/大列表 选中项（-1 = 无） */
    int  confirm;                   /* 1 = 正在「是否」确认 */
    int  filt;                      /* FILT_* */
    int  page;                      /* 技表当前页（0 基） */
    int  count_all, count_known, count_open;
    char msg[192];
    char lord[32];                  /* 君主名 */
    char sa_name[8][32];            /* 该武将的必杀技名（最多 8） */

    const S3Magic *list[256];
    int  list_sf[256];
    int  list_st[256];
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
    u->pick_hover = -1;
    return u;
}

void s3_oui_free(S3OfficerUI *u) {
    if (!u) return;
    if (u->face_ok) shp_free(&u->face);
    free(u);
}

void s3_oui_set_tables(S3OfficerUI *u, const S3ArrayTables *t) { if (u) u->T = t; }

void s3_oui_close(S3OfficerUI *u) {
    if (u) { u->mode = S3_OUI_NONE; u->hover = -1; u->pick_hover = -1; u->confirm = 0; }
}
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
    const int cap = s3_officer_troop_limit(o);          /* 等级×40 + 官位加成（定稿 J8） */
    const int need = s3_officer_exp_need(o->level);
    const char *ld = u->lord[0] ? u->lord : "—";
    const char *rk = o->rank_name[0] ? o->rank_name : "—";
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

/* 技的状态（武將技/軍師技 通用） */
static int status_of(const S3Officer *o, const S3Magic *m, int is_sf) {
    const int known = is_sf ? s3_officer_knows_sf(o, m->no) : s3_officer_knows_bf(o, m->no);
    if (known) return ST_KNOWN;                                  /* 定稿 P2：学会即永久保留 */
    if (!s3_magic_learnable(m, o->str, o->intel, o->level)) return ST_LOCK;
    return (o->merit >= m->contribution) ? ST_OK : ST_POOR;
}
static int st_prio(int st) {
    return (st == ST_OK) ? 0 : (st == ST_POOR) ? 1 : (st == ST_KNOWN) ? 2 : 3;
}
static const char *st_label(int st) {
    return (st == ST_KNOWN) ? "已學" : (st == ST_OK) ? "可學"
                           : (st == ST_POOR) ? "功勳不足" : "未達";
}
static uint32_t st_color(int st) {
    return (st == ST_KNOWN) ? C_KNOWN : (st == ST_OK) ? C_OK
                            : (st == ST_POOR) ? C_NO : C_DIM;
}

/* 重建"该页签的列表"（武將技/軍師技 = 全表 + 状态 + 筛选 + 排序） */
static void rebuild_list(S3OfficerUI *u) {
    u->n_list = 0; u->sel = -1; u->confirm = 0; u->page = 0;
    u->count_all = u->count_known = u->count_open = 0;
    const S3Officer *o = (u->roster && u->off >= 0) ? s3_roster_at(u->roster, u->off) : NULL;
    if (!o || !u->T) return;
    if (u->tab != 3 && u->tab != 4) return;              /* 陣形/兵種/必殺技 走子選單 */

    const int is_sf = (u->tab == 4);
    const S3Magic *arr = is_sf ? u->T->sf : u->T->bf;
    const int n = is_sf ? u->T->n_sf : u->T->n_bf;
    if (!arr || n <= 0) return;

    /* 1) 全表 + 状态 */
    static const S3Magic *tmp[256]; static int tst[256];
    int nt = 0;
    for (int i = 0; i < n && nt < 256; ++i) {
        tmp[nt] = &arr[i];
        tst[nt] = status_of(o, &arr[i], is_sf);
        ++nt;
    }
    u->count_all = nt;
    for (int i = 0; i < nt; ++i) {
        if (tst[i] == ST_KNOWN) ++u->count_known;
        if (tst[i] == ST_OK || tst[i] == ST_POOR) ++u->count_open;
    }

    /* 2) 排序：可學 → 功勳不足 → 已學 → 未達；组内按 等级、No（插入排序，稳定可复现） */
    for (int i = 1; i < nt; ++i) {
        const S3Magic *km = tmp[i]; const int ks = tst[i];
        int j = i - 1;
        while (j >= 0 && (st_prio(tst[j]) > st_prio(ks) ||
                          (st_prio(tst[j]) == st_prio(ks) &&
                           (tmp[j]->level > km->level ||
                            (tmp[j]->level == km->level && tmp[j]->no > km->no))))) {
            tmp[j + 1] = tmp[j]; tst[j + 1] = tst[j]; --j;
        }
        tmp[j + 1] = km; tst[j + 1] = ks;
    }

    /* 3) 按筛选拷进可见列表 */
    for (int i = 0; i < nt && u->n_list < 256; ++i) {
        if (u->filt == FILT_KNOWN && tst[i] != ST_KNOWN) continue;
        if (u->filt == FILT_OPEN && !(tst[i] == ST_OK || tst[i] == ST_POOR)) continue;
        u->list[u->n_list] = tmp[i];
        u->list_st[u->n_list] = tst[i];
        u->list_sf[u->n_list] = is_sf;
        ++u->n_list;
    }
}

static void array_enter(S3OfficerUI *u, int off_idx) {
    u->off = off_idx;
    u->tab = 0;
    u->filt = FILT_ALL;
    u->hover = -1;
    u->msg[0] = '\0';
    u->mode = S3_OUI_ARRAY;
    const S3Officer *o = (u->roster && off_idx >= 0) ? s3_roster_at(u->roster, off_idx) : NULL;
    build_sa_names(u, o);
    rebuild_list(u);
    load_face(u);
    build_card(u, o);
    if (o) snprintf(u->msg, sizeof u->msg, "%s（←→ 換武將 · 長按回名單）", o->name);
}

/* ---- 武将名单（PICK） ---- */
static void pick_build(S3OfficerUI *u) {
    u->n_pick = 0;
    if (!u->roster) return;
    const int n = s3_roster_count(u->roster);
    for (int i = 0; i < n && u->n_pick < 256; ++i) {
        const S3Officer *o = s3_roster_at(u->roster, i);
        if (!o || o->wild || !o->mine) continue;         /* 整备对象 = 我方非在野 */
        u->pick_idx[u->n_pick++] = i;
    }
}

void s3_oui_open_pick(S3OfficerUI *u, S3Roster *roster) {
    if (!u) return;
    u->roster = roster;
    pick_build(u);
    u->pick_page = 0;
    u->pick_hover = -1;
    u->hover = -1;
    u->confirm = 0;
    u->msg[0] = '\0';
    u->mode = S3_OUI_PICK;
    if (u->n_pick == 0) snprintf(u->msg, sizeof u->msg, "我方尚無可整備的武將");
    else                snprintf(u->msg, sizeof u->msg, "請選擇要整備的武將（共 %d 人）", u->n_pick);
}

void s3_oui_open_array(S3OfficerUI *u, S3Roster *roster, int off_idx) {
    if (!u) return;
    u->roster = roster;
    array_enter(u, off_idx);
}

/* 武将信息块浮层（情報） */
void s3_oui_show_card(S3OfficerUI *u, const S3Officer *o, const char *lord,
                      int rank_soldiers) {
    if (!u || !o) return;
    (void)rank_soldiers;                 /* 官位加成已由 roster 自动授勋写入武将自身 */
    u->mode = S3_OUI_CARD;
    u->hover = -1;
    s3_oui_set_lord(u, lord);
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
            array_enter(u, i);
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
static void txt3(S3OfficerUI *u, Sango3Canvas *cv, const char *s, int x, int y,
                 int w, int h, int r, int g, int b, int centered) {
    txt(u, cv, s, x, y, w, h, ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b, centered);
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

static int big_pages(const S3OfficerUI *u) {
    return (u->n_list + BIG_ROWS - 1) / BIG_ROWS;
}

/* ---- 武将名单 ---- */
static void render_pick(S3OfficerUI *u, Sango3Canvas *cv) {
    rect(cv, 0, 0, AR_W, AR_H, C_MASK);
    rect(cv, PK_X, PK_Y, PK_W, PK_H, C_PANEL);
    frame(cv, PK_X, PK_Y, PK_W, PK_H);
    txt(u, cv, "整備 · 選擇武將", PK_X + 10, PK_Y + 4, PK_W - 20, 26, C_TITLE, 0);
    {
        char b[64];
        snprintf(b, sizeof b, "我方 %d 人", u->n_pick);
        txt(u, cv, b, PK_X + PK_W - 130, PK_Y + 4, 120, 26, C_DIM, 1);
    }
    /* 列头 */
    txt(u, cv, "姓名", PK_X + 12,  PK_HDR_Y, 90, 20, C_TITLE, 0);
    txt(u, cv, "官位", PK_X + 104, PK_HDR_Y, 96, 20, C_TITLE, 0);
    txt(u, cv, "等級", PK_X + 204, PK_HDR_Y, 46, 20, C_TITLE, 0);
    txt(u, cv, "所在城", PK_X + 252, PK_HDR_Y, 90, 20, C_TITLE, 0);
    txt(u, cv, "已學技", PK_X + 344, PK_HDR_Y, 126, 20, C_TITLE, 0);

    const int pages = (u->n_pick + PK_ROWS - 1) / PK_ROWS;
    if (u->pick_page >= pages && pages > 0) u->pick_page = pages - 1;
    for (int r = 0; r < PK_ROWS; ++r) {
        const int i = u->pick_page * PK_ROWS + r;
        if (i >= u->n_pick) break;
        const S3Officer *o = s3_roster_at(u->roster, u->pick_idx[i]);
        if (!o) continue;
        const int ry = PK_ROWS_Y + r * PK_ROW;
        if (u->pick_hover == r) rect(cv, PK_X + 3, ry, PK_W - 6, PK_ROW - 2, C_ROW);
        txt(u, cv, o->name, PK_X + 12, ry, 90, PK_ROW - 2, C_TEXT, 0);
        txt(u, cv, o->rank_name[0] ? o->rank_name : "—", PK_X + 104, ry, 96, PK_ROW - 2,
            o->rank_name[0] ? C_TEXT : C_DIM, 0);
        char b[48];
        snprintf(b, sizeof b, "%d", o->level);
        txt(u, cv, b, PK_X + 204, ry, 46, PK_ROW - 2, C_TEXT, 0);
        txt(u, cv, o->city[0] ? o->city : "—", PK_X + 252, ry, 90, PK_ROW - 2, C_DIM, 0);
        snprintf(b, sizeof b, "%d + %d", o->n_learn_bf, o->n_learn_sf);
        txt(u, cv, b, PK_X + 344, ry, 60, PK_ROW - 2, C_KNOWN, 0);
        txt(u, cv, "武 · 軍", PK_X + 404, ry, 56, PK_ROW - 2, C_DIM, 0);
    }
    if (u->n_pick == 0)
        txt(u, cv, "（我方尚無武將）", PK_X + 12, PK_ROWS_Y, PK_W - 24, PK_ROW, C_NO, 0);

    /* 翻页 */
    rect(cv, PK_X + 8, PK_PAGE_Y, PK_BTN_W, PK_PAGE_H,
         u->pick_hover == 100 ? C_HOVER : C_BTN);
    txt(u, cv, "上頁", PK_X + 8, PK_PAGE_Y, PK_BTN_W, PK_PAGE_H, C_TEXT, 1);
    rect(cv, PK_X + 84, PK_PAGE_Y, PK_BTN_W, PK_PAGE_H,
         u->pick_hover == 101 ? C_HOVER : C_BTN);
    txt(u, cv, "下頁", PK_X + 84, PK_PAGE_Y, PK_BTN_W, PK_PAGE_H, C_TEXT, 1);
    {
        char b[64];
        snprintf(b, sizeof b, "第 %d / %d 頁", u->pick_page + 1, pages > 0 ? pages : 1);
        txt(u, cv, b, PK_X + PK_W - 200, PK_PAGE_Y, 190, PK_PAGE_H, C_TITLE, 2);
    }
    txt(u, cv, u->msg, PK_X + 8, PK_Y + PK_H - 24, PK_W - 16, 20, C_DIM, 0);
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

    /* ---------------- 武将名单 ---------------- */
    if (u->mode == S3_OUI_PICK) { render_pick(u, cv); return; }

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

    /* 「名單」+ 9101/9102 ← → */
    rect(cv, LST_X, NAV_Y, LST_W, NAV_H, C_BTN);
    txt(u, cv, "名單", LST_X, NAV_Y, LST_W, NAV_H, C_TITLE, 1);
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

    /* 子選單（页签 0~2） / 技表（页签 3~4） */
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
        const int is_sf = (u->tab == 4);
        rect(cv, BIG_X, BIG_Y, BIG_W, BIG_H, C_PANEL);
        frame(cv, BIG_X, BIG_Y, BIG_W, BIG_H);

        /* 筛选条：全部 / 已學 / 可學（点选切换）+ 三类计数 */
        {
            static const char *L[3] = { "全部", "已學", "可學" };
            for (int f = 0; f < 3; ++f) {
                const int bx = BIG_FB_X + f * (BIG_FB_W + BIG_FB_GAP);
                const int on = (u->filt == f);
                rect(cv, bx, BIG_FILT_Y, BIG_FB_W, BIG_FILT_H - 2,
                     on ? C_BTN_ON : C_BTN);
                txt(u, cv, L[f], bx, BIG_FILT_Y, BIG_FB_W, BIG_FILT_H - 2,
                    on ? C_TITLE : C_DIM, 1);
            }
            char b[64];
            snprintf(b, sizeof b, "%s 共%d · 已學%d · 可學%d",
                     is_sf ? "軍師技" : "武將技",
                     u->count_all, u->count_known, u->count_open);
            txt(u, cv, b, BIG_CNT_X, BIG_FILT_Y, BIG_X + BIG_W - 6 - BIG_CNT_X,
                BIG_FILT_H - 2, C_KNOWN, 0);
        }

        /* 列头 */
        txt(u, cv, "名稱", BIG_X + 8,   BIG_HDR_Y, 116, BIG_HDR_H, C_TITLE, 0);
        txt(u, cv, "等級", BIG_X + 128, BIG_HDR_Y, 36,  BIG_HDR_H, C_TITLE, 0);
        txt(u, cv, "MP",   BIG_X + 168, BIG_HDR_Y, 36,  BIG_HDR_H, C_TITLE, 0);
        txt(u, cv, "所需功勳", BIG_X + 206, BIG_HDR_Y, 70, BIG_HDR_H, C_TITLE, 0);
        txt(u, cv, "狀態", BIG_X + 280, BIG_HDR_Y, 78, BIG_HDR_H, C_TITLE, 0);

        const int pages = big_pages(u);
        if (u->page >= pages && pages > 0) u->page = pages - 1;
        for (int r = 0; r < BIG_ROWS; ++r) {
            const int i = u->page * BIG_ROWS + r;
            if (i >= u->n_list) break;
            const S3Magic *m = u->list[i];
            const int st = u->list_st[i];
            const int ry = BIG_ROWS_Y + r * BIG_ROW;
            if (u->hover == r) rect(cv, BIG_X + 3, ry, BIG_W - 6, BIG_ROW - 2, C_ROW);
            if (u->sel == i)   rect(cv, BIG_X + 3, ry, BIG_W - 6, BIG_ROW - 2, C_SEL);
            uint32_t c = st_color(st);
            if (u->hover == r) c = C_HOVER;
            char b[64];
            snprintf(b, sizeof b, "%s", m->name);
            txt(u, cv, b, BIG_X + 8, ry, 116, BIG_ROW - 2, c, 0);
            snprintf(b, sizeof b, "%d", m->level);
            txt(u, cv, b, BIG_X + 128, ry, 36, BIG_ROW - 2, C_TEXT, 0);
            snprintf(b, sizeof b, "%d", m->mp);
            txt(u, cv, b, BIG_X + 168, ry, 36, BIG_ROW - 2, C_TEXT, 0);
            if (st == ST_KNOWN) snprintf(b, sizeof b, "—");
            else                snprintf(b, sizeof b, "%d", m->contribution);
            txt(u, cv, b, BIG_X + 206, ry, 70, BIG_ROW - 2, st == ST_KNOWN ? C_DIM : c, 0);
            txt(u, cv, st_label(st), BIG_X + 280, ry, 78, BIG_ROW - 2, c, 0);
        }
        if (u->n_list == 0)
            txt(u, cv, u->filt == FILT_KNOWN ? "（尚無已學之技）"
                                            : "（目前無可學之技）",
                BIG_X + 8, BIG_ROWS_Y, BIG_W - 16, BIG_ROW, C_DIM, 0);

        /* 翻页 */
        rect(cv, BIG_X + 8, BIG_PAGE_Y, 56, BIG_PAGE_H,
             u->hover == 100 ? C_HOVER : C_BTN);
        txt(u, cv, "上頁", BIG_X + 8, BIG_PAGE_Y, 56, BIG_PAGE_H, C_TEXT, 1);
        rect(cv, BIG_X + 68, BIG_PAGE_Y, 56, BIG_PAGE_H,
             u->hover == 101 ? C_HOVER : C_BTN);
        txt(u, cv, "下頁", BIG_X + 68, BIG_PAGE_Y, 56, BIG_PAGE_H, C_TEXT, 1);
        {
            char b[64];
            snprintf(b, sizeof b, "第 %d / %d 頁 · 點行為學習/查看",
                     u->page + 1, pages > 0 ? pages : 1);
            txt(u, cv, b, BIG_X + 130, BIG_PAGE_Y, 230, BIG_PAGE_H, C_DIM, 0);
        }
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
    if (u->msg[0]) txt(u, cv, u->msg, MSG_X, MSG_Y, MSG_W, MSG_H, C_TITLE, 0);
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
    if (!u) return;
    if (u->mode == S3_OUI_PICK) {
        u->pick_hover = -1;
        for (int r = 0; r < PK_ROWS; ++r) {
            const int i = u->pick_page * PK_ROWS + r;
            if (i >= u->n_pick) break;
            if (inbox(x, y, PK_X + 3, PK_ROWS_Y + r * PK_ROW, PK_W - 6, PK_ROW - 2)) {
                u->pick_hover = r; return;
            }
        }
        if (inbox(x, y, PK_X + 8, PK_PAGE_Y, PK_BTN_W, PK_PAGE_H)) { u->pick_hover = 100; return; }
        if (inbox(x, y, PK_X + 84, PK_PAGE_Y, PK_BTN_W, PK_PAGE_H)) { u->pick_hover = 101; return; }
        return;
    }
    if (u->mode != S3_OUI_ARRAY) return;
    u->hover = -1;
    const S3Officer *o = (u->roster && u->off >= 0) ? s3_roster_at(u->roster, u->off) : NULL;
    if (!o) return;
    if (u->tab <= 2) {
        const int n = sub_count(u, o);
        for (int i = 0; i < n; ++i) {
            const int ry = SUB_Y + 4 + i * SUB_ROW;
            if (inbox(x, y, SUB_X + 3, ry, SUB_W - 6, SUB_ROW - 2)) { u->hover = i; return; }
        }
        return;
    }
    for (int r = 0; r < BIG_ROWS; ++r) {
        const int i = u->page * BIG_ROWS + r;
        if (i >= u->n_list) break;
        if (inbox(x, y, BIG_X + 3, BIG_ROWS_Y + r * BIG_ROW, BIG_W - 6, BIG_ROW - 2)) {
            u->hover = r; return;
        }
    }
    if (inbox(x, y, BIG_X + 8, BIG_PAGE_Y, 56, BIG_PAGE_H))   { u->hover = 100; return; }
    if (inbox(x, y, BIG_X + 68, BIG_PAGE_Y, 56, BIG_PAGE_H))  { u->hover = 101; return; }
}

/* 「未達」原因（等级 / 武力区间 / 智力区间，半开区间与 s3_magic_learnable 一致） */
static void lock_reason(const S3Officer *o, const S3Magic *m, char *out, size_t cap) {
    int p = 0;
    out[0] = '\0';
    p += snprintf(out + p, cap - (size_t)p, "未達：");
    if (m->level > o->level) p += snprintf(out + p, cap - (size_t)p, "需等級 %d ", m->level);
    if (m->str_up > m->str_down && !(m->str_down <= o->str && o->str < m->str_up))
        p += snprintf(out + p, cap - (size_t)p, "需武力 %d~%d ", m->str_down, m->str_up - 1);
    if (m->int_up > m->int_down && !(m->int_down <= o->intel && o->intel < m->int_up))
        p += snprintf(out + p, cap - (size_t)p, "需智力 %d~%d ", m->int_down, m->int_up - 1);
    if (p > 0 && out[p - 1] == ' ') out[p - 1] = '\0';
    snprintf(out + p, cap - (size_t)p, "（現：等級 %d 武力 %d 智力 %d）",
             o->level, o->str, o->intel);
}

int s3_oui_on_rclick(S3OfficerUI *u) {
    if (!u || u->mode == S3_OUI_NONE) return 0;
    if (u->mode == S3_OUI_ARRAY) {          /* 整备页 → 回名单（仍激活） */
        s3_oui_open_pick(u, u->roster);
        return 1;
    }
    s3_oui_close(u);                        /* 名单/信息块 → 关闭 */
    return 0;
}

int s3_oui_on_click(S3OfficerUI *u, int32_t x, int32_t y) {
    if (!u || u->mode == S3_OUI_NONE) return -1;
    if (u->mode == S3_OUI_CARD) { s3_oui_close(u); return -2; }

    /* ---------------- 武将名单 ---------------- */
    if (u->mode == S3_OUI_PICK) {
        const int pages = (u->n_pick + PK_ROWS - 1) / PK_ROWS;
        if (inbox(x, y, PK_X + 8, PK_PAGE_Y, PK_BTN_W, PK_PAGE_H)) {
            if (u->pick_page > 0) --u->pick_page;
            return -3;
        }
        if (inbox(x, y, PK_X + 84, PK_PAGE_Y, PK_BTN_W, PK_PAGE_H)) {
            if (u->pick_page + 1 < pages) ++u->pick_page;
            return -3;
        }
        for (int r = 0; r < PK_ROWS; ++r) {
            const int i = u->pick_page * PK_ROWS + r;
            if (i >= u->n_pick) break;
            if (inbox(x, y, PK_X + 3, PK_ROWS_Y + r * PK_ROW, PK_W - 6, PK_ROW - 2)) {
                array_enter(u, u->pick_idx[i]);     /* 选人 → 进整备页 */
                return 0;
            }
        }
        if (!inbox(x, y, PK_X, PK_Y, PK_W, PK_H)) { s3_oui_close(u); return -2; }
        return -3;                                  /* 面板内空白：吞掉 */
    }

    /* ---------------- 整备页 ---------------- */
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

    /* 「名單」/ 9101 / 9102 */
    if (inbox(x, y, LST_X, NAV_Y, LST_W, NAV_H)) {
        s3_oui_open_pick(u, u->roster);
        return -3;
    }
    if (inbox(x, y, NAV_X1, NAV_Y, NAV_W, NAV_H)) { step_officer(u, -1); return 0; }
    if (inbox(x, y, NAV_X2, NAV_Y, NAV_W, NAV_H)) { step_officer(u, +1); return 0; }

    /* 9301~9305 五页签 */
    for (int t = 0; t < TAB_COUNT; ++t) {
        const int by = OPT_Y + 6 + t * OPT_IH;
        if (inbox(x, y, OPT_X + 6, by, OPT_W - 12, OPT_BH)) {
            u->tab = t;
            rebuild_list(u);
            if (t >= 3)
                snprintf(u->msg, sizeof u->msg, "%s：共 %d · 已學 %d · 可學 %d（功勳 %d）",
                         tab_label(t), u->count_all, u->count_known, u->count_open, o->merit);
            else
                snprintf(u->msg, sizeof u->msg, "%s：%d 項（變更待接）",
                         tab_label(t), sub_count(u, o));
            return -3;
        }
    }

    /* 子選單 / 技表 */
    if (u->tab <= 2) {
        const int n = sub_count(u, o);
        for (int i = 0; i < n; ++i) {
            const int ry = SUB_Y + 4 + i * SUB_ROW;
            if (inbox(x, y, SUB_X + 3, ry, SUB_W - 6, SUB_ROW - 2)) {
                u->sel = i;
                if (u->tab == 2) snprintf(u->msg, sizeof u->msg, "必殺技：%s（開局自帶）", u->sa_name[i]);
                else snprintf(u->msg, sizeof u->msg, "%s：%s（變更待接）", tab_label(u->tab),
                              (u->tab == 0 && u->T) ? u->T->form_names[i]
                                                    : (u->T ? u->T->soldier_names[i] : "?"));
                return -3;
            }
        }
    } else {
        /* 筛选条 */
        {
            for (int f = 0; f < 3; ++f) {
                const int bx = BIG_FB_X + f * (BIG_FB_W + BIG_FB_GAP);
                if (inbox(x, y, bx, BIG_FILT_Y, BIG_FB_W, BIG_FILT_H - 2)) {
                    u->filt = f;
                    rebuild_list(u);
                    snprintf(u->msg, sizeof u->msg, "篩選：%s（%d 項）",
                             f == FILT_ALL ? "全部" : f == FILT_KNOWN ? "已學" : "可學",
                             u->n_list);
                    return -3;
                }
            }
        }
        /* 翻页 */
        if (inbox(x, y, BIG_X + 8, BIG_PAGE_Y, 56, BIG_PAGE_H)) {
            if (u->page > 0) --u->page;
            return -3;
        }
        if (inbox(x, y, BIG_X + 68, BIG_PAGE_Y, 56, BIG_PAGE_H)) {
            if (u->page + 1 < big_pages(u)) ++u->page;
            return -3;
        }
        /* 数据行 */
        for (int r = 0; r < BIG_ROWS; ++r) {
            const int i = u->page * BIG_ROWS + r;
            if (i >= u->n_list) break;
            if (!inbox(x, y, BIG_X + 3, BIG_ROWS_Y + r * BIG_ROW, BIG_W - 6, BIG_ROW - 2))
                continue;
            const S3Magic *m = u->list[i];
            const int st = u->list_st[i];
            switch (st) {
            case ST_OK:
                u->sel = i; u->confirm = 1;
                snprintf(u->msg, sizeof u->msg, "學習%s？所需功勳：%d", m->name, m->contribution);
                break;
            case ST_POOR:
                u->sel = -1;
                snprintf(u->msg, sizeof u->msg, "功勳不足：%s 需 %d，目前 %d",
                         m->name, m->contribution, o->merit);
                break;
            case ST_KNOWN:
                u->sel = -1;
                snprintf(u->msg, sizeof u->msg, "已學會：%s（等級 %d · MP %d）",
                         m->name, m->level, m->mp);
                break;
            default:
                u->sel = -1;
                lock_reason(o, m, u->msg, sizeof u->msg);
                break;
            }
            return -3;
        }
    }
    return -3;      /* 面板内其它位置：吞掉点击，不落到朝堂 */
}
