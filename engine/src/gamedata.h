/*
 * gamedata.h —— 规则数据层 + 需求③④的运行时规则
 *
 * 数据来源：原版 Setting\*.ini（Big5），经 text.c 解码为 UTF-8。
 *   General01.ini → 武将（421 名）
 *   Thing.ini     → 物品与装备加成（103 件）
 *   Soldier.ini   → 兵种（9 个）
 *   BFMagic.ini   → 武将技（125 个）
 *   Game.ini      → 全局规则（升级经验曲线等）
 *
 * 需求④的实现契约（见 docs/必杀技学习规则.md，已定稿）：
 *   · 判定依据是【当前攻击力】= 基础武力 + 武器加成，**实时求值、不缓存**；
 *   · 触发时机 = 攻击力变化（装备变更 / 入队 / 其他加成来源）→ on_attack_changed；
 *   · 条件：当前攻击力 ≥ 80 且 非野兽 且 尚未拥有；
 *   · 方向性 monotonic：只授予、不撤销（攻击力回落保留已学技能）；
 *   · 敌我一致：本层不区分阵营，调用方对双方使用同一入口。
 *
 * 需求③的实现契约：必杀技用**位集合**（int known[] + 计数器）表达，
 * 不设 3 个上限；原版 SuperAttack 字段本身支持多值，容量限制只在代码层。
 */
#ifndef SANGO3_GAMEDATA_H
#define SANGO3_GAMEDATA_H

#include "ini.h"

#define S3_NAME_CAP     64
#define S3_TEXT_CAP     256
#define S3_MAX_SA       8            /* 原版共 8 种必杀技（SuperAttack01~08） */
#define S3_MAX_SOLDIERS 8

/* 规则常量（引擎唯一出处，改这里即改行为） */
#define S3_SUPER_ATTACK_ATTACK_THRESHOLD 80
#define S3_BEAST_INTELLIGENCE_MAX        20
#define S3_BEAST_HP_MIN                  150

/* 需求②：士兵上限。原版硬编码 400（数据层查无此值），本引擎就是一个变量。 */
#define S3_SOLDIER_LIMIT_DEFAULT 1000
#define S3_SOLDIER_LIMIT_VANILLA 400

/* ------------------------------------------------------------------ 物品 */
typedef struct {
    char name[S3_NAME_CAP];        /* UTF-8 原名（繁体） */
    char display[S3_NAME_CAP];     /* 按显示语言转换后的名字 */
    int  type;                     /* 1..8，见 SLOT 表 */
    int  slot;                     /* 0=soldier_token 1=weapon 2=book 3=horse … */
    char count[32];                /* 原版 Count 字段（字符串，非数量） */
    int  level;                    /* 使用等级 */
    char icon[S3_NAME_CAP];
    char statement[S3_TEXT_CAP];   /* 说明文字（UTF-8，未转换） */
    int  increment[3];
    int  strength_bonus;           /* 仅 Type=2 武器计入武力，其余为 0 */
} S3Item;

/* Type → 槽位语义（依据 Thing.ini 的 Statement 逐件校准） */
enum {
    S3_SLOT_OTHER = 0, S3_SLOT_SOLDIER_TOKEN, S3_SLOT_WEAPON, S3_SLOT_BOOK,
    S3_SLOT_HORSE, S3_SLOT_FORMATION, S3_SLOT_TREASURE, S3_SLOT_HERB, S3_SLOT_FUNGUS
};

/* ------------------------------------------------------------------ 武将 */
typedef struct {
    int  no;
    char name[S3_NAME_CAP];
    char display[S3_NAME_CAP];
    int  strength, intelligence, hp, mp;
    int  justice, morale, personality;
    int  portrait, weapon_type, bfai, bfshape, rank, sex;

    int  super_attack[S3_MAX_SA];  int n_super_attack;   /* 来源①：开局预设 */
    int  learned[S3_MAX_SA];       int n_learned;        /* 来源②：运行时自动习得 */
    int  soldier_type[S3_MAX_SOLDIERS]; int n_soldier_type;

    char weapon[S3_NAME_CAP];      /* 装备槽存的是【物品名】，不是索引 */
    char book[S3_NAME_CAP];
    char horse[S3_NAME_CAP];

    /* 派生（每次由装备实时算出，不缓存语义上的"当前攻击力"） */
    int  weapon_bonus;
    int  is_beast;                 /* intelligence <= 20 且 hp >= 150 */
} S3General;

