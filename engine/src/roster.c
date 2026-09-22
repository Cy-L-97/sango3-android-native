/*
 * roster.c —— 武将名册实现（见 roster.h）
 */
#include "roster.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct S3Roster {
    S3Officer o[S3_ROSTER_MAX];
    int       n;
    int       base_level;    /* 开局初始等级（定稿 Q 区 / 甲 §16.2：第 N 个剧本 → 武将 N 级） */
};

S3Roster *s3_roster_new(void) {
    S3Roster *r = (S3Roster *)calloc(1, sizeof(S3Roster));
    if (r) r->base_level = 1;
    return r;
}

void s3_roster_free(S3Roster *r) { free(r); }

void s3_roster_clear(S3Roster *r) {
    if (r) { memset(r->o, 0, sizeof r->o); r->n = 0; }
}

/* 定稿 Q 区 / 甲 §16.2：第 N 个剧本，武将初始等级 = N（新君主除外——我们暂不区分） */
void s3_roster_set_base_level(S3Roster *r, int lv) {
    if (!r) return;
    if (lv < 1) lv = 1;
    if (lv > S3_LEVEL_MAX) lv = S3_LEVEL_MAX;
    r->base_level = lv;
}

int s3_roster_base_level(const S3Roster *r) { return r ? r->base_level : 1; }

int s3_roster_add(S3Roster *r, const char *name, const char *city,
                  int str, int intel, int wild) {
    if (!r || !name || !*name || r->n >= S3_ROSTER_MAX) return -1;
    S3Officer *o = &r->o[r->n];
    memset(o, 0, sizeof *o);
    snprintf(o->name, sizeof o->name, "%.31s", name);
    snprintf(o->city, sizeof o->city, "%.31s", city ? city : "");
    o->str   = str;
    o->intel = intel;
    o->level = r->base_level;           /* 原版无等级数据：开局 = 剧本序号，之后运行时成长 */
    o->wild  = wild ? 1 : 0;
    return r->n++;
}

/* ---------------------------------------------------------------- 等级 / 经验
 * 曲线（定稿 M 区 / docs/等级经验曲线与升级规则.md，唯一权威）：
 *   **原版表** `Setting\Game.ini [GENERALEXP]` 的 1~50 级累计经验（2026-09-22 解出并采用）：
 *     L2 = 200 · L10 = 6,258 · L33 = 101,657 · L40 = 173,009 · L50 = 338,539
 *   （此前用 `round(97.65625 × (L-1)^2)`，源自用户 CSV，属近似模型 —— 已按用户裁决换成原版表，
 *     见 待办 T22 / docs/武将登场与事件系统.md 5.2）
 *   本表 S3_EXP_NEED[L] = 从 L 升到 L+1 所需经验 = 累计[L+1] − 累计[L]（L = 1..49），
 *   L = 0 未用；**L = 50 未用（置 0，等级上限）**。
 * 月度自动经验（C9 裁决 **A1 比例制**）：月经验 = S3_EXP_NEED[等级] × 档位比例 / 100
 *   档位 = L2~5:100 · L6~15:35 · L16~25:20 · L26~35:12 · L36~45:8 · L46~50:6 （单位：%）
 *   → 每级 1 / 3 / 5~6 / 9 / 13 / 14 月；**1 级到 50 级约 382 个月 ≈ 31.8 年**
 *     （换用原版表后总时长与旧公式的 391 月几乎一致，升级手感不变）
 */
static const int S3_EXP_NEED[51] = {
    0,
200, 310, 426, 546, 674, 808, 948, 1096, 1250, 1413,
1584, 1763, 1951, 2148, 2356, 2573, 2803, 3042, 3295, 3559,
3837, 4129, 4436, 4757, 5096, 5450, 5822, 6214, 6624, 7055,
7509, 7983, 8483, 9007, 9558, 10135, 10742, 11379, 12048, 12751,
13487, 14263, 15075, 15930, 16825, 17767, 18756, 19793, 20883, 0
};

int s3_officer_exp_need(int level) {
    if (level < 1) return S3_EXP_NEED[1];
    if (level >= S3_LEVEL_MAX) return 0;
    return S3_EXP_NEED[level];
}

/* 难度系数（定稿 M5：简单 0.7 / 普通 1.0 / 困难 1.5）。设置项（G3）尚未做界面，
 * 先用模块级变量（默认 = 普通 100），提供 setter 供将来接 UI。 */
static int g_exp_diff_pct = 100;

void s3_roster_set_exp_difficulty(int percent) {
    if (percent < 10) percent = 10;
    if (percent > 300) percent = 300;
    g_exp_diff_pct = percent;
}

int s3_roster_exp_difficulty(void) { return g_exp_diff_pct; }

static int monthly_exp_of(int level) {
    if (level < 1) level = 1;
    if (level >= S3_LEVEL_MAX) return 0;
    int rate = 6;                                   /* L46~50 */
    if (level <= 5)       rate = 100;
    else if (level <= 15) rate = 35;
    else if (level <= 25) rate = 20;
    else if (level <= 35) rate = 12;
    else if (level <= 45) rate = 8;
    int e = S3_EXP_NEED[level] * rate / 100;
    e = e * g_exp_diff_pct / 100;                   /* 难度系数 */
    if (e < 1) e = 1;
    return e;
}

