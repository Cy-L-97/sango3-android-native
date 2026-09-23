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

/* 功勋初始值（P 区）。【待测 · 2026-09-22】原版无此数据，且**功勋来源（战斗 K8 / 比武 Q2）
 * 尚未实现** —— 取 0 会让「整备·学技」永远显示「功勳不足」，无法实机验收。
 * 故暂给 500（够学 2 个 180 功勋的基础技），并在复核表里标注"先按当前值实现、实测再调"。
 * 战斗线落地后应改回 0（由战斗/比武产出）。 */
#define S3_MERIT_INITIAL 500

/* 已学技上限（定稿 P2：学会即永久保留，属性回落也不遗忘）。
 * 武将技 125 + 军师技 23，故给足容量；每人固定数组，不动态分配。 */
#define S3_MAX_LEARN_BF 128
#define S3_MAX_LEARN_SF 32
/* 整备界面用（2026-09-23）：必杀技槽 8（同 gamedata.h 的 S3_MAX_SA）、小队 8（定稿 N1）。
 * 本头文件不引用 gamedata.h，故在此单列同值常量，避免循环依赖。 */
#define S3_MAX_SA_SLOT 8
#define S3_MAX_SQUAD   8

/* 官位表行（`GenTitle.ini`，69 条；app 解析后传入）。
 * 用户 2026-09-23 裁决：**官位按等级自动授勋**（原版是君主任命，我们自动化）。
 * 表本身是干净的阶梯：Lv1 校尉 40 兵 → 每级 +20 兵 → Lv40 大將軍 400 兵，每级 4 个并列。 */
typedef struct {
    int  level;                        /* 授勋门槛等级 */
    int  soldiers;                     /* 该官位的带兵加成（定稿 J8：上限 = 等级×40 + 此值） */
    char name[S3_OFFICER_NAME_CAP];
} S3TitleRow;

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

    /* ---------------- 2026-09-22 P1 新增（定稿 O 区 / P 区 / U-1） ---------------- */
    int  hp, hp_max;                /* 體力 / 上限（General01 的 HP；战斗未做前为静态） */
    int  mp, mp_max;                /* 技力 / 上限（General01 的 MP） */
    int  justice;                   /* 义理（**隐藏属性**：忠诚初值来源 + 招降/离间因子） */
    int  personality;               /* 相性（**隐藏属性**：招揽/招降/离间/同盟公式用） */
    int  loyalty;                   /* 忠诚度（界面「忠」，0~100；**初值 = 义理**，用户 2026-09-22 裁决） */
    int  merit;                     /* 功勋（P 区：整备学技消费；来源 = 战斗/比武/事件） */
    int  wins, losses;              /* 战绩（信息块 9045 的「戰績 %d勝%d敗」） */
    int  troops;                    /* 当前带兵数（上限 = 等级×40 + 官职加成；由「調兵」分配） */
    int  learn_bf[S3_MAX_LEARN_BF]; int n_learn_bf;   /* 已学武将技编号（No） */
    int  learn_sf[S3_MAX_LEARN_SF]; int n_learn_sf;   /* 已学军师技编号（No） */

    /* ---------------- 2026-09-23 整备界面（`ARRAY` root）+ 官位所需 ---------------- */
    int  rank_no;                   /* 官位在 `GenTitle.ini` 表里的序号（-1 = 未授勋/在野） */
    char rank_name[S3_OFFICER_NAME_CAP];  /* 官位名（驃騎將軍…）：按等级自动授勋 */
    int  rank_soldiers;             /* 该官位带来的带兵加成（GenTitle.Soldiers） */
    int  portrait;                  /* 肖像号 → Shape\Portrait\Portrait{号}.SHP */
    char weapon[S3_OFFICER_NAME_CAP];   /* 装备槽：武器（General01 的 Weapon） */
    char book[S3_OFFICER_NAME_CAP];     /* 装备槽：书 */
    char horse[S3_OFFICER_NAME_CAP];    /* 装备槽：马 */
    int  super_attack[S3_MAX_SA_SLOT];  int n_super_attack;   /* 开局预设必杀技（General01 SuperAttack） */
    int  soldier_type[S3_MAX_SQUAD];    int n_soldier_type;   /* 8 个小队的兵种号（0~8） */
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
/* 完整版（2026-09-22 P1）：额外带入 体力/技力/义理/相性；
 * **忠诚度初值 = 义理**（定稿 O1，用户 2026-09-22 裁决），功勋/战绩/带兵/已学技归零。 */
