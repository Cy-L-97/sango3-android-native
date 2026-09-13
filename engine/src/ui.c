/*
 * ui.c —— UI 布局解析（M2）
 *
 * 两个必须与原版一致、又容易写错的点：
 *   ① 行内注释：ini.c 按「整行注释」处理，值里仍残留 `; 淡藍` 这类行内注释
 *      （例：Normal = 21,89,210,1\t\t; 淡藍）。本模块取值后统一 strip_inline()。
 *   ② Style 混有笔误 token：`wcIcon`（应为 wsIcon）在原版里出现十几次，
 *      另有 `Style = Class = WND_CLASS_XXX` 这类把下一行写进来的脏数据 → 未知 token 一律忽略。
 */
#include "ui.h"

#include <stdlib.h>
#include <string.h>

static char *s3_dup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *r = (char *)malloc(n);
    if (r) memcpy(r, s, n);
    return r;
}

/* 去掉行内注释（';' 或 '#' 之后）并去首尾空白 */
static void strip_inline(const char *v, char *out, size_t cap) {
    out[0] = '\0';
    if (!v) return;
    size_t i = 0;
    while (v[i] && v[i] != ';' && v[i] != '#') ++i;
    size_t n = i;
    while (n > 0 && (v[n - 1] == ' ' || v[n - 1] == '\t' || v[n - 1] == '\r')) --n;
    size_t s = 0;
    while (s < n && (v[s] == ' ' || v[s] == '\t')) ++s;
    n = (n > s) ? n - s : 0;
    if (n >= cap) n = cap - 1;
    memcpy(out, v + s, n);
    out[n] = '\0';
}

/* 取 key 的值（去行内注释）。tmp 由调用方提供（同一缓冲勿跨调用持有）。 */
static const char *val_clean(const S3IniSection *s, const char *key, char *tmp, size_t cap) {
    strip_inline(s3_ini_str(s, key, NULL), tmp, cap);
    return tmp;
}

/* 整串严格整数（去空白后必须全是数字） */
static int parse_int_clean(const char *v, int def) {
    if (!v || !*v) return def;
    int sign = 1;
    if (*v == '-') { sign = -1; ++v; }
    else if (*v == '+') ++v;
    if (*v < '0' || *v > '9') return def;
    long x = 0;
    while (*v >= '0' && *v <= '9') {
        x = x * 10 + (*v - '0');
        if (x > 2000000000L) x = 2000000000L;
        ++v;
    }
    while (*v == ' ' || *v == '\t') ++v;
    if (*v) return def;
    return (int)(sign * x);
}

/* "x, y, w, h" → S3Rect（必须恰好 4 段且后面无残留，否则失败返回 -1 并置 0）
 *
 * ⚠ 踩过的坑：段数计数必须在**解析成功时立即自增**（seg[n++]），
 *   不能用 `for (n = 0; n < 4; ++n)` 的循环自增——最后一段走 `break` 跳出时
 *   循环自增不执行，n 会停在 3，导致所有矩形被误判为解析失败、全部置 0。
 */
static int parse_rect_clean(const char *v, S3Rect *r) {
    r->x = r->y = r->w = r->h = 0;
    if (!v || !*v) return -1;
    int seg[4];
    int n = 0;
    const char *p = v;
    while (n < 4) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        int sign = 1;
        if (*p == '-') { sign = -1; ++p; }
        else if (*p == '+') ++p;
        if (*p < '0' || *p > '9') return -1;
        long x = 0;
        while (*p >= '0' && *p <= '9') {
            x = x * 10 + (*p - '0');
            if (x > 2000000000L) x = 2000000000L;
            ++p;
        }
        while (*p == ' ' || *p == '\t') ++p;
        seg[n++] = (int)(sign * x);
        if (*p == ',') { ++p; continue; }
        break;
    }
    if (n < 4 || *p) return -1;
    r->x = seg[0]; r->y = seg[1]; r->w = seg[2]; r->h = seg[3];
    return 0;
}

