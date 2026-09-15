/*
 * editor_scene.c —— 自定义武将创建表单（实现，见 editor_scene.h）
 *
 * 取舍（玩法优先，2026-09-15 用户确认）：
 *   · 属性随机 roll + 整体重随（不做点数分配）
 *   · 头像用原版自创武将专用脸谱 Shape\Portrait\{mFace001~030, wFace001~020}.SHP
 *     （100×120，实测可解码）——比通用武将头像池更贴合"自创"语义，性别联动
 *   · 姓名真文本输入（SDL_TEXTINPUT；安卓软键盘由 SDL_StartTextInput 拉起）
 *   · 表单为纯色框 + 文字（不逐像素复刻原版素材），后续可换皮
 */
#include "editor_scene.h"
#include "shp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* SDL_Keycode 里 SDLK_BACKSPACE == 8（不引入 SDL.h，保持本模块可离线编译） */
#define ED_KEY_BACKSPACE 8

#define ED_NAME_MAX 24        /* 姓名缓冲字节数（UTF-8，约 8 个汉字） */
#define ED_M_FACES  30
#define ED_W_FACES  20

enum { ED_ST_FORCE = 0, ED_ST_INTEL, ED_ST_HP, ED_ST_MP, ED_ST_N };

struct S3Editor {
    S3EditorDrawText draw_text; void *text_ud;
    S3EditorReadAsset read_asset; void *asset_ud;

    char     name[ED_NAME_MAX + 1];
    int      sex;                /* 0=男 1=女 */
    int      face;               /* 所属性别池内的下标（0 基） */
    int      stats[ED_ST_N];
    int      focus_name;         /* 1 = 姓名输入框聚焦 */
    int      result;             /* 0/1/2 */
    uint32_t tick;               /* 光标闪烁 */

    /* 当前头像（已解码缓存） */
    ShpImage face_img;
    int      face_ok;
    char     face_path[160];
};

/* ------------------------------------------------------------- 小工具 */
static int ed_rand(int lo, int hi) {   /* [lo,hi] */
    return lo + rand() % (hi - lo + 1);
}

static void ed_roll(S3Editor *e) {
    e->stats[ED_ST_FORCE] = ed_rand(45, 98);
    e->stats[ED_ST_INTEL] = ed_rand(30, 98);
    e->stats[ED_ST_HP]    = ed_rand(60, 100);
    e->stats[ED_ST_MP]    = ed_rand(30, 100);
}

/* UTF-8：删末尾一个码点；追加时做字节预算 + 危险字符过滤 */
static void ed_name_backspace(S3Editor *e) {
    int n = (int)strlen(e->name);
    while (n > 0 && (e->name[n - 1] & 0xC0) == 0x80) --n;  /* 续字节 */
    if (n > 0) --n;                                        /* 首字节 */
    e->name[n] = '\0';
}

static void ed_name_append(S3Editor *e, const char *utf8) {
    for (const unsigned char *s = (const unsigned char *)utf8; *s; ++s) {
        if (*s < 0x20 || *s == '"' || *s == '\\') continue;  /* 控制符/JSON 危险字符 */
        int n = (int)strlen(e->name);
        if (n + 1 >= ED_NAME_MAX) break;
        e->name[n++] = (char)*s;
        e->name[n] = '\0';
        /* 只接完整码点：非首字节的续字节随其首字节一起进来，这里逐字节复制即可 */
    }
}

static void ed_face_path(const S3Editor *e, char *out, int cap) {
    snprintf(out, cap, "Shape\\Portrait\\%s%03d.SHP",
             e->sex ? "wFace" : "mFace", e->face + 1);
}

/* ------------------------------------------------------------- 绘制原语 */
static void ed_box(Sango3Canvas *cv, int32_t x, int32_t y, int32_t w, int32_t h,
                   uint8_t br, uint8_t bg, uint8_t bb, int focused) {
    sango3_canvas_fill(cv, x, y, w, h, 28, 26, 40);
    if (focused) sango3_canvas_frame(cv, x - 2, y - 2, w + 4, h + 4, 2, 214, 178, 94);
    else         sango3_canvas_frame(cv, x, y, w, h, 1, 120, 116, 108);
}

static void ed_text(S3Editor *e, Sango3Canvas *cv, const char *s,
                    int32_t x, int32_t y, int32_t w, int32_t h,
                    uint32_t rgb, int font, int hcenter, int vcenter) {
    if (!e->draw_text) return;
    uint32_t style = 0;
    if (hcenter) style |= 0x8u;    /* S3_WS_HCENTER（ui.h） */
    if (vcenter) style |= 0x4u;    /* S3_WS_VCENTER */
    e->draw_text(e->text_ud, cv, s, x, y, w, h, rgb, font, style);
}

/* ------------------------------------------------------------- 布局（640×480） */
typedef struct { int32_t x, y, w, h; } EdRect;

