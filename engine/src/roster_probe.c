/*
 * roster_probe.c —— 名册规则验证器（2026-09-27，P1 剩余批次）
 *
 * 目的：把本批新增的**规则公式**做成可离线核对的口径出口（纯规则、不依赖 PAK/素材）：
 *   · X-3 搜索招揽在野武将成功率  `s3_recruit_chance()`
 *   · O-5 離間忠诚下降量          `s3_estrange_delta()`
 *   · F3  離間成功率              `s3_estrange_chance()`
 *   · M4  等级差加速（月度经验）   `s3_roster_monthly_growth_ex()`
 *
 * 用法：sango3roster            → 打印 `key=value` 行（供 Python 基准逐项比对）
 *       sango3roster --check     → 内置期望值，全部命中输出 ALL OK 并返回 0，否则 1
 *
 * 对应文档：docs/P1剩余-忠诚变动·搜索招揽·離間_调研复核表.md
 */
#include "roster.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;

/* --- 期望值校验（--check） --- */
static void expect_i(const char *key, int got, int want) {
    const int ok = (got == want);
    if (!ok) ++g_fail;
    printf("%-34s got=%-6d want=%-6d %s\n", key, got, want, ok ? "OK" : "**MISMATCH**");
}

/* 手工造将：名称 / 武力 / 智力 / 义理 / 相性 / 等级 / 阵营（0=他方·1=我方 / 野） */
static int mk(S3Roster *r, const char *name, const char *city,
              int str, int intel, int justice, int pers, int level,
              int mine, int wild) {
    int i = s3_roster_add_ex(r, name, city, str, intel, 100, 100, justice, pers, wild);
    if (i < 0) return -1;
    S3Officer *o = s3_roster_mut(r, i);
    if (o) { o->level = level; o->mine = mine ? 1 : 0; }
    return i;
}

