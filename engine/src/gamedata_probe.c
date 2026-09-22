/*
 * gamedata_probe.c —— 数据层 / 规则引擎验证器
 *
 * 用法：
 *   sango3data <setting_dir> <out_dir>
 * 例：
 *   sango3data .workbuddy/data/Setting build/pc/out
 *
 * 产出（UTF-8，供 tools/verify_data_c.py 与 Python 基准逐字段比对）：
 *   <out_dir>/gamedata_dump.txt     全部记录（G 武将 / I 物品 / S 兵种 / M 武将技 / R 规则）
 *   <out_dir>/gamedata_summary.txt  派生统计 + 规则自测结论
 *
 * 注意：控制台只输出 ASCII（Windows 控制台是 GBK，中文会乱码），
 *      中文内容一律写文件。
 */
#include "gamedata.h"
#include "rng.h"
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_dump(const S3GameData *d, const char *out_dir) {
    char path[1024];
    snprintf(path, sizeof path, "%s/gamedata_dump.txt", out_dir);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    fprintf(f, "#Sango3 gamedata dump v1\n");
    /* 列顺序固定，Python 侧按下标取值；用 \t 分隔（内容不含制表符） */
    fprintf(f, "#G\tno\tname\tdisplay\tstrength\tintelligence\thp\tmp\t"
               "weapon\tweapon_bonus\tcurrent_attack\tsuper_attack\tsoldier_type\t"
               "is_beast\trank\tsex\tweapon_type\tportrait\n");
    for (int i = 0; i < d->n_generals; ++i) {
        const S3General *g = &d->generals[i];
        fprintf(f, "G\t%d\t%s\t%s\t%d\t%d\t%d\t%d\t%s\t%d\t%d\t",
                g->no, g->name, g->display, g->strength, g->intelligence, g->hp, g->mp,
                g->weapon, g->weapon_bonus, s3_general_current_attack(d, g));
        for (int k = 0; k < g->n_super_attack; ++k) fprintf(f, "%s%d", k ? "," : "", g->super_attack[k]);
        fprintf(f, "\t");
        for (int k = 0; k < g->n_soldier_type; ++k) fprintf(f, "%s%d", k ? "," : "", g->soldier_type[k]);
        fprintf(f, "\t%d\t%d\t%d\t%d\t%d\n", g->is_beast, g->rank, g->sex, g->weapon_type, g->portrait);
    }

    fprintf(f, "#I\tname\tdisplay\ttype\tslot\tlevel\tinc1\tinc2\tinc3\tstrength_bonus\tcount"
               "\tfind_rate\tattraction\n");
    for (int i = 0; i < d->n_items; ++i) {
        const S3Item *it = &d->items[i];
        fprintf(f, "I\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%d\t%d\n",
                it->name, it->display, it->type, it->slot, it->level,
                it->increment[0], it->increment[1], it->increment[2],
                it->strength_bonus, it->count, it->find_rate, it->attraction);
    }

    fprintf(f, "#S\tno\tname\tname_adv\tres_id\tstart_hp\tadd_hp\tstart_power\tadd_power\thit_rate\n");
    for (int i = 0; i < d->n_soldiers; ++i) {
        const S3Soldier *s = &d->soldiers[i];
        fprintf(f, "S\t%d\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t",
                s->no, s->name, s->name_adv, s->res_id,
                s->start_hp, s->add_hp, s->start_power, s->add_power);
        for (int k = 0; k < 10; ++k) fprintf(f, "%s%d", k ? "," : "", s->hit_rate[k]);
        fprintf(f, "\n");
    }

    fprintf(f, "#M\tno\tname\tmp\tpower\tlevel\tcontribution\tattribute"
               "\tstr_down\tstr_up\tint_down\tint_up\tno_arena\n");
    for (int i = 0; i < d->n_magics; ++i) {
        const S3Magic *m = &d->magics[i];
        fprintf(f, "M\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                m->no, m->name, m->mp, m->power, m->level, m->contribution, m->attribute,
                m->str_down, m->str_up, m->int_down, m->int_up, m->no_arena);
    }

    /* 军师技（SFMagic.ini，2026-09-22 新增；字段同武将技 + Range/EnemyType） */
    fprintf(f, "#F\tno\tname\tmp\tlevel\tcontribution\tattribute"
               "\tstr_down\tstr_up\tint_down\tint_up\trange\tenemy_type\n");
    for (int i = 0; i < d->n_sfmagics; ++i) {
        const S3Magic *m = &d->sfmagics[i];
        fprintf(f, "F\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                m->no, m->name, m->mp, m->level, m->contribution, m->attribute,
                m->str_down, m->str_up, m->int_down, m->int_up, m->range, m->enemy_type);
    }

    fprintf(f, "#R\tsection\tkey\tvalue\n");
    for (int i = 0; i < d->n_rules; ++i)
        for (int k = 0; k < d->rules[i].n; ++k)
            fprintf(f, "R\t%s\t%s\t%d\n", d->rules[i].name, d->rules[i].items[k].key,
                    d->rules[i].items[k].value);

    fclose(f);
    return 0;
}

