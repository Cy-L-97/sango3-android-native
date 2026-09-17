/*
 * gen_picker.h —— 武将/军师选择界面（行政指令的执行者选择）
 *
 * 定稿依据（docs/系统功能设定定稿.md A1/A2）：
 *   · 指令入口流程 = 朝堂点命令 → 切大地图 → 选城 → **玩家选择执行武将/军师** → 执行；
 *   · 每名武将每月只能执行 1 条指令（回合结束清空）→ 已行动者**置灰不可选**。
 *
 * 本模块只负责"长什么样 + 命中 + 结果回传"，不碰游戏数据（roster 只读）。
 * 自绘面板（玩法优先，与原 editor_scene / kingdom_scene 风格一致）。
 */
#ifndef SANGO3_GEN_PICKER_H
#define SANGO3_GEN_PICKER_H

#include <stdint.h>
#include "render.h"
#include "roster.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*S3PickDrawText)(void *ud, Sango3Canvas *cv, const char *utf8,
                               int32_t x, int32_t y, int32_t w, int32_t h,
                               uint32_t rgb, int font, uint32_t style);

typedef struct S3GenPicker S3GenPicker;

S3GenPicker *s3_picker_new(S3PickDrawText draw_text, void *text_ud);
void        s3_picker_free(S3GenPicker *p);

/* 打开选择器：为 city 城的 title 指令挑选执行者。
 * only_idle != 0 时只列"本月未行动"者（默认行为）；置 0 可看全员（守城支援等场合）。 */
void s3_picker_open(S3GenPicker *p, const S3Roster *roster, const char *city,
                    const char *title, int only_idle);
void s3_picker_close(S3GenPicker *p);
int  s3_picker_active(const S3GenPicker *p);
const char *s3_picker_city(const S3GenPicker *p);

void s3_picker_on_move(S3GenPicker *p, int32_t x, int32_t y);
/* 命中（面板矩形内为 1）—— 调用方据此决定事件归属（面板优先于地图） */
int  s3_picker_hit(const S3GenPicker *p, int32_t x, int32_t y);
/* 返回 1 = 事件被消费。选中时 *out_idx 写 roster 下标（否则 -1）；
 * 取消（点取消按钮）时 *out_cancel = 1。 */
int  s3_picker_on_click(S3GenPicker *p, int32_t x, int32_t y,
                        int *out_idx, int *out_cancel);

void s3_picker_render(S3GenPicker *p, Sango3Canvas *cv);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_GEN_PICKER_H */
