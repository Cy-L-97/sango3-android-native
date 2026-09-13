/*
 * ui_probe.c —— UI 布局解析验证器（M2）
 *
 * 用法：
 *   sango3ui <out_dir> <pak1> [pak2 ...] [--entry "Setting\Menu.ini"] [--encoding <dir>]
 * 例：
 *   sango3ui build/pc/out "E:/.../Sango3.PAK" "E:/.../Update.PAK"
 *
 * --encoding 指向 engine/assets/encoding（默认 "engine/assets/encoding"，相对 cwd）。
 *   ⚠ 必须先 s3_text_init() 载入 Big5→UTF-8 表，否则中文会原样输出 Big5 字节，
 *     在 UTF-8 视角下就是乱码——这类问题不会报错，只会「静默不一致」。
 *
 * 多 PAK 时**后给出的覆盖先给出的**（原版 Update.PAK 覆盖 Sango3.PAK 同名条目，
 * 与 tools/build_ui.py 的口径一致：Menu.ini 必须取 Update 版，否则控件数对不上）。
 *
 * 链路：PAK 取字节 → s3_ini_parse_inc（展开 #include）→ s3_ui_load → 导出 TSV。
 * #include 的被包含文件同样在 PAK 内（如 Setting\Define.ini），由回调按
 * 「主条目同目录 + 被包含文件名」解析。
 *
 * 产出（UTF-8，供 tools/verify_ui_c.py 与 Python 基准逐字段比对）：
 *   <out_dir>/ui_dump.tsv      三类记录（W 控件 / I 图标 / C 配色）
 *   <out_dir>/ui_summary.txt   计数、类直方图、根窗口与子控件树摘要
 *
 * 注意：控制台只输出 ASCII（Windows 控制台是 GBK），中文一律写文件。
 */
#include "ui.h"
#include "text.h"
#include "pak.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ PAK 读取辅助 */
#define S3UI_MAX_PAK 8

typedef struct {
    PakArchive ar[S3UI_MAX_PAK];
    int        n_ar;
    char       base_dir[512];   /* 主条目所在目录（含尾部反斜杠），供 #include 定位 */
} PakCtx;

static int ieq(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        ++a; ++b;
    }
    return *a == *b;
}

/* 取条目原始字节：后给出的 PAK 优先（Update 覆盖 Sango3）。
 * 匹配：先精确（大小写不敏感），退化为子串包含。 */
static uint8_t *pak_get(PakCtx *c, const char *path, uint32_t *out_len) {
    for (int pass = 0; pass < 2; ++pass) {
        for (int a = c->n_ar - 1; a >= 0; --a) {
            PakArchive *ar = &c->ar[a];
            if (pass == 0) {
                for (uint32_t i = 0; i < ar->entries; ++i)
                    if (ieq(ar->tab[i].name, path)) return pak_read(ar, i, out_len);
            } else {
                int32_t idx = pak_find(ar, path);
                if (idx >= 0) return pak_read(ar, (uint32_t)idx, out_len);
            }
        }
    }
    return NULL;
}

/* #include 回调：name 形如 "define.ini"，按主条目同目录解析 */
static int include_cb(const char *name, unsigned char **out_data, size_t *out_n, void *ud) {
    PakCtx *c = (PakCtx *)ud;
    char path[600];
    /* 若 name 自带目录则直接用；否则拼到 base_dir 下 */
    if (strchr(name, '\\') || strchr(name, '/')) {
        snprintf(path, sizeof path, "%s", name);
    } else {
        snprintf(path, sizeof path, "%s%s", c->base_dir, name);
    }
    uint32_t len = 0;
    uint8_t *d = pak_get(c, path, &len);
    if (!d || !len) return -1;
    *out_data = (unsigned char *)d;
    *out_n = (size_t)len;
    return 0;
}