int main(int argc, char **argv) {
    const int check = (argc > 1 && !strcmp(argv[1], "--check"));
    S3Roster *r = s3_roster_new();
    s3_roster_set_base_level(r, 1);

    /* ============ 角色（相性参照：25 = 曹操系 / 75 = 劉備系 / 125 = 孫吳系） ============ */
    const int I_ZHANGJIAO = mk(r, "張角",   "平原", 72, 95, 52,   25, 1, 1, 0);
    const int I_LVBU      = mk(r, "呂布",   "洛陽", 100, 36, 27,  25, 1, 1, 0);
    const int I_GUANYU    = mk(r, "關羽",   "陳留", 99, 82, 100,  75, 1, 0, 0);  /* 他方 */
    const int I_ZHUGELIANG= mk(r, "諸葛亮", "新野", 68, 100, 100, 75, 1, 0, 0);  /* 他方 */
    const int I_NORM      = mk(r, "某將",   "汝南", 70, 60, 50,   75, 1, 0, 0);  /* 义理 50 · 他方 */
    /* 在野候选（被招揽对象；城 = 平原，与張角同城） */
    const int I_WILD_SAME = mk(r, "野將同", "平原", 60, 50, 60,   25, 1, 0, 1);
    const int I_WILD_FAR  = mk(r, "野將遠", "平原", 60, 50, 60,  125, 1, 0, 1);
    if (I_ZHANGJIAO < 0 || I_LVBU < 0 || I_GUANYU < 0 || I_ZHUGELIANG < 0 ||
        I_NORM < 0 || I_WILD_SAME < 0 || I_WILD_FAR < 0) {
        printf("roster probe: 造将失败\n");
        return 2;
    }
    const S3Officer *zhangjiao = s3_roster_at(r, I_ZHANGJIAO);
    const S3Officer *lvbu      = s3_roster_at(r, I_LVBU);
    const S3Officer *guanyu    = s3_roster_at(r, I_GUANYU);
    const S3Officer *norm      = s3_roster_at(r, I_NORM);
    const S3Officer *wild_same = s3_roster_at(r, I_WILD_SAME);
    const S3Officer *wild_far  = s3_roster_at(r, I_WILD_FAR);

    /* ============================== 1) X-3 招揽成功率 ==============================
     * P% = 40 + 智力/5 + 等级 + (sim − 50)/2，钳 5~95。
     *  張角(智95·級1) vs 同相性(sim 100) → 40+19+1+25 = 85
     *  張角(智95·級1) vs 跨派系(sim 15)  → 40+19+1-17 = 43
     *  呂布(智36·級1) vs 同相性(sim 100) → 40+7+1+25  = 73
     *  呂布(智36·級1) vs 跨派系(sim 15)  → 40+7+1-17  = 31 */
    const int rc_zj_same = s3_recruit_chance(zhangjiao, wild_same);
    const int rc_zj_far  = s3_recruit_chance(zhangjiao, wild_far);
    const int rc_lb_same = s3_recruit_chance(lvbu, wild_same);
    const int rc_lb_far  = s3_recruit_chance(lvbu, wild_far);
    printf("# --- X-3 招揽成功率 ---\n");
    printf("recruit.zhangjiao.same=%d\n", rc_zj_same);
    printf("recruit.zhangjiao.far=%d\n",  rc_zj_far);
    printf("recruit.lvbu.same=%d\n",      rc_lb_same);
    printf("recruit.lvbu.far=%d\n",       rc_lb_far);
    printf("recruit.sim.same=%d\n",       s3_personality_similarity(25, 25));
    printf("recruit.sim.far=%d\n",        s3_personality_similarity(25, 125));

    /* ============================== 2) O-5 离间降幅 ==============================
     * Δ = round((100−義理)/10) + round(|相性差|/25)，钳 1~20。
     *  關羽(义理100) · |差|50 → round(0)=0 + round(2)=2 → 2
     *  諸葛亮(义理100) · |差|0 → 0 + 0 = 0 → 钳到 1
     *  呂布(义理27, 相性25) · 执行者 相性125 → |差|100 → round(7.3)=7 + round(4)=4 = 11
     *  某將(义理50, 相性75) · 执行者 相性25 → |差|50  → round(5)=5 + 2 = 7 */
    printf("# --- O-5 离间降幅 ---\n");
    printf("delta.guanyu=%d\n",    s3_estrange_delta(guanyu, lvbu));      /* 25 vs 75 → 50 */
    printf("delta.zhugeliang=%d\n", s3_estrange_delta(s3_roster_at(r, I_ZHUGELIANG), s3_roster_at(r, I_ZHUGELIANG))); /* |差|=0 */
    printf("delta.lvbu=%d\n",      s3_estrange_delta(lvbu, wild_far));    /* 125 vs 25 → 100 */
    printf("delta.norm=%d\n",      s3_estrange_delta(norm, zhangjiao));   /* 75 vs 25 → 50 */

    /* ============================== 3) F3 离间成功率 ==============================
     * P = (|差|/148)×70 × (1−义理/100)，钳 0~90。
     *  關羽(义理100) → 任何相性差都 = 0（免疫）
     *  呂布(义理27) · |差|100 → (7000/148)=47 → 47×73/100 = 34
     *  某將(义理50) · |差|50  → (3500/148)=23 → 23×50/100 = 11 */
    printf("# --- F3 离间成功率 ---\n");
    printf("chance.guanyu=%d\n", s3_estrange_chance(guanyu, lvbu));
    printf("chance.lvbu=%d\n",   s3_estrange_chance(lvbu, wild_far));
    printf("chance.norm=%d\n",   s3_estrange_chance(norm, zhangjiao));

    /* ============================== 4) M4 等级差加速 ==============================
     * E = 非我方且非在野 的平均等级。上面名册里他方在编 = 關羽/諸葛亮/某將（3 人·等级 1）
     * → E = 1 → 我方（張角/呂布·等级 1）d = 0 → 不加速。
     * 再把 3 名他方提升到 20 级 → E = 20 → 我方 d = 19 ≥ 15 → ×3。 */
    printf("# --- M4 等级差 ---\n");
    printf("levelgap.enemy_avg.lv1=%d\n", s3_roster_enemy_avg_level(r));
    {
        int boosted = -1;
        s3_roster_monthly_growth_ex(r, &boosted);           /* 第 1 次：d = 0，不加乘 */
        printf("levelgap.boosted.d0=%d\n", boosted);
    }
    /* 把 3 名他方在编抬到 20 级（绕开升级逻辑，直接改等级以构造场景） */
    s3_roster_mut(r, I_GUANYU)->level      = 20;
    s3_roster_mut(r, I_ZHUGELIANG)->level  = 20;
    s3_roster_mut(r, I_NORM)->level        = 20;
    printf("levelgap.enemy_avg.lv20=%d\n", s3_roster_enemy_avg_level(r));
    {
        /* 我方两人等级 1：d = 19 → ×3，故本次 boosted 应为 2（我方非在野共 2 人） */
        int boosted = -1;
        s3_roster_monthly_growth_ex(r, &boosted);
        printf("levelgap.boosted.d19=%d\n", boosted);
    }
    /* 抬到我方等级 4：d = 16 ≥ 15 → 仍 ×3；再抬到 12：d = 8 ≥ 8 → ×2 */
    s3_roster_mut(r, I_ZHANGJIAO)->level = 12;
    s3_roster_mut(r, I_LVBU)->level      = 12;
    {
        int boosted = -1;
        s3_roster_monthly_growth_ex(r, &boosted);
        printf("levelgap.boosted.d8=%d\n", boosted);        /* 两人都 d=8 → ×2 → 2 */
    }
    /* 等级差为负（我方向前）→ 不加速 */
    s3_roster_mut(r, I_ZHANGJIAO)->level = 30;
    s3_roster_mut(r, I_LVBU)->level      = 30;
    {
        int boosted = -1;
        s3_roster_monthly_growth_ex(r, &boosted);
        printf("levelgap.boosted.ahead=%d\n", boosted);     /* 0 */
    }

    /* ============================== 5) 招揽归属转移 ============================== */
    printf("# --- 招揽归属转移 ---\n");
    printf("recruit.before=%d/%d/%s\n", wild_same->wild, wild_same->mine, wild_same->city);
    {
        S3Officer *o = s3_roster_mut(r, I_WILD_SAME);
        const int ok = s3_officer_recruit(o, "平原");
        printf("recruit.ok=%d\n", ok);
        printf("recruit.after=%d/%d/%s\n", o->wild, o->mine, o->city);
        printf("recruit.loyalty_kept=%d\n", o->loyalty);    /* 仍 = 义理 60 */
        printf("recruit.again=%d\n", s3_officer_recruit(o, "平原"));  /* 非在野 → 0 */
    }

    if (check) {
        printf("\n== expected ==\n");
        expect_i("recruit.zhangjiao.same", rc_zj_same, 85);
        expect_i("recruit.zhangjiao.far",  rc_zj_far,  43);
        expect_i("recruit.lvbu.same",      rc_lb_same, 73);
        expect_i("recruit.lvbu.far",       rc_lb_far,  31);
        expect_i("delta.guanyu",           s3_estrange_delta(guanyu, lvbu), 2);
        expect_i("delta.lvbu",             s3_estrange_delta(lvbu, wild_far), 11);
        expect_i("delta.norm",             s3_estrange_delta(norm, zhangjiao), 7);
        expect_i("chance.guanyu",          s3_estrange_chance(guanyu, lvbu), 0);
        expect_i("chance.lvbu",            s3_estrange_chance(lvbu, wild_far), 34);
        expect_i("chance.norm",            s3_estrange_chance(norm, zhangjiao), 11);
        printf("\n%s\n", g_fail ? "**FAIL**" : "ALL OK");
    }

    s3_roster_free(r);
    return g_fail ? 1 : 0;
}