/* ------------------------------------------------------------------ 兵种 */
typedef struct {
    int  no;
    char name[S3_NAME_CAP];
    char name_adv[S3_NAME_CAP];
    int  res_id;
    int  start_hp, add_hp;
    int  start_power, add_power;
    int  hit_rate[10];
} S3Soldier;

/* ---------------------------------------------------------------- 武将技 */
typedef struct {
    int  no;
    char name[S3_NAME_CAP];
    int  mp, power, level, contribution, attribute;
    char spec[S3_TEXT_CAP];
} S3Magic;

/* ------------------------------------------------------------ 全局规则表 */
typedef struct { char *key; int value; } S3RuleEntry;
typedef struct { char *name; S3RuleEntry *items; int n; } S3RuleMap;

/* ------------------------------------------------------------------ 总表 */
typedef struct {
    S3Ini  *game_ini;              /* Game.ini 常驻，供规则查询 */
    S3RuleMap *rules;   int n_rules;

    S3General *generals;  int n_generals;
    S3Item    *items;     int n_items;
    S3Soldier *soldiers;  int n_soldiers;
    S3Magic   *magics;    int n_magics;

    int soldier_limit;             /* 需求②：默认 1000 */
    int ok;                        /* 关键表是否全部载入 */
    char error[256];
} S3GameData;

/* 载入 setting_dir 下的 INI（该目录须含 General01.ini / Thing.ini / Soldier.ini /
 * BFMagic.ini / Game.ini）。失败仍返回非 NULL 对象，用 s3_gamedata_ok() 检查。 */
S3GameData *s3_gamedata_load(const char *setting_dir);
void        s3_gamedata_free(S3GameData *d);
int         s3_gamedata_ok(const S3GameData *d);
const char *s3_gamedata_error(const S3GameData *d);

const S3General *s3_gamedata_general_at(const S3GameData *d, int idx);
const S3General *s3_gamedata_general_by_no(const S3GameData *d, int no);
const S3Item    *s3_gamedata_item_by_name(const S3GameData *d, const char *name);

/* ------------------------------------------------------------ 规则引擎 API */
/* 物品的武力加成（非武器恒为 0） */
int s3_item_strength_bonus(const S3Item *it);

/* 当前攻击力 = 基础武力 + 当前武器加成。**实时求值**。 */
int s3_general_current_attack(const S3GameData *d, const S3General *g);

/* 是否已拥有某必杀技（预设 ∪ 自动习得） */
int s3_general_knows_super_attack(const S3General *g, int id);
/* 已拥有总数 */
int s3_general_super_attack_count(const S3General *g);

/* 自动学习时选哪一招。原版行为已用数据验证（见 docs 第五节）：
 * 编号分布近似均匀且与 WeaponType / 武力无相关 → 按【随机】实现，可播种以便复现。
 * 若日后逆向出确定规则，替换本策略即可，不影响其它逻辑。 */
typedef int (*S3SuperAttackPicker)(const S3General *g);
void s3_general_set_super_attack_picker(S3SuperAttackPicker fn);
/* 默认策略：尚未拥有的编号中等概率随机取一个；8 招全有则返回 0。 */
int  s3_general_pick_super_attack_default(const S3General *g);

/* 装备武器（传 NULL 或空串 = 卸下）→ 内部触发 on_attack_changed。返回 0 成功。 */
int s3_general_set_weapon(S3GameData *d, S3General *g, const char *weapon_name);

/* on_attack_changed 事件：求值条件并授予，**每次事件最多授予 1 招**。
 * 授予条件：当前攻击力 >= 80 且 非野兽 且 当前不持有任何必杀技。
 * 已持有者不再重复授予 —— 既符合规则文档的字面口径，也避免反复换装刷满 8 招。
 * granted/granted_max 接收本次编号；返回本次新学会数量（0 或 1）。 */
int s3_general_on_attack_changed(const S3GameData *d, S3General *g,
                                 int *granted, int granted_max);

/* 全局规则查询：取 Game.ini 中某 section 的某键整数 */
int s3_gamedata_rule_int(const S3GameData *d, const char *section, const char *key, int def);
int s3_gamedata_rule_count(const S3GameData *d);

/* 兵种升级/加成查询（后接战斗系统） */
const S3Soldier *s3_gamedata_soldier_by_no(const S3GameData *d, int no);

#endif /* SANGO3_GAMEDATA_H */