/* 给一名武将加经验并处理升级（可连升）；返回升了几级 */
int s3_officer_add_exp(S3Officer *o, int amount) {
    if (!o || amount <= 0) return 0;
    o->exp += amount;
    int ups = 0;
    while (o->level < S3_LEVEL_MAX) {
        int nd = S3_EXP_NEED[o->level];
        if (nd <= 0 || o->exp < nd) break;
        o->exp -= nd;
        o->level++;
        ups++;
    }
    if (o->level >= S3_LEVEL_MAX) o->exp = 0;
    return ups;
}

/* 月度结算：给名册内**所有**武将发自动经验（定稿 M2；在野也涨，
 * 否则后期招到的野将永远是初始等级）。返回升级人数。 */
int s3_roster_monthly_growth(S3Roster *r) {
    if (!r) return 0;
    int ups = 0;
    for (int i = 0; i < r->n; ++i) {
        int got = monthly_exp_of(r->o[i].level);
        if (got <= 0) continue;
        if (s3_officer_add_exp(&r->o[i], got) > 0) ups++;
    }
    return ups;
}

void s3_roster_mark_city(S3Roster *r, const char *city, int mine) {
    if (!r || !city) return;
    for (int i = 0; i < r->n; ++i)
        if (!strcmp(r->o[i].city, city)) r->o[i].mine = mine ? 1 : 0;
}

int s3_roster_count(const S3Roster *r) { return r ? r->n : 0; }

const S3Officer *s3_roster_at(const S3Roster *r, int i) {
    return (r && i >= 0 && i < r->n) ? &r->o[i] : NULL;
}

S3Officer *s3_roster_mut(S3Roster *r, int i) {
    return (r && i >= 0 && i < r->n) ? &r->o[i] : NULL;
}

void s3_roster_end_turn(S3Roster *r) {
    if (!r) return;
    for (int i = 0; i < r->n; ++i) r->o[i].acted = 0;
}

int s3_roster_workers(const S3Roster *r, const char *city,
                      int only_idle, int *out, int out_max) {
    if (!r || !city || !out || out_max <= 0) return 0;
    int k = 0;
    for (int i = 0; i < r->n && k < out_max; ++i) {
        const S3Officer *o = &r->o[i];
        if (!o->mine || o->wild) continue;
        if (strcmp(o->city, city)) continue;
        if (only_idle && o->acted) continue;
        out[k++] = i;
    }
    return k;
}

int s3_roster_worker_count(const S3Roster *r, const char *city, int only_idle) {
    if (!r || !city) return 0;
    int k = 0;
    for (int i = 0; i < r->n; ++i) {
        const S3Officer *o = &r->o[i];
        if (!o->mine || o->wild) continue;
        if (strcmp(o->city, city)) continue;
        if (only_idle && o->acted) continue;
        ++k;
    }
    return k;
}

int s3_roster_workers_mine_all(const S3Roster *r, int only_idle,
                               int *out, int out_max) {
    if (!r || !out || out_max <= 0) return 0;
    int k = 0;
    for (int i = 0; i < r->n && k < out_max; ++i) {
        const S3Officer *o = &r->o[i];
        if (!o->mine || o->wild) continue;
        if (only_idle && o->acted) continue;
        out[k++] = i;
    }
    return k;
}

int s3_roster_worker_count_mine_all(const S3Roster *r, int only_idle) {
    if (!r) return 0;
    int k = 0;
    for (int i = 0; i < r->n; ++i) {
        const S3Officer *o = &r->o[i];
        if (!o->mine || o->wild) continue;
        if (only_idle && o->acted) continue;
        ++k;
    }
    return k;
}

int s3_roster_officers_in_city(const S3Roster *r, const char *city,
                               int *out, int out_max) {
    if (!r || !city || !out || out_max <= 0) return 0;
    int k = 0;
    for (int i = 0; i < r->n && k < out_max; ++i) {
        const S3Officer *o = &r->o[i];
        if (o->wild) continue;                 /* 在野不算该城常驻武将 */
        if (strcmp(o->city, city)) continue;   /* 敌我不限 */
        out[k++] = i;
    }
    return k;
}

int s3_roster_officer_count_in_city(const S3Roster *r, const char *city) {
    if (!r || !city) return 0;
    int k = 0;
    for (int i = 0; i < r->n; ++i) {
        const S3Officer *o = &r->o[i];
        if (o->wild) continue;
        if (strcmp(o->city, city)) continue;
        ++k;
    }
    return k;
}

int s3_officer_troop_limit(const S3Officer *o) {
    if (!o) return 0;
    int lv = o->level;
    if (lv < 1) lv = 1;
    if (lv > S3_LEVEL_MAX) lv = S3_LEVEL_MAX;
    return lv * S3_TROOPS_PER_LEVEL;
}
