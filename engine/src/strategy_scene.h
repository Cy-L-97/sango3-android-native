/*
 * strategy_scene.h —— 战略层地图（M3-lite）
 *
 * 原版做法（逆向确认）：战略地图是**一张预烘焙整图**
 *   Shape\AD\Base\Map.shp（1024×768，含地形/城池图标/道路网），
 *   地图框 Shape\AD\Base\MapFrame.shp（520×345），
 *   70 个城市按钮（MenuMap.ini 的 WND_CLASS_CITYBUTTON）按**地图像素坐标**叠在上面。
 *   blk 文件（Map001~067.blk）是**通行属性逻辑层**，不参与渲染。
 *
 * 本模块：整图缩放到逻辑画布 + 城市标记（我方/选中）+ 点击选中 + 顶部信息条。
 */
#ifndef SANGO3_STRATEGY_SCENE_H
#define SANGO3_STRATEGY_SCENE_H

#include <stdint.h>
#include "render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t *(*S3StratReadAsset)(void *ud, const char *path, uint32_t *out_len);
typedef void (*S3StratDrawText)(void *ud, Sango3Canvas *cv, const char *utf8,
                                int32_t x, int32_t y, int32_t w, int32_t h,
                                uint32_t rgb, int font, uint32_t style);

#define S3_STRAT_MAX_CITIES 96

typedef struct S3Strategy S3Strategy;

S3Strategy *s3_strategy_new(S3StratReadAsset read_asset, void *asset_ud,
                            S3StratDrawText draw_text, void *text_ud);
void        s3_strategy_free(S3Strategy *s);

/* 载入地图整图（pak 路径，如 "Shape\\AD\\Base\\Map.shp"）。返回 0 成功。 */
int  s3_strategy_set_map(S3Strategy *s, const char *pak_path);
void s3_strategy_clear_cities(S3Strategy *s);
/* mine: 1 = 己方城池（金色框）
 * mx,my = 城池图标**中心**的地图像素坐标；mw,mh = 图标尺寸（用于标记与命中） */
void s3_strategy_add_city(S3Strategy *s, const char *name,
                          int32_t mx, int32_t my, int32_t mw, int32_t mh, int mine);

/* 视口：画布尺寸 = 视口尺寸，渲染时 1:1 从整图裁取该区域（零重采样）。
 * 由调用方按屏幕宽高比调用 set_viewport —— 视口比例 = 屏幕比例时，
 * COVER 下恰好铺满且无裁切，同时地图可拖动查看全图。 */
void s3_strategy_set_viewport(S3Strategy *s, int32_t vw, int32_t vh);
/* 拖动（dx,dy 为**手指/鼠标**的移动量；内部取反并 clamp 到地图范围内） */
void s3_strategy_pan_view(S3Strategy *s, int32_t dx, int32_t dy);
int  s3_strategy_view_w(const S3Strategy *s);
int  s3_strategy_view_h(const S3Strategy *s);
int  s3_strategy_view_x(const S3Strategy *s);
int  s3_strategy_view_y(const S3Strategy *s);

void s3_strategy_render(S3Strategy *s, Sango3Canvas *cv);
/* 逻辑坐标点击：命中某城则选中它（命中范围按地图缩放后的城市图标大小放宽） */
void s3_strategy_on_click(S3Strategy *s, int32_t lx, int32_t ly);
/* 拖动地图（长按拖动时调用，dx/dy 为逻辑像素增量） */
void s3_strategy_pan(S3Strategy *s, int32_t dx, int32_t dy);

int         s3_strategy_selected(const S3Strategy *s);      /* 下标，-1 = 无 */
int         s3_strategy_count(const S3Strategy *s);
const char *s3_strategy_city_name(const S3Strategy *s, int idx);
int         s3_strategy_city_mine(const S3Strategy *s, int idx);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_STRATEGY_SCENE_H */
