/*
 * kingdom_scene.h —— 选择君主（开局流程，M3-lite）
 *
 * 原版流程：主菜单「開始遊戲」→ 選擇時期（7 个剧本）→ 選擇君主 → 开局。
 * 本模块渲染**君主列表**（自绘，玩法优先：不显示肖像，只列关键数值）。
 *
 * 数据来源（均由调用方注入，本模块不依赖 PAK/INI）：
 *   · 君主与其城池/人口/金钱 —— 解析 Setting\City0N.ini（N = 1..7 剧本）
 *   · 自定义武将（custom_generals.jsonl）也可作为君主候选
 * 结果由调用方取走（选中君主下标/名称），保存开局状态属调用方职责。
 */
#ifndef SANGO3_KINGDOM_SCENE_H
#define SANGO3_KINGDOM_SCENE_H

#include <stdint.h>
#include "render.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 复用 editor_scene 的同型回调（文本绘制 / 素材读取） */
typedef void (*S3KingDrawText)(void *ud, Sango3Canvas *cv, const char *utf8,
                               int32_t x, int32_t y, int32_t w, int32_t h,
                               uint32_t rgb, int font, uint32_t style);

typedef struct S3Kingdom S3Kingdom;

S3Kingdom *s3_kingdom_new(S3KingDrawText draw_text, void *text_ud);
void       s3_kingdom_free(S3Kingdom *k);

/* 清空并重新开始一个剧本（scenario_name 显示在标题栏） */
void s3_kingdom_begin(S3Kingdom *k, int scenario_id, const char *scenario_name);
/* 追加/累加一个君主；同名君主合并（累加城池/人口/金钱） */
void s3_kingdom_add_lord(S3Kingdom *k, const char *name,
                         int cities, int people, int money, int custom);
/* 给已加入的君主补本人属性（取自 General01.ini，按 Name 匹配）—— I2 大地图版列表用。
 * personality = **相性**（= 原版的"声望"，见定稿 L 区说明）。 */
void s3_kingdom_set_lord_stats(S3Kingdom *k, const char *name,
                               int str, int intel, int justice, int morale,
                               int personality, int portrait);

void s3_kingdom_render(S3Kingdom *k, Sango3Canvas *cv);
void s3_kingdom_on_click(S3Kingdom *k, int32_t lx, int32_t ly);

/* 0 = 继续；1 = 已决定（取 selected 后回战略层）；2 = 取消 */
int         s3_kingdom_result(const S3Kingdom *k);
int         s3_kingdom_selected(const S3Kingdom *k);          /* 下标，-1 = 无 */
const char *s3_kingdom_lord_name(const S3Kingdom *k, int idx);
int         s3_kingdom_lord_cities(const S3Kingdom *k, int idx);
int         s3_kingdom_lord_people(const S3Kingdom *k, int idx);
int         s3_kingdom_lord_money(const S3Kingdom *k, int idx);
int         s3_kingdom_lord_custom(const S3Kingdom *k, int idx);  /* 1 = 自定义武将 */
/* 君主本人属性（I2 大地图版列表用；未补过则为 0） */
int         s3_kingdom_lord_str(const S3Kingdom *k, int idx);
int         s3_kingdom_lord_intel(const S3Kingdom *k, int idx);
int         s3_kingdom_lord_justice(const S3Kingdom *k, int idx);
int         s3_kingdom_lord_morale(const S3Kingdom *k, int idx);
int         s3_kingdom_lord_personality(const S3Kingdom *k, int idx);   /* 相性 */
int         s3_kingdom_lord_portrait(const S3Kingdom *k, int idx);
int         s3_kingdom_count(const S3Kingdom *k);
int         s3_kingdom_scenario(const S3Kingdom *k);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_KINGDOM_SCENE_H */
