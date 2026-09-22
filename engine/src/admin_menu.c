/*
 * admin_menu.c —— 行政主選單实现（见 admin_menu.h）
 */
#include "admin_menu.h"
#include "shp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 命令表：组数/项数取自原版 Menu.ini（8011~8017 的 rows 字段），命令清单
 * 由用户对照原版核对（2026-09-16 两轮，docs/城池信息面板与行政菜单.md 第五节）：
 *   內政 5 · 軍政 5 · 外交 2 · 任免 3 · 計略 3 · 系統 4 · 休息 1
 * （首輪用戶漏了「物品」，第二輪補上 → 與 MenuFrame05 吻合 ✓）
 * 命令名原版由代码填充（INI 无），此处按 Setting\Text.ini 的文案线索自定。 */
typedef struct {
    const char *label;      /* 按钮文案（原版烘焙在 AD\Btn\*.shp 里，此字段仅供日志/提示） */
    const char *icon;       /* Shape\AD\Btn\<icon>[2|3].shp */
    int         n_items;
    const char *items[S3_ADMIN_MAX_ITEMS];
} AdminGroup;

static const AdminGroup GROUPS[S3_ADMIN_GROUPS] = {
    { "內政", "Interior",  5, { "移動", "搜索", "開發", "人才", "物品" } },
    { "軍政", "Military",  5, { "徵兵", "訓練", "戰爭", "整備", "調兵" } },
    { "外交", "Diplomacy", 2, { "同盟", "離間" } },
    { "任免", "Appoint",   3, { "太守", "軍師", "將軍" } },
    { "計略", "Strategy",  3, { "調查", "離間", "情報" } },
    { "系統", "System",    4, { "進度存檔", "進度讀取", "設定調整", "回主選單" } },
    { "休息", "Rest",      1, { "確定" } },
};

/* 原版几何（相对 8000 左上角，逻辑像素） */
#define BTN_W        121
#define BTN_H        29
#define BTN_PITCH    33          /* 按钮纵向间距（0,33,66,99,132,165,198） */
#define SUB_X        134         /* 子选单相对 x */
#define SUB_Y0       6           /* 第一个子选单的 y，之后 = SUB_Y0 + BTN_PITCH*i */
#define FRAME_PAD    14          /* 框内首行偏移（实测 MenuFrame01 高 55 = 29 + 26） */
#define FRAME_ROW_H  26          /* 每项行高（MenuFrame05 高 159 = 29 + 26×5） */

#define COL_TEXT     0xDCDCDCu   /* FColor 5001 = 220,220,220 */
#define COL_HOVER    0xD25915u   /* FColor 5002 focus = 210,89,21 */

typedef struct { ShpImage img; int ok, tried; } AdminImg;

struct S3AdminMenu {
    S3AdminReadAsset read_asset; void *asset_ud;
    S3AdminDrawText  draw_text;  void *text_ud;
    S3AdminCmd       on_cmd;     void *cmd_ud;

    AdminImg btn[S3_ADMIN_GROUPS][3];   /* 三态：0 Normal / 1 Focus(悬停) / 2 Down(展开) */
    AdminImg frame[6];                  /* MenuFrame01..06（项数 1..6） */

    int32_t  ox, oy, zoom;
    int      visible, open;             /* open = 当前展开的组，-1 未展开 */
    int      hover_btn, hover_item;
    char     hint[192];
};

/* ------------------------------------------------------------- 懒加载素材 */
static ShpImage *img_get(AdminImg *slot, S3AdminMenu *m, const char *path) {
    if (slot->ok) return &slot->img;
    if (slot->tried) return NULL;
    slot->tried = 1;
    if (!m->read_asset) return NULL;
    uint32_t len = 0;
    uint8_t *raw = m->read_asset(m->asset_ud, path, &len);
    if (!raw) return NULL;
    const char *err = NULL;
    int ok = shp_decode(raw, len, &slot->img, &err);
    free(raw);
    if (!ok) {
        printf("WARN : admin asset decode failed: %s (%s)\n", path, err ? err : "?");
        return NULL;
    }
    slot->ok = 1;
    return &slot->img;
}

