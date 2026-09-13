/*
 * sango3view.h —— sango3view 跨文件接口
 */
#ifndef SANGO3_VIEW_H
#define SANGO3_VIEW_H

#include <stdint.h>
#include "render.h"   /* Sango3Aspect / Sango3Filter */
#include "font.h"    /* S3FontMode */

#ifdef __cplusplus
extern "C" {
#endif

/* sango3view show 的全部可调参数（含 M1-b GPU 路径 A 与字体显示层） */
typedef struct {
    uint32_t frames;          /* >0 渲染指定帧数后退出（自动化验证）；0=持续显示 */
    int32_t  out_w, out_h;    /* 窗口物理分辨率（默认 2560×1440） */
    int      resizable;       /* 非0=窗口可拖动缩放 */
    Sango3Aspect aspect;      /* pillarbox / stretch */
    Sango3Filter filter;      /* nearest / bilinear / sharp */

    /* 字体叠加层（NULL/空表示不渲染文本） */
    const char *fonts_dir;    /* 字体目录（engine/assets/fonts 绝对路径）；NULL=不初始化字体 */
    const char *text;         /* 叠加文本（UTF-8，已含繁→简层）；NULL=不叠加 */
    S3FontMode  font_mode;    /* S3_FONT_PIXEL（锐利）/ S3_FONT_HD（平滑） */
    int         font_size;    /* 逻辑像素高度 */
    uint32_t    font_color;   /* 0xRRGGBB 前景色 */
    int         font_selftest;/* 非0=跑内置中文自检（验证 CJK glyph 可用）并报告 */
} Sango3ViewParams;

/* SDL2 显示后端：把 RGBA8888 画布（可选叠加文本）显示到窗口（M1-b GPU 路径 A）。
 * 逻辑画布作为纹理上传，开物理分辨率窗口由 GPU 缩放显示。
 * 返回 0 正常退出，非 0 错误码。 */
int sango3view_show_sdl(const uint8_t *rgba, uint32_t w, uint32_t h, const Sango3ViewParams *p);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_VIEW_H */
