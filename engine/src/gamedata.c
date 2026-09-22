/*
 * gamedata.c —— 规则数据层与需求③④的运行时规则实现
 */
#include "gamedata.h"
#include "rng.h"
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- 小工具 */
/* 按 UTF-8 边界安全拷贝（避免把一个汉字截成半个） */
static void copy_str(char *dst, size_t cap, const char *src) {
    if (cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) --n;  /* 回退到字符边界 */
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int slot_of(int type) {
    switch (type) {
        case 1: return S3_SLOT_SOLDIER_TOKEN;
        case 2: return S3_SLOT_WEAPON;
        case 3: return S3_SLOT_BOOK;
        case 4: return S3_SLOT_HORSE;
        case 5: return S3_SLOT_FORMATION;
        case 6: return S3_SLOT_TREASURE;
        case 7: return S3_SLOT_HERB;
        case 8: return S3_SLOT_FUNGUS;
        default: return S3_SLOT_OTHER;
    }
}

/* 显示名：按当前显示语言把 UTF-8 原文转换一次（简中模式做繁→简） */
static void make_display(char *dst, size_t cap, const char *utf8) {
    if (s3_text_ready()) {
        s3_text_display(utf8, dst, cap);
    } else {
        copy_str(dst, cap, utf8);
    }
}

static void *grow(void *p, int *cap, int need, size_t elem) {
    if (need <= *cap) return p;
    int nc = *cap ? *cap * 2 : 32;
    while (nc < need) nc *= 2;
    void *np = realloc(p, (size_t)nc * elem);
    if (!np) return NULL;
    *cap = nc;
    return np;
}

static int join(char *dst, size_t cap, const char *dir, const char *name) {
    size_t a = strlen(dir), b = strlen(name);
    if (a + 1 + b + 1 > cap) return -1;
    memcpy(dst, dir, a);
    size_t i = a;
    if (i && dst[i - 1] != '/' && dst[i - 1] != '\\') dst[i++] = '/';
    memcpy(dst + i, name, b + 1);
    return 0;
}

/* ---------------------------------------------------------------- 载入 */
static void load_items(S3GameData *d, const S3Ini *ini) {
    int cap = 0;
    for (int i = 0; i < ini->n_sections; ++i) {
        const S3IniSection *s = &ini->sections[i];
        if (strcmp(s->name, "ITEM") != 0) continue;
        if (!s3_ini_val_at(s, "Name", 0)) continue;      /* Python: "Name" in t */

        void *np = grow(d->items, &cap, d->n_items + 1, sizeof(S3Item));
        if (!np) return;
        d->items = (S3Item *)np;
        S3Item *it = &d->items[d->n_items++];
        memset(it, 0, sizeof(*it));

        const char *nm = s3_ini_str(s, "Name", "");
        copy_str(it->name, sizeof it->name, nm);
        make_display(it->display, sizeof it->display, it->name);
        it->type  = s3_ini_int(s, "Type", 0);
        it->slot  = slot_of(it->type);
        copy_str(it->count, sizeof it->count, s3_ini_str(s, "Count", ""));
        it->level = s3_ini_int(s, "Level", 0);
        copy_str(it->icon, sizeof it->icon, s3_ini_str(s, "Icon", ""));
        copy_str(it->statement, sizeof it->statement, s3_ini_str(s, "Statement", ""));
        it->increment[0] = s3_ini_int(s, "Increment",  0);
        it->increment[1] = s3_ini_int(s, "Increment2", 0);
        it->increment[2] = s3_ini_int(s, "Increment3", 0);
        /* 关键：只有武器(Type=2)的 Increment 加武力；书/马/其它一律不计入攻击力 */
        it->strength_bonus = (it->type == 2) ? it->increment[0] : 0;
        /* 2026-09-22 补（P1/R 区用）：搜索可发现率（0 = 搜不到）、宝物价值（外交赠礼） */
        it->find_rate  = s3_ini_int(s, "FindRate", 0);
        it->attraction = s3_ini_int(s, "Attraction", 0);
    }
}

static void load_generals(S3GameData *d, const S3Ini *ini) {
    int cap = 0;
    for (int i = 0; i < ini->n_sections; ++i) {
        const S3IniSection *s = &ini->sections[i];
        if (strcmp(s->name, "GENERAL") != 0) continue;
        if (!s3_ini_val_at(s, "No", 0)) continue;

        void *np = grow(d->generals, &cap, d->n_generals + 1, sizeof(S3General));
        if (!np) return;
        d->generals = (S3General *)np;
        S3General *g = &d->generals[d->n_generals++];
        memset(g, 0, sizeof(*g));

        const char *nm = s3_ini_str(s, "Name", "");
        copy_str(g->name, sizeof g->name, nm);
        make_display(g->display, sizeof g->display, g->name);

        g->no           = s3_ini_int(s, "No", 0);
        g->strength     = s3_ini_int(s, "Strength", 0);
        g->intelligence = s3_ini_int(s, "Intelligence", 0);
        g->hp           = s3_ini_int(s, "HP", 0);
        g->mp           = s3_ini_int(s, "MP", 0);
        g->justice      = s3_ini_int(s, "Justice", 0);
        g->morale       = s3_ini_int(s, "Morale", 0);
        g->personality  = s3_ini_int(s, "Personality", 0);
        g->portrait     = s3_ini_int(s, "Portrait", 0);
        g->weapon_type  = s3_ini_int(s, "WeaponType", 0);
        g->bfai         = s3_ini_int(s, "BFAI", 0);
        g->bfshape      = s3_ini_int(s, "BFShape", 0);
        g->rank         = s3_ini_int(s, "Rank", 0);
        g->sex          = s3_ini_int(s, "Sex", 0);

        g->n_super_attack = s3_ini_int_list_digits(s, "SuperAttack", g->super_attack, S3_MAX_SA);
        g->n_soldier_type = s3_ini_int_list(s, "SoldierType", g->soldier_type, S3_MAX_SOLDIERS);

        copy_str(g->weapon, sizeof g->weapon, s3_ini_str(s, "Weapon", ""));
        copy_str(g->book,   sizeof g->book,   s3_ini_str(s, "Book",   ""));
        copy_str(g->horse,  sizeof g->horse,  s3_ini_str(s, "Horse",  ""));

        g->weapon_bonus = 0;   /* 由 s3_general_set_weapon / refresh 求值 */
        g->is_beast = (g->intelligence <= S3_BEAST_INTELLIGENCE_MAX &&
                       g->hp >= S3_BEAST_HP_MIN) ? 1 : 0;
    }
}

static void load_soldiers(S3GameData *d, const S3Ini *ini) {
    int cap = 0;
    for (int i = 0; i < ini->n_sections; ++i) {
        const S3IniSection *s = &ini->sections[i];
        if (strcmp(s->name, "ITEM") != 0) continue;
        if (!s3_ini_val_at(s, "Name", 0)) continue;

        void *np = grow(d->soldiers, &cap, d->n_soldiers + 1, sizeof(S3Soldier));
        if (!np) return;
        d->soldiers = (S3Soldier *)np;
        S3Soldier *x = &d->soldiers[d->n_soldiers++];
        memset(x, 0, sizeof(*x));

        x->no = s3_ini_int(s, "No", 0);
        copy_str(x->name, sizeof x->name, s3_ini_str(s, "Name", ""));
        copy_str(x->name_adv, sizeof x->name_adv, s3_ini_str(s, "NameAdv", ""));
        x->res_id      = s3_ini_int(s, "ResID", 0);
        x->start_hp    = s3_ini_int(s, "StartHP", 0);
        x->add_hp      = s3_ini_int(s, "AddHP", 0);
        x->start_power = s3_ini_int(s, "StartPower", 0);
        x->add_power   = s3_ini_int(s, "AddPower", 0);
        for (int k = 0; k < 10; ++k) {
            char key[16];
            snprintf(key, sizeof key, "HitRate%02d", k);
            x->hit_rate[k] = s3_ini_int(s, key, 0);
        }
    }
}

/* 武将技与军师技字段高度重合 → 同一函数按 section 名加载（2026-09-22 扩展）。
 * 差异字段：BF 用 NoArena（比武禁用），SF 用 Range / EnemyType（范围/作用对象）。 */
static void load_magic_table(S3GameData *d, const S3Ini *ini, const char *section,
                             S3Magic **arr, int *n_arr) {
    int cap = 0;
    for (int i = 0; i < ini->n_sections; ++i) {
        const S3IniSection *s = &ini->sections[i];
        if (strcmp(s->name, section) != 0) continue;
        if (!s3_ini_val_at(s, "Name", 0)) continue;

        void *np = grow(*arr, &cap, *n_arr + 1, sizeof(S3Magic));
        if (!np) return;
        *arr = (S3Magic *)np;
        S3Magic *m = &(*arr)[(*n_arr)++];
        memset(m, 0, sizeof(*m));

        m->no = s3_ini_int(s, "No", 0);
        copy_str(m->name, sizeof m->name, s3_ini_str(s, "Name", ""));
        m->mp           = s3_ini_int(s, "MP", 0);
        m->power        = s3_ini_int(s, "Power", 0);
        m->level        = s3_ini_int(s, "Level", 0);
        m->contribution = s3_ini_int(s, "Contribution", 0);   /* 功勋价（定稿 P6/P7） */
        m->attribute    = s3_ini_int(s, "Attribute", 0);
        /* 可学区间（半开）—— 定稿 P2「不在区间不显示」的数据依据 */
        m->str_down     = s3_ini_int(s, "StrDown", 0);
        m->str_up       = s3_ini_int(s, "StrUp", 0);
        m->int_down     = s3_ini_int(s, "IntDown", 0);
        m->int_up       = s3_ini_int(s, "IntUp", 0);
        m->no_arena     = s3_ini_int(s, "NoArena", 0);
        m->range        = s3_ini_int(s, "Range", 0);
        m->enemy_type   = s3_ini_int(s, "EnemyType", 0);
        copy_str(m->spec, sizeof m->spec, s3_ini_str(s, "Spec", ""));
    }
}

static void load_magics(S3GameData *d, const S3Ini *ini) {
    load_magic_table(d, ini, "BF_MAGIC", &d->magics, &d->n_magics);
}

static void load_sfmagics(S3GameData *d, const S3Ini *ini) {
    load_magic_table(d, ini, "SF_MAGIC", &d->sfmagics, &d->n_sfmagics);
}

/* 定稿 P2/P7：等级 + 武力区间 + 智力区间 三元同时满足才可学。
 * 区间为半开 [down, up)；up <= down 视为"该维不限制"（BF 表用 0/200，SF 表用 999）。*/
int s3_magic_learnable(const S3Magic *m, int strength, int intelligence, int level) {
    if (!m) return 0;
    if (m->level > 0 && level < m->level) return 0;
    if (m->str_up > m->str_down && (strength < m->str_down || strength >= m->str_up)) return 0;
    if (m->int_up > m->int_down && (intelligence < m->int_down || intelligence >= m->int_up)) return 0;
    return 1;
}

static void load_rules(S3GameData *d) {
    static const char *WANT[] = { "GENERALEXP", "SOLDIEREXP", "COST" };
    if (!d->game_ini) return;
    int cap = 0;
    for (size_t w = 0; w < sizeof WANT / sizeof WANT[0]; ++w) {
        const S3IniSection *s = s3_ini_section(d->game_ini, WANT[w]);
        if (!s) continue;
        void *np = grow(d->rules, &cap, d->n_rules + 1, sizeof(S3RuleMap));
        if (!np) return;
        d->rules = (S3RuleMap *)np;
        S3RuleMap *rm = &d->rules[d->n_rules++];
        memset(rm, 0, sizeof(*rm));
        rm->name = (char *)WANT[w];
        rm->items = (S3RuleEntry *)calloc((size_t)(s->n_keys ? s->n_keys : 1), sizeof(S3RuleEntry));
        if (!rm->items) { rm->items = NULL; continue; }
        for (int i = 0; i < s->n_keys; ++i) {
            if (s->keys[i].key[0] == '_') continue;
            rm->items[rm->n].key = s->keys[i].key;      /* 指向 game_ini 的缓冲，随其释放 */
            rm->items[rm->n].value = s3_ini_int(s, s->keys[i].key, 0);
            rm->n++;
        }
    }
}

S3GameData *s3_gamedata_load(const char *setting_dir) {
    S3GameData *d = (S3GameData *)calloc(1, sizeof(S3GameData));
    if (!d) return NULL;
    d->soldier_limit = S3_SOLDIER_LIMIT_DEFAULT;   /* 需求② */
    if (!setting_dir) { copy_str(d->error, sizeof d->error, "setting_dir is NULL"); return d; }

    char path[1024];
    struct { const char *file; int required; } files[] = {
        { "General01.ini", 1 }, { "Thing.ini", 1 }, { "Soldier.ini", 1 },
        { "BFMagic.ini",   1 }, { "Game.ini",  0 }, { "SFMagic.ini", 0 },
    };
    enum { NFILES = sizeof files / sizeof files[0] };
    S3Ini *ini[NFILES];
    for (int i = 0; i < (int)NFILES; ++i) ini[i] = NULL;

    for (int i = 0; i < (int)NFILES; ++i) {
        if (join(path, sizeof path, setting_dir, files[i].file) != 0) continue;
        ini[i] = s3_ini_load(path);
        if (!ini[i] && files[i].required) {
            char msg[256];
            snprintf(msg, sizeof msg, "cannot load %s", files[i].file);
            copy_str(d->error, sizeof d->error, msg);
            for (int k = 0; k < (int)NFILES; ++k) s3_ini_free(ini[k]);
            return d;
        }
    }

    d->game_ini = ini[4];        /* 常驻（规则查询用） */
    if (ini[1]) load_items(d, ini[1]);
    if (ini[0]) load_generals(d, ini[0]);
    if (ini[2]) load_soldiers(d, ini[2]);
    if (ini[3]) load_magics(d, ini[3]);      /* 武将技 125 */
    if (ini[5]) load_sfmagics(d, ini[5]);    /* 军师技 23（2026-09-22 新增） */
    load_rules(d);

    s3_ini_free(ini[0]);
    s3_ini_free(ini[1]);
    s3_ini_free(ini[2]);
    s3_ini_free(ini[3]);
    s3_ini_free(ini[5]);
    /* ini[4] 保留在 d->game_ini */

    /* 用初始装备求值一次攻击力（不授予技能：开局预设只来自数据） */
    for (int i = 0; i < d->n_generals; ++i) {
        S3General *g = &d->generals[i];
        const S3Item *it = s3_gamedata_item_by_name(d, g->weapon);
        g->weapon_bonus = it ? it->strength_bonus : 0;
    }

    d->ok = (d->n_generals > 0 && d->n_items > 0 && d->n_soldiers > 0 && d->n_magics > 0);
    if (!d->ok && !d->error[0]) copy_str(d->error, sizeof d->error, "some tables are empty");
    return d;
}

void s3_gamedata_free(S3GameData *d) {
    if (!d) return;
    for (int i = 0; i < d->n_rules; ++i) free(d->rules[i].items);
    free(d->rules);
    free(d->generals);
    free(d->items);
    free(d->soldiers);
    free(d->magics);
    free(d->sfmagics);
    s3_ini_free(d->game_ini);
    free(d);
}

int         s3_gamedata_ok(const S3GameData *d)    { return d ? d->ok : 0; }
const char *s3_gamedata_error(const S3GameData *d) { return d ? d->error : "null"; }

const S3General *s3_gamedata_general_at(const S3GameData *d, int idx) {
    if (!d || idx < 0 || idx >= d->n_generals) return NULL;
    return &d->generals[idx];
}

const S3General *s3_gamedata_general_by_no(const S3GameData *d, int no) {
    if (!d) return NULL;
    for (int i = 0; i < d->n_generals; ++i)
        if (d->generals[i].no == no) return &d->generals[i];
    return NULL;
}

const S3Item *s3_gamedata_item_by_name(const S3GameData *d, const char *name) {
    if (!d || !name || !*name) return NULL;
    for (int i = 0; i < d->n_items; ++i)
        if (strcmp(d->items[i].name, name) == 0) return &d->items[i];   /* 首次出现优先 */
    return NULL;
}

/* ------------------------------------------------- 武将技 / 军师技 查询（P 区） */
int s3_gamedata_magic_count(const S3GameData *d) { return d ? d->n_magics : 0; }

const S3Magic *s3_gamedata_magic_at(const S3GameData *d, int idx) {
    if (!d || idx < 0 || idx >= d->n_magics) return NULL;
    return &d->magics[idx];
}

const S3Magic *s3_gamedata_magic_by_no(const S3GameData *d, int no) {
    if (!d) return NULL;
    for (int i = 0; i < d->n_magics; ++i)
        if (d->magics[i].no == no) return &d->magics[i];
    return NULL;
}

int s3_gamedata_sfmagic_count(const S3GameData *d) { return d ? d->n_sfmagics : 0; }

const S3Magic *s3_gamedata_sfmagic_at(const S3GameData *d, int idx) {
    if (!d || idx < 0 || idx >= d->n_sfmagics) return NULL;
    return &d->sfmagics[idx];
}

const S3Magic *s3_gamedata_sfmagic_by_no(const S3GameData *d, int no) {
    if (!d) return NULL;
    for (int i = 0; i < d->n_sfmagics; ++i)
        if (d->sfmagics[i].no == no) return &d->sfmagics[i];
    return NULL;
}

const S3Soldier *s3_gamedata_soldier_by_no(const S3GameData *d, int no) {
    if (!d) return NULL;
    for (int i = 0; i < d->n_soldiers; ++i)
        if (d->soldiers[i].no == no) return &d->soldiers[i];
    return NULL;
}

int s3_gamedata_rule_int(const S3GameData *d, const char *section, const char *key, int def) {
    if (!d || !d->game_ini) return def;
    const S3IniSection *s = s3_ini_section(d->game_ini, section);
    if (!s) return def;
    return s3_ini_int(s, key, def);
}

int s3_gamedata_rule_count(const S3GameData *d) { return d ? d->n_rules : 0; }

/* ------------------------------------------------------------ 规则引擎 */
int s3_item_strength_bonus(const S3Item *it) {
    if (!it) return 0;
    return it->type == 2 ? it->increment[0] : 0;
}

int s3_general_current_attack(const S3GameData *d, const S3General *g) {
    if (!g) return 0;
    const S3Item *w = s3_gamedata_item_by_name(d, g->weapon);
    return g->strength + (w ? s3_item_strength_bonus(w) : 0);
}

int s3_general_knows_super_attack(const S3General *g, int id) {
    if (!g || id <= 0) return 0;
    for (int i = 0; i < g->n_super_attack; ++i) if (g->super_attack[i] == id) return 1;
    for (int i = 0; i < g->n_learned; ++i)      if (g->learned[i] == id)      return 1;
    return 0;
}

int s3_general_super_attack_count(const S3General *g) {
    if (!g) return 0;
    int n = 0;
    for (int id = 1; id <= S3_MAX_SA; ++id) if (s3_general_knows_super_attack(g, id)) ++n;
    return n;
}

static S3SuperAttackPicker g_picker = s3_general_pick_super_attack_default;

void s3_general_set_super_attack_picker(S3SuperAttackPicker fn) {
    g_picker = fn ? fn : s3_general_pick_super_attack_default;
}

int s3_general_pick_super_attack_default(const S3General *g) {
    /* 默认策略：在尚未拥有的编号中等概率随机取一个。
     *
     * 依据（实测，见 .workbuddy/probe_sa_rule.py）：原版 79 名自带必杀技的武将中，
     * 编号 1..8 的分布近似均匀（各 7~13 人），且与 WeaponType（仅 1..5）、
     * 武力档位均无相关性 —— 最可能是随机分配，故按随机实现。
     * 随机源可播种（s3_rng_seed），因此回归测试可复现。 */
    int pool[S3_MAX_SA];
    int n = 0;
    for (int id = 1; id <= S3_MAX_SA; ++id)
        if (!s3_general_knows_super_attack(g, id)) pool[n++] = id;
    if (n == 0) return 0;                       /* 8 种全有 —— 需求③下不存在容量限制 */
    return pool[s3_rng_range(0, n - 1)];
}

int s3_general_on_attack_changed(const S3GameData *d, S3General *g,
                                 int *granted, int granted_max) {
    if (!g) return 0;

    /* 先按当前装备刷新加成（装备系统 → 本函数 是唯一入口） */
    const S3Item *w = s3_gamedata_item_by_name(d, g->weapon);
    g->weapon_bonus = w ? s3_item_strength_bonus(w) : 0;

    /* 条件①：当前攻击力 >= 80；条件②：非野兽 */
    if (s3_general_current_attack(d, g) < S3_SUPER_ATTACK_ATTACK_THRESHOLD) return 0;
    if (g->is_beast) return 0;

    /* 条件③：尚未拥有 —— 已持有任一必杀技者不再重复授予。
     * 这样做的两个理由：
     *   1) 与规则 yaml 的 "not_already_known: true # 避免重复授予" 字面一致；
     *   2) 防止"反复装备/卸下武器"把 8 招刷满（学习是历史事件，一个人只学一次）。
     * 原版数据也支持：79 名持有者中 75 人恰好 1 招，仅 4 人 2 招。 */
    if (s3_general_super_attack_count(g) > 0) return 0;
    if (g->n_learned >= S3_MAX_SA) return 0;

    int id = g_picker ? g_picker(g) : 0;
    if (id <= 0) return 0;

    g->learned[g->n_learned++] = id;
    if (granted && granted_max > 0) granted[0] = id;
    return 1;   /* 每次事件最多授予 1 招；本函数没有撤销分支（monotonic） */
}

int s3_general_set_weapon(S3GameData *d, S3General *g, const char *weapon_name) {
    if (!d || !g) return -1;
    if (weapon_name && *weapon_name) {
        const S3Item *it = s3_gamedata_item_by_name(d, weapon_name);
        if (!it || it->type != 2) return -1;         /* 不是合法武器名 */
        copy_str(g->weapon, sizeof g->weapon, weapon_name);
    } else {
        g->weapon[0] = '\0';                          /* 卸下 */
    }
    s3_general_on_attack_changed(d, g, NULL, 0);      /* 触发事件 */
    return 0;
}