/* 按钮图：优先取所需态，缺失回退 Normal */
static ShpImage *btn_img(S3AdminMenu *m, int g, int state) {
    static const char *SUF[3] = { "", "2", "3" };
    char path[96];
    for (int s = state; s >= 0; --s) {
        snprintf(path, sizeof path, "Shape\\AD\\Btn\\%s%s.shp", GROUPS[g].icon, SUF[s]);
        ShpImage *im = img_get(&m->btn[g][s], m, path);
        if (im) return im;
    }
    return NULL;
}

static ShpImage *frame_img(S3AdminMenu *m, int n_items) {
    int n = n_items;
    if (n < 1) n = 1;
    if (n > 6) n = 6;
    char path[96];
    snprintf(path, sizeof path, "Shape\\AD\\Base\\MenuFrame%02d.shp", n);
    return img_get(&m->frame[n - 1], m, path);
}

/* ------------------------------------------------------------------ 几何 */
static void btn_rect(const S3AdminMenu *m, int g, int32_t *x, int32_t *y,
                     int32_t *w, int32_t *h) {
    *x = m->ox;
    *y = m->oy + g * BTN_PITCH * m->zoom;
    *w = BTN_W * m->zoom;
    *h = BTN_H * m->zoom;
}

static void sub_rect(const S3AdminMenu *m, int g, int32_t *x, int32_t *y,
                     int32_t *w, int32_t *h) {
    int n = GROUPS[g].n_items;
    ShpImage *f = frame_img((S3AdminMenu *)m, n);
    int32_t fw = f ? (int32_t)f->width  : 115;
    int32_t fh = f ? (int32_t)f->height : (FRAME_PAD * 2 + FRAME_ROW_H * n);
    *x = m->ox + SUB_X * m->zoom;
    /* 子选单一律从**菜单顶部**展开（2026-09-16 用户反馈：原来跟随所属按钮的 y，
     * 靠下的组——如系統——会把子选单顶出屏幕外，命令看不见）。 */
    *y = m->oy;
    *w = fw * m->zoom;
    *h = fh * m->zoom;
}

static void item_rect(const S3AdminMenu *m, int g, int i, int32_t *x, int32_t *y,
                      int32_t *w, int32_t *h) {
    int32_t sx, sy, sw, sh;
    sub_rect(m, g, &sx, &sy, &sw, &sh);
    *x = sx;
    *y = sy + (FRAME_PAD + FRAME_ROW_H * i) * m->zoom;
    *w = sw;
    *h = FRAME_ROW_H * m->zoom;
}

/* ------------------------------------------------------------------ 生命周期 */
S3AdminMenu *s3_admin_new(S3AdminReadAsset read_asset, void *asset_ud,
                          S3AdminDrawText draw_text, void *text_ud,
                          S3AdminCmd on_cmd, void *cmd_ud) {
    S3AdminMenu *m = (S3AdminMenu *)calloc(1, sizeof *m);
    if (!m) return NULL;
    m->read_asset = read_asset; m->asset_ud = asset_ud;
    m->draw_text = draw_text;   m->text_ud = text_ud;
    m->on_cmd = on_cmd;         m->cmd_ud = cmd_ud;
    m->zoom = 2;                /* 战略层画布是地图原生分辨率 → 放大一档 */
    m->open = -1;
    m->hover_btn = m->hover_item = -1;
    m->visible = 1;
    return m;
}

void s3_admin_free(S3AdminMenu *m) {
    if (!m) return;
    for (int g = 0; g < S3_ADMIN_GROUPS; ++g)
        for (int s = 0; s < 3; ++s) if (m->btn[g][s].ok) shp_free(&m->btn[g][s].img);
    for (int i = 0; i < 6; ++i) if (m->frame[i].ok) shp_free(&m->frame[i].img);
    free(m);
}

void s3_admin_set_visible(S3AdminMenu *m, int vis) { if (m) m->visible = vis; }
int  s3_admin_visible(const S3AdminMenu *m) { return m ? m->visible : 0; }

/* 收起子选单（并把 hover 一并复位）。
 * 用途：离开朝堂/进命令阶段收起菜单时**同时**收起子选单，
 * 避免"回到朝堂时子选单还展着、一级按钮还高亮"（2026-09-22 用户实测指正）。 */