/* 四态通用：全数字则记段数，否则 n_seg=0 只保留原串（如 "Normal"） */
static S3UiState parse_state(const char *clean) {
    S3UiState st;
    memset(&st, 0, sizeof st);
    st.raw = s3_dup(clean ? clean : "");
    if (!clean || !*clean) return st;

    int seg[4], n = 0;
    const char *p = clean;
    while (n < 4) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        int sign = 1;
        if (*p == '-') { sign = -1; ++p; }
        else if (*p == '+') ++p;
        if (*p < '0' || *p > '9') return st;          /* 非数字串 → 保留 raw */
        long x = 0;
        while (*p >= '0' && *p <= '9') {
            x = x * 10 + (*p - '0');
            if (x > 2000000000L) x = 2000000000L;
            ++p;
        }
        while (*p == ' ' || *p == '\t') ++p;
        if (*p != ',' && *p != '\0') return st;        /* 段内多余字符 → 保留 raw */
        seg[n++] = (int)(sign * x);
        if (*p == ',') { ++p; continue; }
        break;
    }
    if (*p) return st;                                  /* 超过 4 段 → 保留 raw */
    st.n_seg = n;
    for (int i = 0; i < n; ++i) st.v[i] = seg[i];
    return st;
}

/* ------------------------------------------------------------ Style / Class */
static uint32_t style_bit(const char *t) {
    if (!t || !*t) return 0;
    if (!strcmp(t, "wsVisible")) return S3_WS_VISIBLE;
    if (!strcmp(t, "wsIcon") || !strcmp(t, "wcIcon")) return S3_WS_ICON;  /* wcIcon = 原版笔误 */
    if (!strcmp(t, "wsVCenter")) return S3_WS_VCENTER;
    if (!strcmp(t, "wsHCenter")) return S3_WS_HCENTER;
    if (!strcmp(t, "wsText")) return S3_WS_TEXT;
    if (!strcmp(t, "wsCheck")) return S3_WS_CHECK;
    if (!strcmp(t, "wsVScroll")) return S3_WS_VSCROLL;
    if (!strcmp(t, "wsHScroll")) return S3_WS_HSCROLL;
    if (!strcmp(t, "wsSCheck")) return S3_WS_SCHECK;
    if (!strcmp(t, "wsRight")) return S3_WS_RIGHT;
    if (!strcmp(t, "wsLeft")) return S3_WS_LEFT;
    if (!strcmp(t, "wsHorizontal")) return S3_WS_HORIZONTAL;
    if (!strcmp(t, "wsTrans")) return S3_WS_TRANS;
    if (!strcmp(t, "wsReport")) return S3_WS_REPORT;
    if (!strcmp(t, "wsForceSelectChange")) return S3_WS_FORCE_SELECT_CHANGE;
    if (!strcmp(t, "wsRight2Left")) return S3_WS_RIGHT2LEFT;
    if (!strcmp(t, "wsLeft2Right")) return S3_WS_LEFT2RIGHT;
    return 0;   /* 未知 token（含原版脏数据）忽略 */
}

uint32_t s3_ui_parse_style(const char *style_csv) {
    uint32_t m = 0;
    if (!style_csv) return 0;
    char tmp[1024];
    strip_inline(style_csv, tmp, sizeof tmp);
    char *p = tmp;
    while (*p) {
        char *c = strchr(p, ',');
        if (c) *c = '\0';
        while (*p == ' ' || *p == '\t') ++p;
        size_t n = strlen(p);
        while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t')) p[--n] = '\0';
        m |= style_bit(p);
        if (!c) break;
        p = c + 1;
    }
    return m;
}

S3WndClass s3_ui_parse_class(const char *cls) {
    if (!cls) return S3_WND_UNKNOWN;
    if (!strcmp(cls, "WND_CLASS_BASE")) return S3_WND_BASE;
    if (!strcmp(cls, "WND_CLASS_BUTTON")) return S3_WND_BUTTON;
    if (!strcmp(cls, "WND_CLASS_STATIC")) return S3_WND_STATIC;
    if (!strcmp(cls, "WND_CLASS_LIST")) return S3_WND_LIST;
    if (!strcmp(cls, "WND_CLASS_SCROLLBAR")) return S3_WND_SCROLLBAR;
    if (!strcmp(cls, "WND_CLASS_MSTATIC")) return S3_WND_MSTATIC;
    if (!strcmp(cls, "WND_CLASS_BUTTONREPORT")) return S3_WND_BUTTONREPORT;
    if (!strcmp(cls, "WND_CLASS_PROGRESS_BFHP")) return S3_WND_PROGRESS_BFHP;
    if (!strcmp(cls, "WND_CLASS_PROGRESS")) return S3_WND_PROGRESS;
    if (!strcmp(cls, "WND_CLASS_PROGRESSEX")) return S3_WND_PROGRESSEX;
    if (!strcmp(cls, "WND_CLASS_BFRADAR")) return S3_WND_BFRADAR;
    if (!strcmp(cls, "WND_CLASS_TIMER")) return S3_WND_TIMER;
    if (!strcmp(cls, "WND_CLASS_BFMESSAGE")) return S3_WND_BFMESSAGE;
    return S3_WND_UNKNOWN;
}