/* ------------------------------------------------------------ 输出辅助 */
/* TSV 安全化：控制字符与分隔符用 C 风格转义输出（**不丢信息**，可被还原）。
 * 值里确实存在制表符（如 `Normal = 220,220,220,1\t；白`、评论里 `Portrait\t// report form`），
 * 若简单替换成空格，Python 基准就对不上了——所以两侧统一做同一套转义。
 * 空串与 NULL 一律输出 '-'（Python 侧还原成 None）。 */
static void tsv(FILE *f, const char *s) {
    if (!s || !*s) { fputc('-', f); return; }
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        switch (*p) {
            case '\\': fputs("\\\\", f); break;
            case '\t': fputs("\\t", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            default:
                if (*p < 0x20) fprintf(f, "\\x%02X", (unsigned)*p);
                else fputc((int)*p, f);
        }
    }
}

static void tsv_state(FILE *f, const S3UiState *st) {
    tsv(f, st->raw);
    fprintf(f, "\t%d\t", st->n_seg);
    for (int i = 0; i < 4; ++i) fprintf(f, "%s%d", i ? "+" : "", st->v[i]);
}

/* ------------------------------------------------------------ 导出 */
static int write_dump(const S3UiLayout *L, const char *out_dir) {
    char path[1024];
    snprintf(path, sizeof path, "%s/ui_dump.tsv", out_dir);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    /* 列顺序固定；新增列只能在末尾追加，并同步 Python 侧 */
    fprintf(f, "#W\tid\trx\try\trw\trh\twx\twy\tww\twh\tfx\tfy\tfw\tfh\t"
               "style\tclass\tclsname\ticon\tcommand\tfont\tfcolor\tbcolor\t"
               "cols\trows\tlines\tcheck\ttitle\tcomment\tnchild\tchildren\n");
    for (uint32_t i = 0; i < L->n_windows; ++i) {
        const S3UiWindow *w = &L->windows[i];
        fprintf(f, "W\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%lu\t%d\t",
                w->id, w->range.x, w->range.y, w->range.w, w->range.h,
                w->work_range.x, w->work_range.y, w->work_range.w, w->work_range.h,
                w->fill_range.x, w->fill_range.y, w->fill_range.w, w->fill_range.h,
                (unsigned long)w->style, (int)w->cls);
        tsv(f, s3_ui_class_name(w->cls));
        fprintf(f, "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t",
                w->icon_id, w->command, w->font, w->fcolor, w->bcolor,
                w->cols, w->rows, w->lines, w->check);
        tsv(f, w->title); fputc('\t', f);
        tsv(f, w->comment);
        fprintf(f, "\t%u\t", w->child_count);
        for (uint32_t k = 0; k < w->child_count; ++k)
            fprintf(f, "%s%u", k ? "," : "", w->child_ids[k]);
        if (!w->child_count) fputc('-', f);
        fputc('\n', f);
    }

    fprintf(f, "#I\tid\tpx\tpy\tdir\t"
               "n_raw\tn_seg\tn_v\tf_raw\tf_seg\tf_v\td_raw\td_seg\td_v\t"
               "x_raw\tx_seg\tx_v\tcomment\n");
    for (uint32_t i = 0; i < L->n_icons; ++i) {
        const S3UiIcon *ic = &L->icons[i];
        fprintf(f, "I\t%u\t%d\t%d\t", ic->id, ic->pos_x, ic->pos_y);
        tsv(f, ic->dir);
        fputc('\t', f); tsv_state(f, &ic->normal);
        fputc('\t', f); tsv_state(f, &ic->focus);
        fputc('\t', f); tsv_state(f, &ic->down);
        fputc('\t', f); tsv_state(f, &ic->disable);
        fputc('\t', f); tsv(f, ic->comment);
        fputc('\n', f);
    }

    fprintf(f, "#C\tid\t"
               "n_raw\tn_seg\tn_v\tf_raw\tf_seg\tf_v\td_raw\td_seg\td_v\t"
               "x_raw\tx_seg\tx_v\tcomment\n");
    for (uint32_t i = 0; i < L->n_colors; ++i) {
        const S3UiColor *co = &L->colors[i];
        fprintf(f, "C\t%u\t", co->id);
        tsv_state(f, &co->normal);
        fputc('\t', f); tsv_state(f, &co->focus);
        fputc('\t', f); tsv_state(f, &co->down);
        fputc('\t', f); tsv_state(f, &co->disable);
        fputc('\t', f); tsv(f, co->comment);
        fputc('\n', f);
    }

    fclose(f);
    return 0;
}

