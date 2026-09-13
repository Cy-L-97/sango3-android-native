/*
 * ui.h —— UI 布局数据层（M2）
 *
 * 原版把整个界面写成一张声明式表：`Setting\Menu.ini`（7048 行），
 *   · `[WINDOW]` 控件：ID / Style(位标志集) / Class(控件类) / Range(逻辑坐标 x,y,w,h)
 *                      / Icon(引用 ICON id) / Command / Child(可多行，引用 WINDOW id)
 *                      / FColor·BColor(引用 COLOR id) / Font / WorkRange / FillRange ...
 *   · `[ICON]`  图标：ID / Pos / Dir(素材目录) / Normal·Focus·Down·Disable(四态素材名)
 *   · `[COLOR]` 配色：ID / Normal·Focus·Down·Disable(四态，值形如 "21,89,210,1")
 *
 * 三个 ID 空间**各自独立**，靠 Icon / FColor / Child 交叉引用——解析时必须分清。
 *
 * 两个容易踩的坑（已在本模块处理）：
 *   ① COLOR 的四态值**不一定是数字**：可能是 "Normal" 这类「沿用」语义串。
 *      因此原串必须保留，不能一律强转整数（强转会静默丢信息）。
 *   ② Style 值里混有非标准 token（原版存在 `Style = Class = WND_CLASS_BASE` 这类笔误），
 *      未知 token 应忽略而不是报错。
 *
 * 本模块只做「INI → 结构体」，不含任何渲染/平台依赖，可离线跑回归。
 */
#ifndef SANGO3_UI_H
#define SANGO3_UI_H

#include <stdint.h>
#include "ini.h"

/* ------------------------------------------------------------ 控件类 */
typedef enum {
    S3_WND_BASE = 0,
    S3_WND_BUTTON,
    S3_WND_STATIC,
    S3_WND_LIST,
    S3_WND_SCROLLBAR,
    S3_WND_MSTATIC,
    S3_WND_BUTTONREPORT,
    S3_WND_PROGRESS_BFHP,
    S3_WND_PROGRESS,
    S3_WND_PROGRESSEX,
    S3_WND_BFRADAR,
    S3_WND_TIMER,
    S3_WND_BFMESSAGE,
    S3_WND_UNKNOWN = 99
} S3WndClass;

/* ------------------------------------------------------------ Style 位标志 */
#define S3_WS_VISIBLE             0x00000001u
#define S3_WS_ICON                0x00000002u
#define S3_WS_VCENTER             0x00000004u
#define S3_WS_HCENTER             0x00000008u
#define S3_WS_TEXT                0x00000010u
#define S3_WS_CHECK               0x00000020u
#define S3_WS_VSCROLL             0x00000040u
#define S3_WS_HSCROLL             0x00000080u
#define S3_WS_SCHECK              0x00000100u  /* wsSCheck */
#define S3_WS_RIGHT               0x00000200u
#define S3_WS_LEFT                0x00000400u
#define S3_WS_HORIZONTAL          0x00000800u
#define S3_WS_TRANS               0x00001000u  /* wsTrans */
#define S3_WS_REPORT              0x00002000u
#define S3_WS_FORCE_SELECT_CHANGE 0x00004000u
#define S3_WS_RIGHT2LEFT          0x00008000u
#define S3_WS_LEFT2RIGHT          0x00010000u

typedef struct { int32_t x, y, w, h; } S3Rect;

/* ------------------------------------------------------------ 一个状态值（配色/图标四态通用） */
typedef struct {
    char *raw;      /* 原串（可能是 "21,89,210,1"，也可能是 "Normal"） */
    int   v[4];     /* 按 ',' 切出的整数段（最多 4 段） */
    int   n_seg;    /* 解析出的段数；0 表示非数字串（保留 raw） */
} S3UiState;

typedef struct {
    uint32_t   id;
    S3Rect     range;
    S3Rect     work_range;
    S3Rect     fill_range;
    uint32_t   style;      /* S3_WS_* 位掩码 */
    S3WndClass cls;
    int32_t    icon_id;    /* -1 = 无 */
    int32_t    command;    /* -1 = 无 */
    int32_t    font;       /* -1 = 无 */
    int32_t    fcolor;     /* -1 = 无 */
    int32_t    bcolor;     /* -1 = 无 */
    int32_t    cols;       /* -1 = 无 */
    int32_t    rows;       /* -1 = 无 */
    int32_t    lines;      /* -1 = 无 */
    int32_t    check;      /* -1 = 无 */
    char      *title;      /* UTF-8（Big5 原文已解码） */
    char      *comment;    /* UTF-8 */
    uint32_t  *child_ids;
    uint32_t   child_count;
} S3UiWindow;

typedef struct {
    uint32_t id;
    int32_t  pos_x, pos_y;
    char    *dir;
    S3UiState normal, focus, down, disable;   /* 四态素材名 */
    char    *comment;
} S3UiIcon;

typedef struct {
    uint32_t  id;
    S3UiState normal, focus, down, disable;   /* 四态配色 */
    char     *comment;
} S3UiColor;

typedef struct {
    S3UiWindow *windows; uint32_t n_windows;
    S3UiIcon   *icons;   uint32_t n_icons;
    S3UiColor  *colors;  uint32_t n_colors;
} S3UiLayout;

/* 从已解析的 INI（应已展开 #include）构建布局。成功返回 0，失败非 0。 */
int  s3_ui_load(S3UiLayout *out, const S3Ini *ini);
void s3_ui_free(S3UiLayout *L);

/* 按 id 查找（线性；M2 阶段数量在百量级，足够）。找不到返回 NULL。 */
const S3UiWindow *s3_ui_window(const S3UiLayout *L, uint32_t id);
const S3UiIcon   *s3_ui_icon(const S3UiLayout *L, uint32_t id);
const S3UiColor  *s3_ui_color(const S3UiLayout *L, uint32_t id);

uint32_t   s3_ui_parse_style(const char *style_csv);
S3WndClass s3_ui_parse_class(const char *cls);
const char *s3_ui_class_name(S3WndClass c);

#endif /* SANGO3_UI_H */
