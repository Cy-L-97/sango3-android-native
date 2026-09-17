/*
 * roster.h —— 武将名册（运行时）
 *
 * 用途：行政指令需要「玩家选择执行该指令的武将/军师」，且**每名武将每月只能执行 1 条指令**
 * （2026-09-16 用户复核定稿，见 docs/系统功能设定定稿.md A1/A2/H2）。
 *
 * 数据来源（均在 PAK 内，由 sango3app 解析后 push 进来，本模块不碰 PAK）：
 *   Setting\General01.ini —— 段 [GENERAL]：Name / Strength / Intelligence
 *   Setting\General02.ini —— 段 [ITEM]：Name + City1..City7（值形如 "廬江,野"，`,野` = 在野）
 *
 * ⚠ 等级：原版数据层没有等级字段（运行时成长），本模块初始 1 级。
 *   单将带兵上限 = 等级×40（等级上限暂定 30）—— 定稿 J3 / C3。
 */
#ifndef SANGO3_ROSTER_H
#define SANGO3_ROSTER_H

#ifdef __cplusplus
extern "C" {
#endif

#define S3_ROSTER_MAX 512
#define S3_OFFICER_NAME_CAP 32
#define S3_CITY_NAME_CAP    32
/* 定稿 C6（2026-09-17 裁决）：等级上限 **50**（原暂定 30；因装备需求到 40/45 级、
   官职最高档 40 级。等级/经验曲线见 docs/等级经验曲线与升级规则.md） */
#define S3_LEVEL_MAX        50
#define S3_TROOPS_PER_LEVEL 40      /* 定稿 J3：单将带兵上限 = 等级×40（50 级 → 2000） */
/* 定稿 J7（2026-09-17 用户终裁）：每征 1 兵 = **2 金**（原版 10，我们刻意更便宜） */
#define S3_RECRUIT_GOLD_PER_TROOP 2
/* 定稿 C2/J4（C4 裁决）：訓練升士气 = 5 + 武力/5 + 等级，**结果钳到 25~30**（对齐甲 §4.3） */
#define S3_TRAIN_MORALE_MIN 25
#define S3_TRAIN_MORALE_MAX 30

typedef struct {
    char name[S3_OFFICER_NAME_CAP];
    char city[S3_CITY_NAME_CAP];    /* 所在城池名（""=未定） */
    int  str;                       /* 武力（General01 的 Strength） */
    int  intel;                     /* 智力 */
    int  level;                     /* 运行时等级，初始 = 剧本序号，上限 S3_LEVEL_MAX */
    int  exp;                       /* 当前等级已积累的经验（升级时扣掉本级所需） */
    int  wild;                      /* 1 = 在野（不可作为执行者，只能被招募/搜索） */
    int  mine;                      /* 所在城池是否我方 */
    int  acted;                     /* 本回合已执行过指令（回合结束清空） */
} S3Officer;

typedef struct S3Roster S3Roster;

S3Roster *s3_roster_new(void);
void      s3_roster_free(S3Roster *r);
void      s3_roster_clear(S3Roster *r);
/* 开局初始等级（定稿 Q 区 / 甲 §16.2：第 N 个剧本 → 武将初始 N 级）。**必须在 add 之前调用** */
void      s3_roster_set_base_level(S3Roster *r, int lv);
int       s3_roster_base_level(const S3Roster *r);

/* 追加一名武将；返回下标，满员返回 -1。wild: 名字后带 ,野 */
int  s3_roster_add(S3Roster *r, const char *name, const char *city,
                   int str, int intel, int wild);
/* 按城名批量标记"是否我方"（城池表建好后调用一次） */
void s3_roster_mark_city(S3Roster *r, const char *city, int mine);

int              s3_roster_count(const S3Roster *r);
const S3Officer *s3_roster_at(const S3Roster *r, int i);
S3Officer       *s3_roster_mut(S3Roster *r, int i);

/* 回合结束：清空全部 acted（定稿 A2：守城支援不受 acted 限制，由调用方判断） */
void s3_roster_end_turn(S3Roster *r);

/* 可执行者：该城 && 我方 && 非在野 &&（only_idle 时 acted==0）
 * 结果写入 out（roster 下标），返回条数。 */
int  s3_roster_workers(const S3Roster *r, const char *city,
                       int only_idle, int *out, int out_max);
/* ---- 等级 / 经验（定稿 M 区，见 docs/等级经验曲线与升级规则.md） ---- */
int  s3_officer_exp_need(int level);                 /* 从 level 升到 level+1 所需经验 */
int  s3_officer_add_exp(S3Officer *o, int amount);   /* 加经验并处理升级，返回升了几级 */
int  s3_roster_monthly_growth(S3Roster *r);          /* 月度自动经验（A1 比例制），返回升级人数 */
/* 难度系数（百分比）：简单 70 / 普通 100 / 困难 150（G3 设置项落地后由 UI 调） */
void s3_roster_set_exp_difficulty(int percent);
int  s3_roster_exp_difficulty(void);

int  s3_roster_worker_count(const S3Roster *r, const char *city, int only_idle);

/* 我方**全军**可执行者（跨城，排除在野）—— 定稿 F1「調查」用：
 * 調査的目标是**非我方城池**，执行者只能从我自己的人里挑（2026-09-17 用户确认：
 * "我方全軍任選"，故列表要能跨城）。 */
int  s3_roster_workers_mine_all(const S3Roster *r, int only_idle,
                                int *out, int out_max);
int  s3_roster_worker_count_mine_all(const S3Roster *r, int only_idle);

/* 某城中的武将（**不分敌我**，排除在野）—— 定稿 F2「情報」用（看敌将详情）。 */
int  s3_roster_officers_in_city(const S3Roster *r, const char *city,
                                int *out, int out_max);
int  s3_roster_officer_count_in_city(const S3Roster *r, const char *city);

/* 单将带兵上限（等级×40，等级上限 30） */
int  s3_officer_troop_limit(const S3Officer *o);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_ROSTER_H */