/* 是否被别的窗口当作 Child 引用 */
static int is_child_of_any(const S3UiLayout *L, uint32_t id) {
    for (uint32_t i = 0; i < L->n_windows; ++i)
        for (uint32_t k = 0; k < L->windows[i].child_count; ++k)
            if (L->windows[i].child_ids[k] == id) return 1;
    return 0;
}

static void print_tree(FILE *f, const S3UiLayout *L, uint32_t id, int depth,
                      int max_depth, int *printed) {
    if (depth > max_depth || *printed > 200) return;
    const S3UiWindow *w = s3_ui_window(L, id);
    if (!w) return;
    for (int i = 0; i < depth; ++i) fputs("  ", f);
    fprintf(f, "[%u] %s", w->id, s3_ui_class_name(w->cls));
    fprintf(f, "  range=(%d,%d,%d,%d)", w->range.x, w->range.y, w->range.w, w->range.h);
    fprintf(f, "  style=0x%08lX", (unsigned long)w->style);
    if (w->icon_id >= 0) fprintf(f, "  icon=%d", w->icon_id);
    if (w->command >= 0) fprintf(f, "  cmd=%d", w->command);
    if (w->title) { fputs("  title=", f); tsv(f, w->title); }
    fputc('\n', f);
    ++*printed;
    for (uint32_t k = 0; k < w->child_count; ++k)
        print_tree(f, L, w->child_ids[k], depth + 1, max_depth, printed);
}