int  s3_roster_add_ex(S3Roster *r, const char *name, const char *city,
                      int str, int intel, int hp, int mp,
                      int justice, int personality, int wild);

/* 补填"静态档案"（2026-09-23 整备界面用）：肖像号 / 三装备槽 /
 * 开局预设必杀技 / 8 个小队的兵种。add_ex 之后立刻调用（数据来自 General01）。
 * rank_no 传 -1：官位不由数据决定，改由 `s3_roster_auto_titles()` 按等级授予。 */
void s3_officer_set_profile(S3Officer *o, int rank_no, int portrait,
                            const char *weapon, const char *book, const char *horse,
                            const int *super_attack, int n_sa,
                            const int *soldier_type, int n_st);
/* 按城名批量标记"是否我方"（城池表建好后调用一次） */
void s3_roster_mark_city(S3Roster *r, const char *city, int mine);

/* ---- 官位自动授勋（用户 2026-09-23 裁决；定稿 J8 的"官职加成"来源） ----
 * 规则：① 只有**我方非在野**武将授勋（他方/在野 → 无官位，加成 0）；
 *       ② 取 `rows` 中 `level <= 武将等级` 的**最高一档**（定稿 M 区：等级上限 50，
 *          故 40 级后恒为最高档 大將軍一类）；
 *       ③ 同档并列 4 个时按 `roster 下标 % 档内条数` 分散，避免全势力同名（确定性、不随机）。
 * 返回官位发生变化的人数。**开局建名册后、以及每次等级变化后（月度成长）都要调用。** */
int  s3_roster_auto_titles(S3Roster *r, const S3TitleRow *rows, int n_rows);

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

/* 单将带兵上限（**定稿 J8，用户 2026-09-23 重申"按定稿来"**）：
 *   = 等级×40（S3_TROOPS_PER_LEVEL）+ 官位加成（o->rank_soldiers，由自动授勋写入）
 * ⚠ 原版口径 = **仅官位 Soldiers**（截图 呂布驃騎將軍 → 400）；这是**有意的差异**，
 *   不再改（用户 2026-09-23 明确"带兵上限不要动，按照定稿来"）。 */
int  s3_officer_troop_limit(const S3Officer *o);
/* 兼容旧调用（信息块浮层）：rank_soldiers > 0 时用传入值，否则用武将自身已授勋的加成。 */
int  s3_officer_troop_limit_ex(const S3Officer *o, int rank_soldiers);

/* ---------------------------------------------- P1（2026-09-22，定稿 O 区/P 区）
 * 忠诚度：0~100，初值 = 义理（add_ex 内自动设置）。add_loyalty 会把结果钳在 0~100。 */
void s3_officer_add_loyalty(S3Officer *o, int delta);
void s3_officer_set_loyalty(S3Officer *o, int v);

/* 功勋：独立资源。战斗/比武/事件来源待接（K8/Q2），本批先提供累加入口。 */
void s3_officer_add_merit(S3Officer *o, int amount);
/* 消费功勋（整备学技用）：成功返回 1，功勋不足返回 0。 */
int  s3_officer_spend_merit(S3Officer *o, int amount);

/* 已学技（定稿 P2：学会即永久保留，属性回落不遗忘）
 * 返回：0 = 新学会 · 1 = 已学过（不重复）· 2 = 名额满 · 3 = 参数非法 */
int  s3_officer_learn_bf(S3Officer *o, int no);
int  s3_officer_learn_sf(S3Officer *o, int no);
int  s3_officer_knows_bf(const S3Officer *o, int no);
int  s3_officer_knows_sf(const S3Officer *o, int no);

/* 相性相似度（定稿 X-2 方案 B，用户 2026-09-22 裁决）：
 * |差| ≤10 → 100 · ≤24 → 85 · ≤49 → 65 · ≤99 → 40 · ≥100 → 15（0~100）。
 * 用于搜索招揽 / 招降 / 离间 / 同盟成功率。 */
int  s3_personality_similarity(int a, int b);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_ROSTER_H */
