/*
 * diplomacy.h —— D 区外交（同盟/解盟）+ L 区威望 的**规则层**（纯公式，不碰 UI/素材）
 *
 * 口径来源：`docs/D区外交与L4威望_调研复核表.md`（用户 2026-09-28 逐行裁定）。
 * 拆成独立模块的原因：这些公式要能**离线核对**（同 `roster_probe` 的做法），
 * 且 app 与探针共用同一份实现，避免"文档写一套、代码写一套"。
 *
 * 数据依据（原版）：
 *   · `Nation.ini` 的 `Friendship` —— **友好度 0~70**（70 友好 / 50 默认 / 30 差 / 0 敌对），
 *     我方（自己）不列入 → 我方视为 70。
 *   · `Text.ini` 7200/7201/7202 = **寶物 / 金錢 / 遊說**（同盟三方式）；
 *     7306/7307 = 確定與%s同盟嗎？/ 確定與%s解盟嗎？。
 *   · 原版**没有"威望"字段与文案** → 威望数值由我们自定（本文件即唯一出处）。
 */
#ifndef SANGO3_DIPLOMACY_H
#define SANGO3_DIPLOMACY_H

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ 友好度（0~70） */
#define S3_FRIEND_MAX      70       /* 原版值域上限（我方 = 此值） */
#define S3_FRIEND_MIN      0
#define S3_FRIEND_DEFAULT  50       /* Nation.ini 未列出该势力时的缺省 */
#define S3_FRIEND_HOSTILE  0        /* 敌对（解盟后落到这里） */
#define S3_FRIEND_ALLY_MIN 60       /* 友好度 ≥ 60 才可能缔结同盟（用户 2026-09-28 裁定） */

int s3_friend_clamp(int v);

/* ------------------------------------------------------------------ 威望（L4，0~100） */
#define S3_PRESTIGE_MIN          0
#define S3_PRESTIGE_MAX          100
#define S3_PRESTIGE_INIT         50      /* 玩家势力初值；AI 不单独维护（视为 50） */
#define S3_PRESTIGE_KILL_NATION  15      /* ① 灭一国（最核心途径） */
#define S3_PRESTIGE_BATTLE_WIN    3      /* ② 关键战役胜利（以少胜多/攻克重兵城） */
#define S3_PRESTIGE_ARENA_WIN     5      /* ③ 每年比武夺冠 */
#define S3_PRESTIGE_EVENT_GOOD    2      /* ④ 随机正向事件（讨伐猛兽/平定黄巾残部） */
#define S3_PRESTIGE_BREAK_ALLY   20      /* ↓ 主动撕毁同盟（大量扣除） */
#define S3_PRESTIGE_LOSE_CITY     3      /* ↓ 每次战败/丢城 */
#define S3_PRESTIGE_EXEC_DIV     20      /* ↓ 处决被俘知名武将：−(义理/20)，义理 100 → −5 */
/* 事件解锁阈值（Q 区事件系统未做 → 本批只留常量） */
#define S3_PRESTIGE_EVT_EMPEROR  70      /* 献帝封赏 */
#define S3_PRESTIGE_EVT_SCHOLAR  50      /* 名士来投 */

int s3_prestige_clamp(int v);
/* 作用系数（L1 的系数化）：
 *   同盟成功率 `+(威望−50)/4` · 招揽/招降成功率 `+(威望−50)/5`。
 *   ⚠ F3 離間里的"威望系数"**保持 1.0** —— 它取决于**敌方君主**的威望，我们没有 AI 数据，
 *     不假装有（等做"敌方 AI 离间我方"时再启用）。 */
int s3_prestige_ally_bonus(int prestige);      /* (威望−50)/4，钳 −12~+12 */
int s3_prestige_recruit_bonus(int prestige);   /* (威望−50)/5，钳 −10~+10 */

/* ------------------------------------------------------------------ 同盟成功率 */
#define S3_DIPLO_P_BASE   20
#define S3_DIPLO_P_MIN     5
#define S3_DIPLO_P_MAX    95
/* P% = 20 + 智力/4 + (友好−50)/2 + (威望−50)/4，钳 5~95。
 * 校验：友好 60 · 智 80 · 威望 50 → 45%；友好 70 · 智 95 · 威望 70 → 58%。
 * 友好 < S3_FRIEND_ALLY_MIN 时**必定婉拒**（返回 0）。 */
int s3_diplo_ally_chance(int intel, int friendliness, int prestige);

/* ------------------------------------------------------------------ 三方式的友好度增量
 * ① 赠送宝物：clamp(round(Attraction/10), 1, 10)（Attraction 24~100 → +2~+10）
 * ② 给予金钱：clamp(金额/100, 1, 10)（300/500/1000 金 → +3/+5/+10）
 * ③ 游说：    3 + 智力/25 + (威望−50)/5，钳 1~12（智 100·威望 50 → +7） */
#define S3_DIPLO_GIFT_MIN  1
#define S3_DIPLO_GIFT_MAX  10
#define S3_DIPLO_TALK_MIN  1
#define S3_DIPLO_TALK_MAX  12
int s3_diplo_gift_gain(int attraction);
int s3_diplo_money_gain(int money);
int s3_diplo_talk_gain(int intel, int prestige);

/* ------------------------------------------------------------------ 解盟惩罚（定稿 D2） */
#define S3_DIPLO_BREAK_LOYALTY 10      /* 全体我方（非在野）武将忠诚 −10 */

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_DIPLOMACY_H */
