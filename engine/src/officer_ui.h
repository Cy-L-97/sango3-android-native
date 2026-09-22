/*
 * officer_ui.h —— 武将信息块 + 整备·学技列表（P1 / 定稿 U-1、U-2、P6、P7）
 *
 * 数据依据（原版排版，2026-09-22 解出）：
 *   · Text.ini **String 9045** = 武将信息块排版：
 *       姓名 戰績%d勝%d敗 / 君主 武力 體力 / 等級 智力 技力 / 忠誠 士氣 帶兵數 %d/%d /
 *       功勳 經驗值 %d/%d
 *   · Text.ini **9040~9044** = 整备学技文案：「所需功勳：%d」「學習%s？」「功勳不足」
 *       「獲得武將技 %s」「獲得軍師技 %s」
 *
 * 口径来源：`docs/P1-忠诚度·功勋·相性·外交_调研复核表.md`（用户 2026-09-22 裁决
 * 忠诚度初值 = 义理 · 本批含「整备·学技」）。
 *
 * 本模块只负责"长什么样 + 命中 + 结果回传"，不碰游戏数据（roster / bfmagic 只读）。
 * 渲染尺寸按当前画布自适应（`z = cv->w <= 640 ? 1 : 2`，与 gen_picker/lord_picker 同）。
 */
#ifndef SANGO3_OFFICER_UI_H
#define SANGO3_OFFICER_UI_H

#include <stdint.h>
#include "render.h"
#include "roster.h"
#include "gamedata.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*S3OuiDrawText)(void *ud, Sango3Canvas *cv, const char *utf8,
                              int32_t x, int32_t y, int32_t w, int32_t h,
                              uint32_t rgb, int font, uint32_t style);

typedef struct S3OfficerUI S3OfficerUI;

typedef enum {
    S3_OUI_NONE  = 0,
    S3_OUI_CARD  = 1,     /* 武将信息块（只读，点任意处关闭） */
    S3_OUI_LEARN = 2      /* 整备·学技（列出可学技，选中即回传下标） */
} S3OuiMode;

S3OfficerUI *s3_oui_new(S3OuiDrawText draw_text, void *text_ud);
void         s3_oui_free(S3OfficerUI *u);

/* 弹信息块：lord = 所属君主名（可传 NULL → 显示「—」）。任何点击关闭。 */
void s3_oui_show_card(S3OfficerUI *u, const S3Officer *o, const char *lord);

/* 弹学技列表：list 是**已经过滤好的可学技**（调用方用 s3_magic_learnable() 过滤）；
 * count 为「该表总数」，仅用于文案（原版 125 武将技 / 23 军师技）。 */
void s3_oui_show_learn(S3OfficerUI *u, const S3Officer *o,
                       const S3Magic *const *list, int n, int is_sf);

void s3_oui_close(S3OfficerUI *u);
int  s3_oui_active(const S3OfficerUI *u);
int  s3_oui_mode(const S3OfficerUI *u);

void s3_oui_on_move(S3OfficerUI *u, int32_t x, int32_t y);
int  s3_oui_hit(const S3OfficerUI *u, int32_t x, int32_t y);
/* 返回：-1 = 未处理（调用方继续）· -2 = 关闭 · -3 = 翻页/取消等已消费 · >=0 = 选中第 i 项 */
int  s3_oui_on_click(S3OfficerUI *u, int32_t x, int32_t y);

void s3_oui_render(S3OfficerUI *u, Sango3Canvas *cv);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_OFFICER_UI_H */
