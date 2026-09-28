/*
 * diplomacy.c —— D 区外交 + L 区威望 的规则实现（见 diplomacy.h）
 *
 * 全部为**纯函数**：不依赖 PAK / 素材 / 名册，便于离线核对（`sango3roster --check` 的 diplo 段）。
 */
#include "diplomacy.h"

static int clamp_i(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int s3_friend_clamp(int v) { return clamp_i(v, S3_FRIEND_MIN, S3_FRIEND_MAX); }

int s3_prestige_clamp(int v) { return clamp_i(v, S3_PRESTIGE_MIN, S3_PRESTIGE_MAX); }

int s3_prestige_ally_bonus(int prestige) {
    return clamp_i((s3_prestige_clamp(prestige) - S3_PRESTIGE_INIT) / 4, -12, 12);
}

int s3_prestige_recruit_bonus(int prestige) {
    return clamp_i((s3_prestige_clamp(prestige) - S3_PRESTIGE_INIT) / 5, -10, 10);
}

/* P% = 20 + 智力/4 + (友好−50)/2 + (威望−50)/4，钳 5~95；友好 < 60 → 0（必定婉拒）。 */
int s3_diplo_ally_chance(int intel, int friendliness, int prestige) {
    const int fr = s3_friend_clamp(friendliness);
    if (fr < S3_FRIEND_ALLY_MIN) return 0;      /* 未达门槛：必定婉拒（走 7120~7129 文案） */
    int p = S3_DIPLO_P_BASE + intel / 4 + (fr - 50) / 2 + s3_prestige_ally_bonus(prestige);
    return clamp_i(p, S3_DIPLO_P_MIN, S3_DIPLO_P_MAX);
}

/* ① 赠送宝物：clamp(round(Attraction/10), 1, 10) */
int s3_diplo_gift_gain(int attraction) {
    if (attraction <= 0) return 0;              /* 该物品不是宝物 → 不能送 */
    return clamp_i((attraction + 5) / 10, S3_DIPLO_GIFT_MIN, S3_DIPLO_GIFT_MAX);
}

/* ② 给予金钱：clamp(金额/100, 1, 10) */
int s3_diplo_money_gain(int money) {
    if (money <= 0) return 0;
    return clamp_i(money / 100, 1, S3_DIPLO_GIFT_MAX);
}

/* ③ 游说：3 + 智力/25 + (威望−50)/5，钳 1~12 */
int s3_diplo_talk_gain(int intel, int prestige) {
    int g = 3 + intel / 25 + (s3_prestige_clamp(prestige) - S3_PRESTIGE_INIT) / 5;
    return clamp_i(g, S3_DIPLO_TALK_MIN, S3_DIPLO_TALK_MAX);
}