static int write_summary(const S3UiLayout *L, const char *out_dir,
                         const char *entry, uint32_t bytes,
                         char const *const *paks, int n_paks) {
    char path[1024];
    snprintf(path, sizeof path, "%s/ui_summary.txt", out_dir);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    fprintf(f, "# Sango3 UI layout summary\n");
    for (int i = 0; i < n_paks; ++i) fprintf(f, "pak\t%s\n", paks[i]);
    fprintf(f, "entry\t%s\n", entry);
    fprintf(f, "bytes\t%lu\n", (unsigned long)bytes);
    fprintf(f, "n_windows\t%u\n", L->n_windows);
    fprintf(f, "n_icons\t%u\n", L->n_icons);
    fprintf(f, "n_colors\t%u\n", L->n_colors);

    /* 控件类直方图 */
    fprintf(f, "\n## class histogram\n");
    int hist[100];
    memset(hist, 0, sizeof hist);
    int unknown = 0;
    for (uint32_t i = 0; i < L->n_windows; ++i) {
        int c = (int)L->windows[i].cls;
        if (c >= 0 && c < 100) ++hist[c]; else ++unknown;
    }
    for (int c = 0; c <= (int)S3_WND_BFMESSAGE; ++c) {
        S3WndClass k = (S3WndClass)c;
        fprintf(f, "%s\t%d\n", s3_ui_class_name(k), hist[c]);
    }
    fprintf(f, "UNKNOWN\t%d\n", unknown);

    /* Style 位使用统计 */
    fprintf(f, "\n## style bit usage\n");
    struct { uint32_t bit; const char *name; } BITS[] = {
        { S3_WS_VISIBLE, "wsVisible" }, { S3_WS_ICON, "wsIcon" },
        { S3_WS_VCENTER, "wsVCenter" }, { S3_WS_HCENTER, "wsHCenter" },
        { S3_WS_TEXT, "wsText" }, { S3_WS_CHECK, "wsCheck" },
        { S3_WS_VSCROLL, "wsVScroll" }, { S3_WS_HSCROLL, "wsHScroll" },
        { S3_WS_SCHECK, "wsSCheck" }, { S3_WS_RIGHT, "wsRight" },
        { S3_WS_LEFT, "wsLeft" }, { S3_WS_HORIZONTAL, "wsHorizontal" },
        { S3_WS_TRANS, "wsTrans" }, { S3_WS_REPORT, "wsReport" },
        { S3_WS_FORCE_SELECT_CHANGE, "wsForceSelectChange" },
        { S3_WS_RIGHT2LEFT, "wsRight2Left" }, { S3_WS_LEFT2RIGHT, "wsLeft2Right" },
    };
    for (size_t b = 0; b < sizeof BITS / sizeof BITS[0]; ++b) {
        uint32_t cnt = 0;
        for (uint32_t i = 0; i < L->n_windows; ++i)
            if (L->windows[i].style & BITS[b].bit) ++cnt;
        fprintf(f, "%s\t%u\n", BITS[b].name, cnt);
    }

    /* 健全性：矩形全 0（解析失败的典型症状，用来防止「静默置 0」再次溜过去） */
    fprintf(f, "\n## sanity\n");
    uint32_t z_range = 0, z_work = 0, z_fill = 0;
    for (uint32_t i = 0; i < L->n_windows; ++i) {
        const S3UiWindow *w = &L->windows[i];
        if (!w->range.w && !w->range.h) ++z_range;
        if (!w->work_range.w && !w->work_range.h) ++z_work;
        if (!w->fill_range.w && !w->fill_range.h) ++z_fill;
    }
    fprintf(f, "range_wh_zero\t%u / %u\n", z_range, L->n_windows);
    fprintf(f, "work_range_wh_zero\t%u / %u\n", z_work, L->n_windows);
    fprintf(f, "fill_range_wh_zero\t%u / %u\n", z_fill, L->n_windows);

    /* 交叉引用完整性：Icon / FColor / BColor / Child 是否都能找到目标 */
    fprintf(f, "\n## cross references\n");
    uint32_t m_icon = 0, m_fcolor = 0, m_bcolor = 0, m_child = 0;
    uint32_t t_icon = 0, t_fcolor = 0, t_bcolor = 0, t_child = 0;
    for (uint32_t i = 0; i < L->n_windows; ++i) {
        const S3UiWindow *w = &L->windows[i];
        if (w->icon_id >= 0)   { ++t_icon;   if (!s3_ui_icon(L, (uint32_t)w->icon_id)) ++m_icon; }
        if (w->fcolor >= 0)    { ++t_fcolor; if (!s3_ui_color(L, (uint32_t)w->fcolor)) ++m_fcolor; }
        if (w->bcolor >= 0)    { ++t_bcolor; if (!s3_ui_color(L, (uint32_t)w->bcolor)) ++m_bcolor; }
        for (uint32_t k = 0; k < w->child_count; ++k) {
            ++t_child;
            if (!s3_ui_window(L, w->child_ids[k])) ++m_child;
        }
    }
    fprintf(f, "icon\ttotal=%u\tmissing=%u\n", t_icon, m_icon);
    fprintf(f, "fcolor\ttotal=%u\tmissing=%u\n", t_fcolor, m_fcolor);
    fprintf(f, "bcolor\ttotal=%u\tmissing=%u\n", t_bcolor, m_bcolor);
    fprintf(f, "child\ttotal=%u\tmissing=%u\n", t_child, m_child);

    /* 根窗口（没有被任何窗口引用为 Child） */
    fprintf(f, "\n## root windows (not referenced as child)\n");
    uint32_t n_root = 0;
    for (uint32_t i = 0; i < L->n_windows; ++i)
        if (!is_child_of_any(L, L->windows[i].id)) ++n_root;
    fprintf(f, "n_roots\t%u\n", n_root);
    for (uint32_t i = 0; i < L->n_windows; ++i) {
        const S3UiWindow *w = &L->windows[i];
        if (is_child_of_any(L, w->id)) continue;
        fprintf(f, "root\t%u\t%s\tchildren=%u\n", w->id, s3_ui_class_name(w->cls), w->child_count);
    }

    /* 前若干棵控件树（控制台友好，只打前 3 棵、深度 3） */
    fprintf(f, "\n## sample trees (first 3 roots, depth<=3)\n");
    int printed_total = 0;
    for (uint32_t i = 0; i < L->n_windows && printed_total < 3; ++i) {
        uint32_t id = L->windows[i].id;
        if (is_child_of_any(L, id)) continue;
        int p = 0;
        print_tree(f, L, id, 0, 3, &p);
        fputc('\n', f);
        ++printed_total;
    }

    fclose(f);
    return 0;
}