const char *s3_ui_class_name(S3WndClass c) {
    switch (c) {
        case S3_WND_BASE: return "WND_CLASS_BASE";
        case S3_WND_BUTTON: return "WND_CLASS_BUTTON";
        case S3_WND_STATIC: return "WND_CLASS_STATIC";
        case S3_WND_LIST: return "WND_CLASS_LIST";
        case S3_WND_SCROLLBAR: return "WND_CLASS_SCROLLBAR";
        case S3_WND_MSTATIC: return "WND_CLASS_MSTATIC";
        case S3_WND_BUTTONREPORT: return "WND_CLASS_BUTTONREPORT";
        case S3_WND_PROGRESS_BFHP: return "WND_CLASS_PROGRESS_BFHP";
        case S3_WND_PROGRESS: return "WND_CLASS_PROGRESS";
        case S3_WND_PROGRESSEX: return "WND_CLASS_PROGRESSEX";
        case S3_WND_BFRADAR: return "WND_CLASS_BFRADAR";
        case S3_WND_TIMER: return "WND_CLASS_TIMER";
        case S3_WND_BFMESSAGE: return "WND_CLASS_BFMESSAGE";
        default: return "WND_CLASS_UNKNOWN";
    }
}

/* ------------------------------------------------------------ 载入 */
int s3_ui_load(S3UiLayout *out, const S3Ini *ini) {
    if (!out || !ini) return -1;
    memset(out, 0, sizeof *out);

    /* 第一遍：统计数量，一次分配到位 */
    uint32_t nw = 0, ni = 0, nc = 0;
    for (int i = 0; i < ini->n_sections; ++i) {
        const char *n = ini->sections[i].name;
        if      (!strcmp(n, "WINDOW")) ++nw;
        else if (!strcmp(n, "ICON"))   ++ni;
        else if (!strcmp(n, "COLOR"))  ++nc;
    }
    if (nw) out->windows = (S3UiWindow *)calloc(nw, sizeof(S3UiWindow));
    if (ni) out->icons   = (S3UiIcon   *)calloc(ni, sizeof(S3UiIcon));
    if (nc) out->colors  = (S3UiColor  *)calloc(nc, sizeof(S3UiColor));
    if ((nw && !out->windows) || (ni && !out->icons) || (nc && !out->colors)) {
        s3_ui_free(out);
        return -1;
    }

    /* 第二遍：填充 */
    char t[512];
    for (int i = 0; i < ini->n_sections; ++i) {
        const S3IniSection *s = &ini->sections[i];
        const char *n = s->name;

        if (!strcmp(n, "WINDOW")) {
            S3UiWindow *w = &out->windows[out->n_windows++];
            w->id = (uint32_t)parse_int_clean(val_clean(s, "ID", t, sizeof t), 0);
            parse_rect_clean(val_clean(s, "Range", t, sizeof t), &w->range);
            parse_rect_clean(val_clean(s, "WorkRange", t, sizeof t), &w->work_range);
            parse_rect_clean(val_clean(s, "FillRange", t, sizeof t), &w->fill_range);
            w->style = s3_ui_parse_style(s3_ini_str(s, "Style", NULL));
            {
                char c[256];
                w->cls = s3_ui_parse_class(val_clean(s, "Class", c, sizeof c));
            }
            w->icon_id = parse_int_clean(val_clean(s, "Icon", t, sizeof t), -1);
            w->command = parse_int_clean(val_clean(s, "Command", t, sizeof t), -1);
            w->font    = parse_int_clean(val_clean(s, "Font", t, sizeof t), -1);
            w->fcolor  = parse_int_clean(val_clean(s, "FColor", t, sizeof t), -1);
            w->bcolor  = parse_int_clean(val_clean(s, "BColor", t, sizeof t), -1);
            w->cols    = parse_int_clean(val_clean(s, "Cols", t, sizeof t), -1);
            w->rows    = parse_int_clean(val_clean(s, "Rows", t, sizeof t), -1);
            w->lines   = parse_int_clean(val_clean(s, "Lines", t, sizeof t), -1);
            w->check   = parse_int_clean(val_clean(s, "Check", t, sizeof t), -1);
            {
                char c[512];
                const char *v = val_clean(s, "Title", c, sizeof c);
                w->title = *v ? s3_dup(v) : NULL;
                v = val_clean(s, "Comment", c, sizeof c);
                w->comment = *v ? s3_dup(v) : NULL;
            }
            int nc2 = s3_ini_count(s, "Child");
            if (nc2 > 0) {
                w->child_ids = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)nc2);
                if (w->child_ids) {
                    for (int k = 0; k < nc2; ++k) {
                        char c[256];
                        strip_inline(s3_ini_val_at(s, "Child", k), c, sizeof c);
                        w->child_ids[k] = (uint32_t)parse_int_clean(c, -1);
                    }
                    w->child_count = (uint32_t)nc2;
                }
            }
        } else if (!strcmp(n, "ICON")) {
            S3UiIcon *ic = &out->icons[out->n_icons++];
            ic->id = (uint32_t)parse_int_clean(val_clean(s, "ID", t, sizeof t), 0);
            {
                char c[512];
                const char *v = val_clean(s, "Pos", c, sizeof c);
                int seg[2] = {0, 0};
                const char *p = v;
                for (int k = 0; k < 2; ++k) {
                    while (*p == ' ' || *p == '\t') ++p;
                    int sign = 1;
                    if (*p == '-') { sign = -1; ++p; }
                    if (*p < '0' || *p > '9') break;
                    long x = 0;
                    while (*p >= '0' && *p <= '9') { x = x * 10 + (*p - '0'); ++p; }
                    seg[k] = (int)(sign * x);
                    if (*p == ',') { ++p; continue; }
                    break;
                }
                ic->pos_x = seg[0]; ic->pos_y = seg[1];
                v = val_clean(s, "Dir", c, sizeof c);
                ic->dir = *v ? s3_dup(v) : NULL;
                ic->normal  = parse_state(val_clean(s, "Normal",  c, sizeof c));
                ic->focus   = parse_state(val_clean(s, "Focus",   c, sizeof c));
                ic->down    = parse_state(val_clean(s, "Down",    c, sizeof c));
                ic->disable = parse_state(val_clean(s, "Disable", c, sizeof c));
                v = val_clean(s, "Comment", c, sizeof c);
                ic->comment = *v ? s3_dup(v) : NULL;
            }
        } else if (!strcmp(n, "COLOR")) {
            S3UiColor *co = &out->colors[out->n_colors++];
            co->id = (uint32_t)parse_int_clean(val_clean(s, "ID", t, sizeof t), 0);
            {
                char c[512];
                co->normal  = parse_state(val_clean(s, "Normal",  c, sizeof c));
                co->focus   = parse_state(val_clean(s, "Focus",   c, sizeof c));
                co->down    = parse_state(val_clean(s, "Down",    c, sizeof c));
                co->disable = parse_state(val_clean(s, "Disable", c, sizeof c));
                const char *v = val_clean(s, "Comment", c, sizeof c);
                co->comment = *v ? s3_dup(v) : NULL;
            }
        }
    }
    return 0;
}

