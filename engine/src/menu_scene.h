/*
 * menu_scene.h —— 控件树运行时 + 场景渲染（M2-5，渲染路径 B：图元级原生渲染）
 *
 * 与路径 A（整帧放大）的区别：
 *   路径 B 直接在逻辑画布上按 `Range` 逐个绘制控件图元，素材按 `1:1` 贴图。
 *   由于原版 `Menu.ini` 的逻辑坐标与素材像素**完全 1:1**（已实测：
 *   按钮 Range=234,185,172,46 ↔ 素材 NewGame.shp 实测 172×46），
 *   因此路径 B 在整数倍缩放下是**零重采样**的，2K 下天然清晰。
 *
 * 本模块职责（运行时那一层）：
 *   遍历控件树（root 窗口 + 递归 Child），对每个控件：
 *     · 若 Style 含 S3_WS_ICON 且引用了 ICON id
 *         → 查 ICON 表 → 取四态素材名（按当前 state）→ 拼路径
 *           `Shape + Dir + Name + .shp` → 由 read_asset 回调取字节
 *           → SHP 解码 → 贴到 (Range.x + Pos.x, Range.y + Pos.y)
 *     · 若 Style 含 S3_WS_TEXT 且有 title → 交给 draw_text 回调（字体层）
 *     · 递归子控件（子在父之上）
 *
 * 依赖注入（本模块不直接依赖 PAK / SDL）：
 *   read_asset —— 资源读取（多 PAK 优先级查找由调用方实现）
 *   draw_text  —— 文本绘制（SDL_ttf 层实现；未提供则跳过文本）
 * 这样本模块可离线跑（dump 出图校验），也可在 SDL 程序里复用。
 *
 * 统计字段是**sanity 计数**：素材"静默找不到"是这类代码最典型的错法，
 * 必须让"没画出来"这件事有数字可查。
 */
#ifndef SANGO3_MENU_SCENE_H
#define SANGO3_MENU_SCENE_H

#include <stdint.h>
#include "ui.h"
#include "render.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 资源读取：返回 malloc 的字节缓冲（调用方 free），失败返回 NULL。 */
typedef uint8_t *(*S3MenuReadAsset)(void *ud, const char *path, uint32_t *out_len);

/* 文本绘制：在画布的 (x,y,w,h) 区域内绘制 utf8 文本。
 * rgb = 0xRRGGBB；font = 0/1/2（原版字号档位）；style = S3_WS_* 中的对齐位。 */
typedef void (*S3MenuDrawText)(void *ud, Sango3Canvas *cv, const char *utf8,
                               int32_t x, int32_t y, int32_t w, int32_t h,
                               uint32_t rgb, int font, uint32_t style);

typedef struct {
    const S3UiLayout *layout;

    S3MenuReadAsset   read_asset;   /* 必需 */
    void             *asset_ud;
    S3MenuDrawText    draw_text;    /* 可空 */
    void             *text_ud;

    int32_t           root_id;      /* 要渲染的根窗口；默认 1（主菜单） */
    int               state;        /* 0=normal 1=focus 2=down 3=disable */

    /* ---- 统计（sanity）---- */
    int32_t n_drawn;          /* 实际贴图次数 */
    int32_t n_icon_missing;   /* ICON 表里没有该 id */
    int32_t n_asset_missing;  /* PAK 里找不到素材文件 */
    int32_t n_decode_fail;    /* SHP 解码失败 */
    int32_t n_text;           /* 文本绘制次数 */
    int32_t n_containers;     /* 既无图标也无文本的纯容器 */

    /* 失败清单（各最多 8 条）——让"没画出来"可见，而不是静默失败 */
    char    log_asset_missing[8][512];
    char    log_decode_fail[8][512];
} S3MenuScene;

/* 渲染 root 窗口到画布（不清屏，调用方自备底色）。返回贴图次数。 */
int32_t s3_menu_render(S3MenuScene *ms, Sango3Canvas *cv);

const char *s3_menu_state_name(int state);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_MENU_SCENE_H */