/* ------------------------------------------------------------ main */
int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "usage: sango3ui <out_dir> <pak1> [pak2 ...] "
                "[--entry \"Setting\\Menu.ini\"] [--encoding <dir>]\n"
                "  note: later PAK overrides earlier ones (Update.PAK after Sango3.PAK)\n");
        return 2;
    }
    const char *out_dir = argv[1];
    const char *entry = "Setting\\Menu.ini";
    const char *enc_dir = "engine/assets/encoding";
    const char *paks[S3UI_MAX_PAK];
    int n_paks = 0;

    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--entry") && i + 1 < argc) { entry = argv[++i]; continue; }
        if (!strcmp(argv[i], "--encoding") && i + 1 < argc) { enc_dir = argv[++i]; continue; }
        if (n_paks < S3UI_MAX_PAK) paks[n_paks++] = argv[i];
    }
    if (n_paks == 0) {
        fprintf(stderr, "no pak given\n");
        return 2;
    }

    /* Big5 → UTF-8 查表：必须在任何 INI 解析之前载入 */
    if (s3_text_init(enc_dir) != 0 || !s3_text_ready()) {
        fprintf(stderr, "text_init failed (encoding dir: %s)\n", enc_dir);
        return 1;
    }

    PakCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    for (int i = 0; i < n_paks; ++i) {
        const char *err = NULL;
        if (!pak_open(paks[i], &ctx.ar[ctx.n_ar], &err)) {
            fprintf(stderr, "pak_open failed: %s (%s)\n", paks[i], err ? err : "?");
            for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
            return 1;
        }
        ++ctx.n_ar;
    }

    uint32_t len = 0;
    uint8_t *data = pak_get(&ctx, entry, &len);
    if (!data) {
        fprintf(stderr, "entry not found: %s\n", entry);
        for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
        return 1;
    }

    {
        /* 主条目目录（含尾部反斜杠） */
        const char *slash = strrchr(entry, '\\');
        const char *slash2 = strrchr(entry, '/');
        if (slash2 && (!slash || slash2 > slash)) slash = slash2;
        if (slash) {
            size_t n = (size_t)(slash - entry) + 1;
            if (n >= sizeof ctx.base_dir) n = sizeof ctx.base_dir - 1;
            memcpy(ctx.base_dir, entry, n);
            ctx.base_dir[n] = '\0';
        }
    }

    S3Ini *ini = s3_ini_parse_inc(data, len, include_cb, &ctx);
    free(data);
    if (!ini) {
        fprintf(stderr, "ini parse failed\n");
        for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
        return 1;
    }

    S3UiLayout L;
    if (s3_ui_load(&L, ini) != 0) {
        fprintf(stderr, "ui_load failed\n");
        s3_ini_free(ini);
        for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
        return 1;
    }

    if (write_dump(&L, out_dir) != 0 ||
        write_summary(&L, out_dir, entry, len, paks, n_paks) != 0) {
        fprintf(stderr, "write failed (out_dir exists?): %s\n", out_dir);
        s3_ui_free(&L); s3_ini_free(ini);
        for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
        return 1;
    }

    printf("OK windows=%u icons=%u colors=%u sections=%d -> %s\n",
           L.n_windows, L.n_icons, L.n_colors, ini->n_sections, out_dir);

    s3_ui_free(&L);
    s3_ini_free(ini);
    for (int k = 0; k < ctx.n_ar; ++k) pak_close(&ctx.ar[k]);
    s3_text_shutdown();
    return 0;
}