void s3_admin_collapse(S3AdminMenu *m) {
    if (!m) return;
    m->open = -1;
    m->hover_item = -1;
}
void s3_admin_set_zoom(S3AdminMenu *m, int32_t zoom) {
    if (m && zoom >= 1 && zoom <= 4) m->zoom = zoom;
}
void s3_admin_set_origin(S3AdminMenu *m, int32_t x, int32_t y) {
    if (m) { m->ox = x; m->oy = y; }
}
void s3_admin_set_hint(S3AdminMenu *m, const char *hint) {
    if (m) snprintf(m->hint, sizeof m->hint, "%s", hint ? hint : "");
}
int         s3_admin_open_group(const S3AdminMenu *m) { return m ? m->open : -1; }
const char *s3_admin_hint(const S3AdminMenu *m) { return m ? m->hint : ""; }
const char *s3_admin_group_label(int g) {
    return (g >= 0 && g < S3_ADMIN_GROUPS) ? GROUPS[g].label : "?";
}
int s3_admin_group_items(int g) {
    return (g >= 0 && g < S3_ADMIN_GROUPS) ? GROUPS[g].n_items : 0;
}
const char *s3_admin_item_label(int group, int item) {
    if (group < 0 || group >= S3_ADMIN_GROUPS) return NULL;
    if (item < 0 || item >= GROUPS[group].n_items) return NULL;
    return GROUPS[group].items[item];
}

/* ------------------------------------------------------------------ 渲染 */
void s3_admin_render(S3AdminMenu *m, Sango3Canvas *cv) {
    if (!m || !cv || !m->visible) return;

    /* 七个按钮（文案烘焙在素材内，无需绘字） */
    for (int g = 0; g < S3_ADMIN_GROUPS; ++g) {
        int32_t x, y, w, h;
        btn_rect(m, g, &x, &y, &w, &h);
        int state = (m->hover_btn == g) ? 2 : (m->open == g ? 1 : 0);
        ShpImage *im = btn_img(m, g, state);
        if (im) sango3_canvas_blit(cv, im->rgba, (int32_t)im->width,
                                   (int32_t)im->height, x, y, m->zoom);
        else sango3_canvas_frame(cv, x, y, w, h, 1, 160, 140, 90);   /* 素材缺失兜底 */
    }

    /* 展开的子选单：先铺行政視窗底色（原版 COLOR 5003 = 21,89,210），再贴金框。
     * 框素材大面积透明，直接贴在地图上几乎看不清（2026-09-16 用户实测反馈）。 */
    if (m->open >= 0) {
        int32_t sx, sy, sw, sh;
        sub_rect(m, m->open, &sx, &sy, &sw, &sh);
        sango3_canvas_fill(cv, sx, sy, sw, sh, 21, 89, 210);
        ShpImage *f = frame_img(m, GROUPS[m->open].n_items);
        if (f) sango3_canvas_blit(cv, f->rgba, (int32_t)f->width, (int32_t)f->height,
                                  sx, sy, m->zoom);
        if (m->draw_text) {
            for (int i = 0; i < GROUPS[m->open].n_items; ++i) {
                int32_t ix, iy, iw, ih;
                item_rect(m, m->open, i, &ix, &iy, &iw, &ih);
                uint32_t rgb = (m->hover_item == i) ? COL_HOVER : COL_TEXT;
                m->draw_text(m->text_ud, cv, GROUPS[m->open].items[i],
                             ix + 10 * m->zoom, iy, iw - 12 * m->zoom, ih,
                             rgb, 2, 0x4u /* S3_WS_VCENTER */);
            }
        }
    }

    /* 提示（最近一次命令的结果）：深色底 + 文本，否则压在地图上同样看不清 */
    if (m->hint[0] && m->draw_text) {
        int32_t hx = m->ox + SUB_X * m->zoom;
        int32_t hy = m->oy + (SUB_Y0 + BTN_PITCH * S3_ADMIN_GROUPS + 6) * m->zoom;
        int32_t hw = 460 * m->zoom / 2, hh = 30 * m->zoom / 2;
        sango3_canvas_fill(cv, hx, hy, hw, hh, 12, 14, 24);
        sango3_canvas_frame(cv, hx, hy, hw, hh, 1, 150, 130, 80);
        m->draw_text(m->text_ud, cv, m->hint, hx + 8, hy, hw - 16, hh, 0xF0DCA0u, 1, 0x4u);
    }
}

