/*
 * presenter.h —— GPU 呈现器（M1-b 路径 A 核心）
 *
 * 职责：把一张「逻辑画布」（RGBA8888，恒定 640×480 或任意逻辑尺寸）作为
 *       一张 SDL 静态纹理上传到 GPU，再开一个**物理分辨率**窗口，由 GPU 负责
 *       按宽高比（pillarbox / stretch）与滤镜（nearest / linear）把逻辑画布
 *       缩放到窗口。整帧放大从此交给 GPU，不再走 CPU 的 sango3_present()。
 *
 * 设计要点（与 render.h 的铁律一致）：
 *   ① 所有玩法/UI 逻辑仍用逻辑坐标，本模块只做「逻辑画布 → 物理输出」的变换；
 *   ② 缩放、滤波、黑边全部由 GPU 完成（SDL_RenderCopy + SDL_SetTextureScaleMode）；
 *   ③ 窗口可拖动缩放，缩放后按当前窗口尺寸重算内容矩形（pillarbox 自动居中）。
 *
 * 注意：SDL_MAIN_HANDLED 由构建系统定义，这里的 main 保持为真 main。
 */
#ifndef SANGO3_PRESENTER_H
#define SANGO3_PRESENTER_H

#include "render.h"   /* Sango3Aspect / Sango3Filter / Sango3Viewport */
#include <stdint.h>

typedef struct Sango3Presenter Sango3Presenter;

/* 创建一个 GPU 呈现器。
 *   logical_w,h : 逻辑画布尺寸（本作 640×480；show 模式用画布实际尺寸）
 *   out_w,h     : 窗口初始物理分辨率（如 2560×1440 = 2K）
 *   resizable   : 非 0 时窗口可拖动改变大小（拖动后自动重算内容矩形）
 *   aspect      : SANGO3_ASPECT_PILLARBOX（保持逻辑宽高比，两侧留边）/
 *                 SANGO3_ASPECT_STRETCH（拉伸铺满，会变形）
 *   filter      : SANGO3_FILTER_NEAREST（像素完美）/ _BILINEAR / _SHARP（后两者映射为 GPU linear）
 *   title       : 窗口标题（ASCII）
 * 返回 NULL 表示初始化失败（SDL 不可用等）。 */
Sango3Presenter *sango3_presenter_new(int32_t logical_w, int32_t logical_h,
                                      int32_t out_w, int32_t out_h,
                                      int resizable,
                                      Sango3Aspect aspect, Sango3Filter filter,
                                      const char *title);

void sango3_presenter_free(Sango3Presenter *p);

/* 初始化是否成功（窗口/渲染器/纹理均就绪）。 */
int  sango3_presenter_valid(const Sango3Presenter *p);

/* 上传最新一帧逻辑画布像素（RGBA8888，logical_w*logical_h*4 字节）。
 * 纹理为 STATIC，仅更新像素，不改尺寸。 */
void sango3_presenter_upload(Sango3Presenter *p, const uint8_t *rgba);

/* 推进一帧：处理窗口事件（ESC/关闭/拖动缩放）→ 清屏 → 按内容矩形 GPU 缩放绘制 → present。
 * 返回 1 表示用户请求退出（ESC / 关闭窗口），0 表示继续。
 * out_drawn 非 NULL 时累加已绘制帧数（用于自动化验证）。 */
int  sango3_presenter_frame(Sango3Presenter *p, uint32_t *out_drawn);

/* 当前内容矩形（物理像素，pillarbox 居中）。供自检 / UI 命中测试比对。 */
void sango3_presenter_content_rect(const Sango3Presenter *p,
                                   int32_t *x, int32_t *y, int32_t *w, int32_t *h);

/* 当前窗口物理尺寸（拖动缩放后可能变化）。 */
void sango3_presenter_window_size(const Sango3Presenter *p, int32_t *w, int32_t *h);

/* 当前统一缩放系数（逻辑→物理，取内容矩形边长 / 逻辑边长）。 */
float sango3_presenter_scale(const Sango3Presenter *p);

#endif /* SANGO3_PRESENTER_H */