/* 注意：本函数会推进武将的运行时状态（自动习得记录），因此 d 不能是 const —— 
 * 这是一次性验证进程，不影响磁盘数据。 */
static int write_summary(S3GameData *d, const char *out_dir) {
    char path[1024];
    snprintf(path, sizeof path, "%s/gamedata_summary.txt", out_dir);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    int max_bonus = 0;
    for (int i = 0; i < d->n_items; ++i)
        if (d->items[i].strength_bonus > max_bonus) max_bonus = d->items[i].strength_bonus;

    int with_sa = 0, beasts = 0, A = 0, B = 0, C = 0;
    for (int i = 0; i < d->n_generals; ++i) {
        const S3General *g = &d->generals[i];
        if (g->n_super_attack > 0) ++with_sa;
        if (g->is_beast) ++beasts;
        if (g->n_super_attack > 0 || g->is_beast) continue;
        int cur = s3_general_current_attack(d, g);
        if (g->strength >= 80)                     ++A;
        else if (cur >= 80)                        ++B;
        else if (g->strength + max_bonus >= 80)    ++C;
    }

    fprintf(f, "#Sango3 gamedata summary v1\n");
    fprintf(f, "counts.generals=%d\n",  d->n_generals);
    fprintf(f, "counts.items=%d\n",     d->n_items);
    fprintf(f, "counts.soldiers=%d\n",  d->n_soldiers);
    fprintf(f, "counts.magics=%d\n",    d->n_magics);
    fprintf(f, "counts.sfmagics=%d\n",  d->n_sfmagics);
    /* P 区自检（2026-09-22）：区间过滤的非空性 —— 有区间约束的技数、比武禁用数、搜不到的物品种数 */
    {
        int bf_band = 0, bf_noarena = 0, sf_band = 0;
        for (int i = 0; i < d->n_magics; ++i) {
            const S3Magic *m = &d->magics[i];
            if (m->str_up > m->str_down || m->int_up > m->int_down) ++bf_band;
            if (m->no_arena) ++bf_noarena;
        }
        for (int i = 0; i < d->n_sfmagics; ++i) {
            const S3Magic *m = &d->sfmagics[i];
            if (m->str_up > m->str_down || m->int_up > m->int_down) ++sf_band;
        }
        int unfindable = 0, gift_ok = 0;
        for (int i = 0; i < d->n_items; ++i) {
            if (d->items[i].find_rate == 0) ++unfindable;
            if (d->items[i].type == 6 && d->items[i].increment[0] > 0) ++gift_ok;
        }
        fprintf(f, "p1.magic_with_band=%d\n", bf_band);
        fprintf(f, "p1.magic_no_arena=%d\n", bf_noarena);
        fprintf(f, "p1.sfmagic_with_band=%d\n", sf_band);
        fprintf(f, "p1.items_unfindable=%d\n", unfindable);
        fprintf(f, "p1.gift_items_with_loyalty=%d\n", gift_ok);
    }
    fprintf(f, "counts.with_super_attack=%d\n", with_sa);
    fprintf(f, "counts.beasts=%d\n",    beasts);
    fprintf(f, "class_A_base_ge80=%d\n", A);
    fprintf(f, "class_B_initial_weapon=%d\n", B);
    fprintf(f, "class_C_potential=%d\n", C);
    fprintf(f, "max_weapon_strength_bonus=%d\n", max_bonus);
    fprintf(f, "soldier_limit=%d\n", d->soldier_limit);
    fprintf(f, "beast_rule=intelligence<=%d and hp>=%d\n",
            S3_BEAST_INTELLIGENCE_MAX, S3_BEAST_HP_MIN);
    fprintf(f, "attack_threshold=%d\n", S3_SUPER_ATTACK_ATTACK_THRESHOLD);

    /* ---------------------------------------------- 规则自测（写进同一文件） */
    s3_rng_seed(20260913u);

    /* 测试 1：对全部武将触发 on_attack_changed，应恰好授予 A 类人数 */
    int learned_total = 0, learned_beast = 0, learned_below = 0;
    for (int i = 0; i < d->n_generals; ++i) {
        S3General *g = &d->generals[i];
        int n = s3_general_on_attack_changed(d, g, NULL, 0);
        if (n > 0) {
            ++learned_total;
            if (g->is_beast) ++learned_beast;
            if (s3_general_current_attack(d, g) < 80) ++learned_below;
        }
    }
    fprintf(f, "test.initial_grants=%d\n", learned_total);
    fprintf(f, "test.granted_to_beast=%d\n", learned_beast);
    fprintf(f, "test.granted_below_threshold=%d\n", learned_below);

    /* 测试 2：monotonic —— 卸下武器后已授予的技能必须保留 */
    int revoked = 0, checked = 0;
    for (int i = 0; i < d->n_generals; ++i) {
        S3General *g = &d->generals[i];
        if (g->n_learned == 0) continue;
        ++checked;
        int before = s3_general_super_attack_count(g);
        s3_general_set_weapon(d, g, NULL);                  /* 卸下 */
        if (s3_general_super_attack_count(g) != before) ++revoked;
    }
    fprintf(f, "test.monotonic_checked=%d\n", checked);
    fprintf(f, "test.monotonic_revoked=%d\n", revoked);

    /* 测试 3：C 类换最强武器后应全部达标（证明"必须运行时判定"） */
    const S3Item *best = NULL;
    for (int i = 0; i < d->n_items; ++i)
        if (d->items[i].strength_bonus == max_bonus && max_bonus > 0) { best = &d->items[i]; break; }
    int c_learned = 0, c_total = 0;
    if (best) {
        for (int i = 0; i < d->n_generals; ++i) {
            S3General *g = &d->generals[i];
            if (g->is_beast || g->n_super_attack > 0) continue;
            if (g->strength >= 80) continue;
            if (g->strength + max_bonus < 80) continue;
            ++c_total;
            /* 复位运行时状态，模拟"新获得此武将" */
            g->n_learned = 0;
            g->weapon[0] = '\0';
            s3_general_set_weapon(d, g, best->name);
            if (g->n_learned > 0) ++c_learned;
        }
    }
    fprintf(f, "test.class_c_best_weapon_total=%d\n", c_total);
    fprintf(f, "test.class_c_best_weapon_learned=%d\n", c_learned);
    fprintf(f, "test.best_weapon_name=%s\n", best ? best->name : "(none)");

    /* 测试 4：需求③ —— 携带数量无 3 招上限 */
    for (int i = 0; i < d->n_generals; ++i) {
        S3General *g = &d->generals[i];
        if (g->is_beast || g->n_super_attack > 0) continue;
        g->n_learned = 0;
        for (int k = 1; k <= S3_MAX_SA; ++k) g->learned[g->n_learned++] = k;
        fprintf(f, "test.carry_capacity=%d\n", s3_general_super_attack_count(g));
        break;
    }

    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: sango3data <setting_dir> <out_dir>\n");
        return 2;
    }
    /* 编码表位于 engine/assets/encoding —— 允许用第二个可选参数覆盖，默认相对路径 */
    const char *enc = (argc >= 4) ? argv[3] : "engine/assets/encoding";
    if (s3_text_init(enc) != 0) {
        printf("ERROR: cannot load encoding tables from '%s'\n", enc);
        return 3;
    }
    s3_text_set_language(S3_LANG_HANS);

    S3GameData *d = s3_gamedata_load(argv[1]);
    if (!d) { printf("ERROR: out of memory\n"); return 4; }
    if (!s3_gamedata_ok(d)) {
        printf("ERROR: %s\n", s3_gamedata_error(d));
        s3_gamedata_free(d);
        s3_text_shutdown();
        return 5;
    }

    int rc = 0;
    if (write_dump(d, argv[2]) != 0)    { printf("ERROR: cannot write dump to %s\n", argv[2]); rc = 6; }
    if (write_summary(d, argv[2]) != 0) { printf("ERROR: cannot write summary to %s\n", argv[2]); rc = 7; }

    if (rc == 0) {
    printf("OK generals=%d items=%d soldiers=%d magics=%d sfmagics=%d\n",
           d->n_generals, d->n_items, d->n_soldiers, d->n_magics, d->n_sfmagics);
        printf("   encoding=%s  language=HANS  soldier_limit=%d\n", enc, d->soldier_limit);
    }

    s3_gamedata_free(d);
    s3_text_shutdown();
    return rc;
}
