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
    return s3_roster_add_ex(r, name, city, str, intel, 0, 0, 0, 0, wild);
}

/* 完整版（2026-09-22 P1）：额外带入 体力/技力/义理/相性。
 * **忠诚度初值 = 义理**（定稿 O1；用户 2026-09-22 裁决）—— 数据实测义理 19~100、均值 50.5，
 * 正好解释甲文档"吕布剩 30 忠诚就降、夏侯惇要等到个位数"。 */
int s3_roster_add_ex(S3Roster *r, const char *name, const char *city,
                     int str, int intel, int hp, int mp,
                     int justice, int personality, int wild) {
    if (!r || !name || !*name || r->n >= S3_ROSTER_MAX) return -1;
    S3Officer *o = &r->o[r->n];
    memset(o, 0, sizeof *o);
    snprintf(o->name, sizeof o->name, "%.31s", name);
    snprintf(o->city, sizeof o->city, "%.31s", city ? city : "");
    o->str   = str;
    o->intel = intel;
    o->level = r->base_level;           /* 原版无等级数据：开局 = 剧本序号，之后运行时成长 */
    o->wild  = wild ? 1 : 0;
    /* ---- P1 新增 ---- */
    o->hp_max = o->hp = (hp > 0) ? hp : 1;
    o->mp_max = o->mp = (mp > 0) ? mp : 1;
    o->justice     = justice;
    o->personality = personality;
    o->loyalty     = justice;           /* 定稿 O1：初值 = 义理 */
    o->merit       = S3_MERIT_INITIAL;  /* 【待测】见 roster.h 的说明（战斗线落地后改回 0） */
    o->wins = o->losses = 0;
    o->troops = 0;                      /* 由「調兵」分配（本批未做） */
    /* ---- 官位（2026-09-23）：未授勋，等 `s3_roster_auto_titles()` 按等级写入 ---- */
    o->rank_no = -1;
    o->rank_name[0] = '\0';
    o->rank_soldiers = 0;
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
 * 否则后期招到的野将永远是初始等级）。返回升级人数。
 *
 * **M4 等级差加速**（2026-09-27 用户裁定"采纳"）：
 *   E = 非我方且非在野武将的平均等级（s3_roster_enemy_avg_level）；
 *   d = E − 自身等级；我方武将 d ≥ S3_LEVELGAP_D1(8) → ×2、d ≥ S3_LEVELGAP_D2(15) → ×3（封顶）。
 *   ⚠ **只给我方加乘**：实现注见复核表第〇节 —— 双向加速会互相抵消，且会让 AI 滚雪球。
 * 返回升级人数（兼容旧调用）。 */
int s3_roster_monthly_growth(S3Roster *r) {
    return s3_roster_monthly_growth_ex(r, NULL);
}

int s3_roster_monthly_growth_ex(S3Roster *r, int *out_boosted) {
    if (out_boosted) *out_boosted = 0;
    if (!r) return 0;
    const int enemy_avg = s3_roster_enemy_avg_level(r);
    int ups = 0, boosted = 0;
    for (int i = 0; i < r->n; ++i) {
        int got = monthly_exp_of(r->o[i].level);
        if (got <= 0) continue;
        /* 等级差加速：只对我方非在野武将，且我方向后落后时生效 */
        if (enemy_avg > 0 && r->o[i].mine && !r->o[i].wild) {
            const int d = enemy_avg - r->o[i].level;
            int mult = 1;
            if (d >= S3_LEVELGAP_D2)      mult = S3_LEVELGAP_MULT_2;
            else if (d >= S3_LEVELGAP_D1) mult = S3_LEVELGAP_MULT_1;
            if (mult > 1) { got *= mult; ++boosted; }
        }
        if (s3_officer_add_exp(&r->o[i], got) > 0) ups++;
    }
    if (out_boosted) *out_boosted = boosted;
    return ups;
}

/* M4 用：**非我方且非在野**武将的平均等级（即"对手势力"的平均等级）。
 * 无此类武将（例如全图只剩我方）→ 返回 0，调用方据此关闭加速。 */
int s3_roster_enemy_avg_level(const S3Roster *r) {
    if (!r) return 0;
    long long sum = 0;
    int cnt = 0;
    for (int i = 0; i < r->n; ++i) {
        const S3Officer *o = &r->o[i];
        if (o->mine || o->wild) continue;      /* 只看他方在编武将 */
        sum += o->level;
        ++cnt;
    }
    return cnt > 0 ? (int)(sum / cnt) : 0;
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

/* 某城中的**在野**武将（2026-09-27，B2 搜索招揽用）。
 * 在野武将也有 city（`General02` 的 "城市,野"），故按城名筛即可。 */
int s3_roster_wild_in_city(const S3Roster *r, const char *city,
                           int *out, int out_max) {
    if (!r || !city || !out || out_max <= 0) return 0;
    int k = 0;
    for (int i = 0; i < r->n && k < out_max; ++i) {
        const S3Officer *o = &r->o[i];
        if (!o->wild) continue;
        if (strcmp(o->city, city)) continue;
        out[k++] = i;
    }
    return k;
}

int s3_roster_wild_count_in_city(const S3Roster *r, const char *city) {
    if (!r || !city) return 0;
    int k = 0;
    for (int i = 0; i < r->n; ++i) {
        const S3Officer *o = &r->o[i];
        if (!o->wild) continue;
        if (strcmp(o->city, city)) continue;
        ++k;
    }
    return k;
}

/* 补填静态档案（2026-09-23 整备界面用）。名字字段截断到 31 字节以对齐 S3_OFFICER_NAME_CAP。 */
void s3_officer_set_profile(S3Officer *o, int rank_no, int portrait,
                            const char *weapon, const char *book, const char *horse,
                            const int *super_attack, int n_sa,
                            const int *soldier_type, int n_st) {
    if (!o) return;
    o->rank_no  = rank_no;
    o->portrait = portrait;
    snprintf(o->weapon, sizeof o->weapon, "%.31s", weapon ? weapon : "");
    snprintf(o->book,   sizeof o->book,   "%.31s", book   ? book   : "");
    snprintf(o->horse,  sizeof o->horse,  "%.31s", horse  ? horse  : "");
    o->n_super_attack = 0;
    if (super_attack) {
        for (int i = 0; i < n_sa && i < S3_MAX_SA_SLOT; ++i)
            o->super_attack[o->n_super_attack++] = super_attack[i];
    }
    o->n_soldier_type = 0;
    if (soldier_type) {
        for (int i = 0; i < n_st && i < S3_MAX_SQUAD; ++i)
            o->soldier_type[o->n_soldier_type++] = soldier_type[i];
    }
}

/* 单将带兵上限（**定稿 J8，用户 2026-09-23 重申"按定稿来"**）
 *   = 等级×40 + 官位加成（o->rank_soldiers，由 s3_roster_auto_titles 自动授勋写入）
 * ⚠ 原版是**仅官位值**（驃騎將軍 400）；这是有意的差异，不再改。 */
int s3_officer_troop_limit(const S3Officer *o) {
    if (!o) return 0;
    int lv = o->level;
    if (lv < 1) lv = 1;
    if (lv > S3_LEVEL_MAX) lv = S3_LEVEL_MAX;
    int cap = lv * S3_TROOPS_PER_LEVEL + o->rank_soldiers;
    if (cap < 0) cap = 0;
    return cap;
}

/* 兼容旧调用（信息块浮层）：传 0 时用武将自身已授勋的加成 */
int s3_officer_troop_limit_ex(const S3Officer *o, int rank_soldiers) {
    if (rank_soldiers > 0) {
        int lv = o ? o->level : 1;
        if (lv < 1) lv = 1;
        if (lv > S3_LEVEL_MAX) lv = S3_LEVEL_MAX;
        return lv * S3_TROOPS_PER_LEVEL + rank_soldiers;
    }
    return s3_officer_troop_limit(o);
}

/* ---------------------------------------------------------------- 官位自动授勋
 * 用户 2026-09-23 裁决："官位可以按照等级自动授勋"（原版是君主任命）。
 * 规则见 roster.h：取最高可达档，**同档统一取档内第一个**（用户第二轮："同档统一给同一个名"）。 */
int s3_roster_auto_titles(S3Roster *r, const S3TitleRow *rows, int n_rows) {
    if (!r || !rows || n_rows <= 0) return 0;
    int changed = 0;
    for (int i = 0; i < r->n; ++i) {
        S3Officer *o = &r->o[i];
        /* 只有我方非在野武将授勋（在野/他方 = 无官位） */
        if (o->wild || !o->mine) {
            if (o->rank_no != -1 || o->rank_soldiers != 0 || o->rank_name[0]) {
                o->rank_no = -1;
                o->rank_name[0] = '\0';
                o->rank_soldiers = 0;
                ++changed;
            }
            continue;
        }
        /* 取 level <= 武将等级 的最高一档；同档并列多个 → **统一取档内第一个**
         * （用户 2026-09-23 裁决："同档统一给同一个名吧"，故不再按下标分散）。 */
        int best_lv = -1, first = -1;
        for (int k = 0; k < n_rows; ++k) {
            if (rows[k].level > o->level) continue;
            if (rows[k].level > best_lv) { best_lv = rows[k].level; first = k; }
        }
        if (first < 0) continue;                    /* 等级低于最低官位门槛（Lv1 校尉）→ 无官位 */
        const S3TitleRow *tr = &rows[first];
        if (o->rank_no == first && o->rank_soldiers == tr->soldiers &&
            strcmp(o->rank_name, tr->name) == 0)
            continue;                               /* 官位未变 */
        o->rank_no = first;
        snprintf(o->rank_name, sizeof o->rank_name, "%.31s", tr->name);
        o->rank_soldiers = tr->soldiers;
        ++changed;
    }
    return changed;
}

/* ================================================================== P1（2026-09-22）
 * 忠诚度 / 功勋 / 已学技 / 相性相似度 —— 定稿 O 区、P 区、X-2。
 * 口径来源：用户 2026-09-22 在 docs/P1-忠诚度·功勋·相性·外交_调研复核表.md 的裁决。 */

void s3_officer_set_loyalty(S3Officer *o, int v) {
    if (!o) return;
    if (v < 0) v = 0;
    if (v > 100) v = 100;                 /* 定稿 O1：忠诚度 0~100 运行时值 */
    o->loyalty = v;
}

void s3_officer_add_loyalty(S3Officer *o, int delta) {
    if (!o) return;
    s3_officer_set_loyalty(o, o->loyalty + delta);
}

void s3_officer_add_merit(S3Officer *o, int amount) {
    if (!o || amount <= 0) return;
    o->merit += amount;                   /* 功勋不设上限（用户 2026-09-22 裁决） */
}

int s3_officer_spend_merit(S3Officer *o, int amount) {
    if (!o || amount < 0) return 0;
    if (o->merit < amount) return 0;      /* 对应 Text.ini 9042「功勳不足」 */
    o->merit -= amount;
    return 1;
}

int s3_officer_knows_bf(const S3Officer *o, int no) {
    if (!o || no <= 0) return 0;
    for (int i = 0; i < o->n_learn_bf; ++i) if (o->learn_bf[i] == no) return 1;
    return 0;
}

int s3_officer_knows_sf(const S3Officer *o, int no) {
    if (!o || no <= 0) return 0;
    for (int i = 0; i < o->n_learn_sf; ++i) if (o->learn_sf[i] == no) return 1;
    return 0;
}

int s3_officer_learn_bf(S3Officer *o, int no) {
    if (!o || no <= 0) return 3;
    if (s3_officer_knows_bf(o, no)) return 1;              /* 定稿 P2：已学不重复、不遗忘 */
    if (o->n_learn_bf >= S3_MAX_LEARN_BF) return 2;
    o->learn_bf[o->n_learn_bf++] = no;
    return 0;
}

int s3_officer_learn_sf(S3Officer *o, int no) {
    if (!o || no <= 0) return 3;
    if (s3_officer_knows_sf(o, no)) return 1;
    if (o->n_learn_sf >= S3_MAX_LEARN_SF) return 2;
    o->learn_sf[o->n_learn_sf++] = no;
    return 0;
}

/* 相性相似度（定稿 X-2 **方案 B**，用户 2026-09-22 裁决）：
 * 数据依据 —— Personality 实测 1~149、按 25/75/125 聚成派系（派系间恒差 50），
 * 两两 |差| 均值 45.7 / 中位 41，故用**分段**表达"同派系 / 跨派系"比线性更贴合语义。 */
int s3_personality_similarity(int a, int b) {
    int d = a - b; if (d < 0) d = -d;
    if (d <= 10)  return 100;
    if (d <= 24)  return 85;
    if (d <= 49)  return 65;
    if (d <= 99)  return 40;
    return 15;
}

/* ============================================ 本批（2026-09-27）：招揽 / 離間
 * 口径：`docs/P1剩余-忠诚变动·搜索招揽·離間_调研复核表.md`（用户逐行裁定）。
 * 公式均标【待测】，先按当前值实现并留常量。 */

/* X-3 搜索招揽**在野**武将成功率（%）。C10 裁决：只看执行者本人与野将的相性，
 * **与君主是谁无关**（"这个人搜不来，就换一个相性近的人去搜"）。 */
int s3_recruit_chance(const S3Officer *actor, const S3Officer *target) {
    if (!actor || !target) return 0;
    const int sim = s3_personality_similarity(actor->personality, target->personality);
    int p = S3_RECRUIT_P_BASE + actor->intel / 5 + actor->level + (sim - 50) / 2;
    if (p < S3_RECRUIT_P_MIN) p = S3_RECRUIT_P_MIN;
    if (p > S3_RECRUIT_P_MAX) p = S3_RECRUIT_P_MAX;
    return p;
}

/* 招揽成功 → 野将转我方。忠诚度**保持 = 义理**（不因"换主"重置为别的值；
 * 定稿 O1 的初值口径在 add_ex 里已设，这里不动它）。 */
int s3_officer_recruit(S3Officer *o, const char *new_city) {
    if (!o || !o->wild) return 0;              /* 只有在野武将可被招揽 */
    o->wild = 0;
    o->mine = 1;
    if (new_city && *new_city)
        snprintf(o->city, sizeof o->city, "%.31s", new_city);
    /* 转投后官位需按等级重授 → 置回未授勋，由 s3_roster_auto_titles() 补 */
    o->rank_no = -1;
    o->rank_name[0] = '\0';
    o->rank_soldiers = 0;
    return 1;
}

/* O-5 離間忠诚下降量（用户 2026-09-27 裁定采纳）：
 *   Δ = round((100 − 義理)/10) + round(|相性差|/25)，钳 S3_ESTRANGE_MIN~MAX(1~20)。
 * 义理越高越难降（关/赵/诸葛/周瑜 义理 100 → 只剩相性项 0~6，常为 1~2 点）。 */
int s3_estrange_delta(const S3Officer *target, const S3Officer *actor) {
    if (!target) return 0;
    int d = 0;
    if (actor) { d = target->personality - actor->personality; if (d < 0) d = -d; }
    /* 四舍五入：把 +half 折进分子（整数除法） */
    int delta = ((100 - target->justice) + S3_ESTRANGE_BASE_DIV / 2) / S3_ESTRANGE_BASE_DIV;
    delta    += (d + S3_ESTRANGE_SIM_DIV / 2) / S3_ESTRANGE_SIM_DIV;
    if (delta < S3_ESTRANGE_MIN) delta = S3_ESTRANGE_MIN;
    if (delta > S3_ESTRANGE_MAX) delta = S3_ESTRANGE_MAX;
    return delta;
}

/* F3 離間成功率（%）：
 *   P = (|相性差| / 148) × 70 × (1 − 義理/100) × 威望系数(暂 1.0)，钳 0~90。
 * 义理 100 的名将 → **恒 0%**（"高义理名将基本免疫"，符合甲口径）。
 * 威望系数等 L4 数值（T15 威望系统）落地后再接 —— 现在固定 1.0。 */
int s3_estrange_chance(const S3Officer *target, const S3Officer *actor) {
    if (!target || !actor) return 0;
    int d = target->personality - actor->personality; if (d < 0) d = -d;
    if (d > S3_ESTRANGE_P_DIFF) d = S3_ESTRANGE_P_DIFF;
    int p = (d * S3_ESTRANGE_P_SPAN) / S3_ESTRANGE_P_DIFF;   /* 相性项 0~70 */
    p = p * (100 - target->justice) / 100;                   /* 义理衰减 */
    if (p < 0) p = 0;
    if (p > S3_ESTRANGE_P_MAX) p = S3_ESTRANGE_P_MAX;
    return p;
}

/* ================================================== 装备加成 / 有效属性（2026-09-23）
 * 用户要求（原版行为）：技的**武/智区间门槛用"有效属性"判定** ——
 *   "48+ 武力才可学的技，武力不到就不显示；装备上武器达标后才显示"。
 * 数据来源：`Thing.ini` 的 `Increment`
 *   · 武器 Type=2 → 武力（如 吳鉤 +1 … 方天畫戟 +12）
 *   · 书   Type=3 → 智力（如 春秋左傳 +3 … 幻世錄 +20）
 * 装备变更系统未做（首版装备 = General01 的 Weapon/Book），故加成在开局算一次。 */
void s3_officer_set_equip_bonus(S3Officer *o, int str_bonus, int intel_bonus) {
    if (!o) return;
    o->equip_str = (str_bonus   > 0) ? str_bonus   : 0;
    o->equip_int = (intel_bonus > 0) ? intel_bonus : 0;
}

int s3_officer_eff_str(const S3Officer *o) {
    if (!o) return 0;
    int v = o->str + o->equip_str;
    return (v > 0) ? v : 0;
}

int s3_officer_eff_intel(const S3Officer *o) {
    if (!o) return 0;
    int v = o->intel + o->equip_int;
    return (v > 0) ? v : 0;
}