void s3_ui_free(S3UiLayout *L) {
    if (!L) return;
    for (uint32_t i = 0; i < L->n_windows; ++i) {
        S3UiWindow *w = &L->windows[i];
        free(w->title); free(w->comment); free(w->child_ids);
    }
    for (uint32_t i = 0; i < L->n_icons; ++i) {
        S3UiIcon *ic = &L->icons[i];
        free(ic->dir); free(ic->comment);
        free(ic->normal.raw); free(ic->focus.raw); free(ic->down.raw); free(ic->disable.raw);
    }
    for (uint32_t i = 0; i < L->n_colors; ++i) {
        S3UiColor *co = &L->colors[i];
        free(co->comment);
        free(co->normal.raw); free(co->focus.raw); free(co->down.raw); free(co->disable.raw);
    }
    free(L->windows); free(L->icons); free(L->colors);
    memset(L, 0, sizeof *L);
}

const S3UiWindow *s3_ui_window(const S3UiLayout *L, uint32_t id) {
    if (!L) return NULL;
    for (uint32_t i = 0; i < L->n_windows; ++i)
        if (L->windows[i].id == id) return &L->windows[i];
    return NULL;
}
const S3UiIcon *s3_ui_icon(const S3UiLayout *L, uint32_t id) {
    if (!L) return NULL;
    for (uint32_t i = 0; i < L->n_icons; ++i)
        if (L->icons[i].id == id) return &L->icons[i];
    return NULL;
}
const S3UiColor *s3_ui_color(const S3UiLayout *L, uint32_t id) {
    if (!L) return NULL;
    for (uint32_t i = 0; i < L->n_colors; ++i)
        if (L->colors[i].id == id) return &L->colors[i];
    return NULL;
}
