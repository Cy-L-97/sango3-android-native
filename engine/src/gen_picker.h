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

/* 选择范围（不同指令的候选来源不同）：
 *   MY_CITY   —— 某座**我方城**的可执行武将（一般内政/军事指令，定稿 A1）
 *   MY_ALL    —— 我方**全军**（跨城）可执行者 —— 定稿 F1「調查」：目标是敌城，
 *                执行者只能从自己人里挑（2026-09-17 用户确认"我方全軍任選"）
 *   ANY_CITY  —— 指定城里的**全部武将（不分敌我）** —— 定稿 F2「情報」：看敌将详情。
 *                此模式下不按"本月已行动"置灰（看情报不消耗行动）。
 *   WILD_CITY —— 指定城里的**在野武将** —— 定稿 B2：搜索命中「人才」后挑一名招揽
 *                （2026-09-27 新增）。同样不受"本月已行动"约束（执行的是发起搜索的那位）。 */
typedef enum {
    S3_PICK_MY_CITY  = 0,
    S3_PICK_MY_ALL   = 1,
    S3_PICK_ANY_CITY = 2,
    S3_PICK_WILD_CITY= 3
} S3PickScope;

S3GenPicker *s3_picker_new(S3PickDrawText draw_text, void *text_ud);
void        s3_picker_free(S3GenPicker *p);

/* 打开选择器：为 city 城的 title 指令挑选执行者（city 可为 NULL/空 =
 * 不限定城池，配合 scope = S3_PICK_MY_ALL 表示"我方全军"）。
 *
 * ⚠ 定稿 A2 口径：**本月已行动的武将在列表里置灰且不可选**（不是隐藏）——
 * 故默认调用方应传 only_idle = 0（列全部）；only_idle != 0 表示"只列本月未行动者"
 * （守城支援等需要"看全员"之外的过滤场合才用）。S3_PICK_ANY_CITY 不受此约束。 */
void s3_picker_open(S3GenPicker *p, const S3Roster *roster, const char *city,
                    const char *title, int only_idle, S3PickScope scope);
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
