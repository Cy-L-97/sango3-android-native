/*
 * lord_picker.h —— 選擇君主（大地图版，定稿 I2）
 *
 * 原版做法（用户附原版截图 + 2026-09-17 定稿 I2）：**直接在大地图上选君主** ——
 *   地图 + 左侧君主列表 + 选中君主的城池高亮插旗 + 右侧城池面板 + 底部统计栏 + 肖像。
 * 旧的 `kingdom_scene`（640×480 纯列表）退役。
 *
 * ⚠ 本模块只画**叠加层**（列表 / 统计栏 / 肖像 / 按钮），地图与城池标记仍由
 *   `strategy_scene` 负责 —— 两者叠加 = 完整界面。这样插旗/城名/金框都能直接复用。
 *
 * 口径注记：
 *   · 列表列 = 名 / 武 / 智 / 忠 / 士（**没有「聲望」** —— 2026-09-17 用户核查原版，
 *     确认原版不存在声望系统）；
 *   · 忠 = `General01.ini` 的 Justice，士 = 其 Morale；
 *   · 肖像 = `Shape\Portrait\Portrait{Portrait号}.SHP`（421 张，正好一将一张）。
 */
#ifndef SANGO3_LORD_PICKER_H
#define SANGO3_LORD_PICKER_H

#include <stdint.h>
#include "render.h"
#include "kingdom_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t *(*S3LordReadAsset)(void *ud, const char *path, uint32_t *out_len);

typedef struct S3LordPick S3LordPick;

S3LordPick *s3_lordpick_new(S3KingDrawText draw_text, void *text_ud,
                            S3LordReadAsset read_asset, void *asset_ud);
void        s3_lordpick_free(S3LordPick *lp);

/* 绑定数据（只读，不持有生命周期）。每次进入该界面调用。 */
void s3_lordpick_bind(S3LordPick *lp, const S3Kingdom *k);
void s3_lordpick_reset(S3LordPick *lp);

/* 底部统计栏的数据（由调用方按当前选中君主算好传入，避免本模块依赖城池数据层） */
void s3_lordpick_set_stats(S3LordPick *lp, int cities, int forts, int generals,
                           long long troops, long long people, long long money);

void s3_lordpick_render(S3LordPick *lp, Sango3Canvas *cv);
/* 命中：返回 1 = 事件被消费。选中某行时 *out_lord = 该行下标（否则 -1）；
 * 点「決定」置 *out_ok=1，点「取消」置 *out_cancel=1。 */
int  s3_lordpick_on_click(S3LordPick *lp, int32_t x, int32_t y,
                          int *out_lord, int *out_ok, int *out_cancel);
void s3_lordpick_on_move(S3LordPick *lp, int32_t x, int32_t y);

int  s3_lordpick_page(const S3LordPick *lp);
int  s3_lordpick_selected(const S3LordPick *lp);
void s3_lordpick_select(S3LordPick *lp, int idx);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_LORD_PICKER_H */
