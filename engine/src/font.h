/*
 * font.h —— TTF 字体渲染层（M1-b 字体显示）
 *
 * 职责：把「UTF-8 文本」渲染成 RGBA8888 位图，供画布叠加 / GPU 缩放显示。
 *   业务代码只给 UTF-8（已含繁→简层），本模块只负责「码点 → 像素」。
 *
 * 两种模式（需求⑤高清化的字体侧落点）：
 *   S3_FONT_PIXEL —— 像素风字体（FusionPixel12，简/繁两套），关 hinting 保持锐利，
 *                    配合 presenter 的 nearest 滤镜，整帧放大后是干净的像素感。
 *   S3_FONT_HD    —— 矢量字体（霞鹜文楷 LXGWWenKai，简繁通用），正常 hinting，
 *                    配合 presenter 的 linear 滤镜得到平滑高清文字。
 *
 * 回退链（pick_font_path）：项目 assets/fonts 优先 → 系统字体（Windows）兜底 → 失败返回 NULL。
 *   项目用开源字体（OFL/SIL，可再分发），Android 端打包项目字体即可，不依赖系统字体。
 *
 * 颜色：SDL_ttf 的 Blended 渲染用前景色 + 边缘 alpha，所以直接传 0xRRGGBB 即得彩色抗锯齿文本。
 */
#ifndef SANGO3_FONT_H
#define SANGO3_FONT_H

#include "text.h"   /* S3Lang */
#include <stdint.h>

typedef enum {
    S3_FONT_PIXEL = 0,   /* 像素风（锐利，配合 nearest） */
    S3_FONT_HD    = 1    /* 矢量高清（平滑，配合 linear） */
} S3FontMode;

typedef struct S3Font S3Font;

/* 初始化 TTF 子系统并记录字体目录（fonts_dir 为 engine/assets/fonts 绝对路径）。
 * 可重复调用（幂等）。返回 0 成功，非 0 失败。 */
int  s3_font_init(const char *fonts_dir);
void s3_font_quit(void);
int  s3_font_ready(void);

/* 打开一个字体实例。
 *   mode     : 像素 / 高清（决定字体族）
 *   size_px  : 逻辑像素高度（本作逻辑画布 640×480，一般 16~28）
 *   lang     : 显示语言（仅像素模式区分 hans/hant 两套文件；高清用通用字体）
 * 返回 NULL 表示无可用字体（调用方应降级：不渲染文本或换模式）。 */
S3Font *s3_font_open(S3FontMode mode, int size_px, S3Lang lang);
void    s3_font_close(S3Font *f);

/* 渲染 UTF-8 文本为 RGBA8888（R,G,B,A 内存序），含 alpha（抗锯齿边缘覆盖度）。
 *   color    : 0xRRGGBB 前景色
 *   out_rgba : 由本函数 malloc（调用方 free），尺寸 w*h*4
 * 返回 0 成功；非 0 失败（无字体 / 空串），此时 *out_rgba = NULL。 */
int s3_font_render_utf8(const S3Font *f, const char *utf8,
                        uint8_t **out_rgba, int *w, int *h, uint32_t color);

#endif /* SANGO3_FONT_H */