/* ------------------------------------------------------------------ 命中 */
int s3_admin_hit(const S3AdminMenu *m, int32_t x, int32_t y) {
    if (!m || !m->visible) return 0;
    for (int g = 0; g < S3_ADMIN_GROUPS; ++g) {
        int32_t bx, by, bw, bh;
        btn_rect(m, g, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) return 1;
    }
    if (m->open >= 0) {
        int32_t sx, sy, sw, sh;
        sub_rect(m, m->open, &sx, &sy, &sw, &sh);
        if (x >= sx && x < sx + sw && y >= sy && y < sy + sh) return 1;
    }
    return 0;
}

void s3_admin_on_move(S3AdminMenu *m, int32_t x, int32_t y) {
    if (!m || !m->visible) return;
    m->hover_btn = m->hover_item = -1;
    for (int g = 0; g < S3_ADMIN_GROUPS; ++g) {
        int32_t bx, by, bw, bh;
        btn_rect(m, g, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) { m->hover_btn = g; return; }
    }
    if (m->open >= 0) {
        for (int i = 0; i < GROUPS[m->open].n_items; ++i) {
            int32_t ix, iy, iw, ih;
            item_rect(m, m->open, i, &ix, &iy, &iw, &ih);
            if (x >= ix && x < ix + iw && y >= iy && y < iy + ih) { m->hover_item = i; return; }
        }
    }
}

int s3_admin_on_click(S3AdminMenu *m, int32_t x, int32_t y) {
    if (!m || !m->visible) return 0;
    /* 先判按钮：点同一按钮 = 收起，点别的 = 切换展开 */
    for (int g = 0; g < S3_ADMIN_GROUPS; ++g) {
        int32_t bx, by, bw, bh;
        btn_rect(m, g, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            m->open = (m->open == g) ? -1 : g;
            m->hover_item = -1;
            return 1;
        }
    }
    /* 再判子选单项 */
    if (m->open >= 0) {
        for (int i = 0; i < GROUPS[m->open].n_items; ++i) {
            int32_t ix, iy, iw, ih;
            item_rect(m, m->open, i, &ix, &iy, &iw, &ih);
            if (x >= ix && x < ix + iw && y >= iy && y < iy + ih) {
                const int g = m->open;                  /* 派发前先记住组号（下面会复位 open） */
                const char *label = GROUPS[g].items[i];
                int done = 0;
                if (m->on_cmd) done = m->on_cmd(m->cmd_ud, g, i, label);
                /* ★ 选完即收起子选单（2026-09-22 用户实测指正）：
                 * 原先只派发命令、不复位 `open` → 命令**取消**（长按/返回）回到朝堂时，
                 * 子选单仍展开、一级按钮仍停在 `open == g` 的高亮态（"没点选却展着"）。
                 * 原版行为也是"选中一项后子选单收起"，故此处统一收起。 */
                m->open = -1;
                m->hover_item = -1;
                if (!done) {
                    char b[160];
                    snprintf(b, sizeof b, "%s「%s」尚未實現", GROUPS[g].label, label);
                    snprintf(m->hint, sizeof m->hint, "%s", b);
                }
                return 1;
            }
        }
        int32_t sx, sy, sw, sh;
        sub_rect(m, m->open, &sx, &sy, &sw, &sh);
        if (x >= sx && x < sx + sw && y >= sy && y < sy + sh) return 1;   /* 框内空白 */
    }
    /* 兜底：**点空白处 = 收起已展开的子选单**（2026-09-22 用户实测指正 —— 上一版只修了
     * "选完即收"与"取消回朝堂"，漏了"点开一级按钮后点空白"这条路）。
     * 与"点同一个一级按钮"同效；没有展开任何组时保持 no-op（返回 0 交上层）。
     * 安全性：大地图路径先经 s3_admin_hit()（内含 visible 判定）才会调到这里，
     * 菜单在地图上是隐藏的 → 不会吃掉选城的点击。 */
    if (m->open >= 0) {
        m->open = -1;
        m->hover_item = -1;
        return 1;
    }
    return 0;
}