static const EdRect R_INPUT  = { 170,  92, 300, 40 };
static const EdRect R_SEXM   = { 170, 152,  80, 32 };
static const EdRect R_SEXW   = { 262, 152,  80, 32 };
static const EdRect R_FACE   = { 270, 208, 100, 120 };
static const EdRect R_PREV   = { 214, 250,  40, 36 };
static const EdRect R_NEXT   = { 386, 250,  40, 36 };
static const EdRect R_REROLL = {  60, 380, 110, 36 };
static const EdRect R_CREATE = { 420, 432,  90, 34 };
static const EdRect R_CANCEL = { 524, 432,  90, 34 };

static int ed_in(const EdRect *r, int32_t x, int32_t y) {
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* ------------------------------------------------------------- 头像 */
static void ed_load_face(S3Editor *e) {
    char path[160];
    ed_face_path(e, path, sizeof path);
    if (e->face_ok && strcmp(path, e->face_path) == 0) return;   /* 已缓存 */
    if (e->face_ok) { shp_free(&e->face_img); e->face_ok = 0; }

    uint32_t len = 0;
    uint8_t *raw = e->read_asset ? e->read_asset(e->asset_ud, path, &len) : NULL;
    if (!raw) { snprintf(e->face_path, sizeof e->face_path, "%s", path); return; }
    const char *err = NULL;
    if (shp_decode(raw, len, &e->face_img, &err)) {
        e->face_ok = 1;
        snprintf(e->face_path, sizeof e->face_path, "%s", path);
    }
    free(raw);
}

/* ------------------------------------------------------------- API */
S3Editor *s3_editor_new(S3EditorDrawText draw_text, void *text_ud,
                        S3EditorReadAsset read_asset, void *asset_ud) {
    S3Editor *e = (S3Editor *)calloc(1, sizeof *e);
    if (!e) return NULL;
    e->draw_text = draw_text; e->text_ud = text_ud;
    e->read_asset = read_asset; e->asset_ud = asset_ud;
    srand((unsigned)time(NULL) ^ (unsigned)(uintptr_t)e);
    ed_roll(e);
    return e;
}

void s3_editor_free(S3Editor *e) {
    if (!e) return;
    if (e->face_ok) shp_free(&e->face_img);
    free(e);
}

void s3_editor_render(S3Editor *e, Sango3Canvas *cv) {
    if (!e || !cv) return;
    ++e->tick;

    /* 面板底（半屏暗色，让背景透出四周） */
    sango3_canvas_fill(cv, 20, 60, 600, 420, 16, 14, 26);
    sango3_canvas_frame(cv, 20, 60, 600, 420, 2, 168, 140, 76);

    ed_text(e, cv, "创建武将", 220, 66, 200, 34, 0xF2E0B0, 2, 1, 1);

    /* 姓名 */
    ed_text(e, cv, "姓名", 60, 92, 100, 40, 0xE8E8E8, 1, 0, 1);
    ed_box(cv, R_INPUT.x, R_INPUT.y, R_INPUT.w, R_INPUT.h, 0, 0, 0, e->focus_name);
    {
        char buf[ED_NAME_MAX + 2];
        snprintf(buf, sizeof buf, "%s", e->name);
        if (e->focus_name && (e->tick / 30) % 2) {
            size_t n = strlen(buf);
            if (n + 1 < sizeof buf) { buf[n] = '_'; buf[n + 1] = '\0'; }
        }
        ed_text(e, cv, buf[0] ? buf : "点击输入…",
                R_INPUT.x + 10, R_INPUT.y, R_INPUT.w - 20, R_INPUT.h,
                buf[0] ? 0xFFFFFF : 0x888888, 1, 0, 1);
    }

    /* 性别 */
    ed_text(e, cv, "性别", 60, 152, 100, 32, 0xE8E8E8, 1, 0, 1);
    ed_box(cv, R_SEXM.x, R_SEXM.y, R_SEXM.w, R_SEXM.h, 0, 0, 0, e->sex == 0);
    ed_text(e, cv, "男", R_SEXM.x, R_SEXM.y, R_SEXM.w, R_SEXM.h,
            e->sex == 0 ? 0xFFE9A8 : 0xC8C8C8, 1, 1, 1);
    ed_box(cv, R_SEXW.x, R_SEXW.y, R_SEXW.w, R_SEXW.h, 0, 0, 0, e->sex == 1);
    ed_text(e, cv, "女", R_SEXW.x, R_SEXW.y, R_SEXW.w, R_SEXW.h,
            e->sex == 1 ? 0xFFE9A8 : 0xC8C8C8, 1, 1, 1);

    /* 头像 + 翻页 */
    ed_load_face(e);
    if (e->face_ok)
        sango3_canvas_blit(cv, e->face_img.rgba,
                           (int32_t)e->face_img.width, (int32_t)e->face_img.height,
                           R_FACE.x, R_FACE.y, 1);
    else
        ed_box(cv, R_FACE.x, R_FACE.y, R_FACE.w, R_FACE.h, 0, 0, 0, 0);
    ed_box(cv, R_PREV.x, R_PREV.y, R_PREV.w, R_PREV.h, 0, 0, 0, 0);
    ed_text(e, cv, "<", R_PREV.x, R_PREV.y, R_PREV.w, R_PREV.h, 0xFFFFFF, 1, 1, 1);
    ed_box(cv, R_NEXT.x, R_NEXT.y, R_NEXT.w, R_NEXT.h, 0, 0, 0, 0);
    ed_text(e, cv, ">", R_NEXT.x, R_NEXT.y, R_NEXT.w, R_NEXT.h, 0xFFFFFF, 1, 1, 1);

    /* 属性 + 重随 */
    static const char *ST[ED_ST_N] = { "武力", "智力", "体力", "技力" };
    ed_box(cv, R_REROLL.x, R_REROLL.y, R_REROLL.w, R_REROLL.h, 0, 0, 0, 0);
    ed_text(e, cv, "重新随机", R_REROLL.x, R_REROLL.y, R_REROLL.w, R_REROLL.h,
            0xFFE9A8, 1, 1, 1);
    for (int i = 0; i < ED_ST_N; ++i) {
        char buf[32];
        snprintf(buf, sizeof buf, "%s  %d", ST[i], e->stats[i]);
        ed_text(e, cv, buf, 60, 210 + i * 40, 140, 32, 0xE8E8E8, 1, 0, 1);
    }

    /* 创建 / 取消 */
    ed_box(cv, R_CREATE.x, R_CREATE.y, R_CREATE.w, R_CREATE.h, 0, 0, 0, 0);
    ed_text(e, cv, "创建", R_CREATE.x, R_CREATE.y, R_CREATE.w, R_CREATE.h,
            0xFFE9A8, 1, 1, 1);
    ed_box(cv, R_CANCEL.x, R_CANCEL.y, R_CANCEL.w, R_CANCEL.h, 0, 0, 0, 0);
    ed_text(e, cv, "取消", R_CANCEL.x, R_CANCEL.y, R_CANCEL.w, R_CANCEL.h,
            0xC8C8C8, 1, 1, 1);
}

void s3_editor_on_click(S3Editor *e, int32_t lx, int32_t ly) {
    if (!e) return;
    e->result = 0;
    if (ed_in(&R_INPUT, lx, ly)) { e->focus_name = 1; return; }
    e->focus_name = 0;
    if (ed_in(&R_SEXM, lx, ly)) {
        if (e->sex != 0) { e->sex = 0; e->face = 0; }
        return;
    }
    if (ed_in(&R_SEXW, lx, ly)) {
        if (e->sex != 1) { e->sex = 1; e->face = 0; }
        return;
    }
    if (ed_in(&R_PREV, lx, ly)) {
        int n = e->sex ? ED_W_FACES : ED_M_FACES;
        e->face = (e->face + n - 1) % n;
        return;
    }
    if (ed_in(&R_NEXT, lx, ly)) {
        int n = e->sex ? ED_W_FACES : ED_M_FACES;
        e->face = (e->face + 1) % n;
        return;
    }
    if (ed_in(&R_REROLL, lx, ly)) { ed_roll(e); return; }
    if (ed_in(&R_CREATE, lx, ly)) {
        if (e->name[0]) e->result = 1;      /* 空名不允许创建 */
        return;
    }
    if (ed_in(&R_CANCEL, lx, ly)) { e->result = 2; return; }
}

void s3_editor_on_text(S3Editor *e, const char *utf8) {
    if (e && e->focus_name && utf8) ed_name_append(e, utf8);
}

void s3_editor_on_key(S3Editor *e, int32_t sym) {
    if (e && e->focus_name && sym == ED_KEY_BACKSPACE) ed_name_backspace(e);
}

int s3_editor_result(const S3Editor *e) { return e ? e->result : 2; }

int s3_editor_typing(const S3Editor *e) { return e ? e->focus_name : 0; }

int s3_editor_save(const S3Editor *e, const char *jsonl_path) {
    if (!e || !e->name[0] || !jsonl_path) return -1;
    char face[64];
    ed_face_path(e, face, sizeof face);
    FILE *f = fopen(jsonl_path, "ab");
    if (!f) return -2;
    fprintf(f,
        "{\"name\":\"%s\",\"sex\":\"%s\",\"face\":\"%s\","
        "\"force\":%d,\"intel\":%d,\"hp\":%d,\"mp\":%d}\n",
        e->name, e->sex ? "w" : "m", face,
        e->stats[ED_ST_FORCE], e->stats[ED_ST_INTEL],
        e->stats[ED_ST_HP], e->stats[ED_ST_MP]);
    fclose(f);
    return 0;
}
