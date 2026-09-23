/*
 * officer_ui.h —— 整备界面（原版 `ARRAY` root）+ 武将信息块（P1 / 定稿 U-1、U-2、P6、P7）
 *
 * 原版布局（2026-09-23 从 `ui.json`/`Menu.ini` 逆向，id 9000~9530；**640×480 坐标**）：
 *   9000 肖像[6,6,100,120] · 9100 資訊欄[119,6,515,120] · 9101/9102 ←/→（cmd 1/2）
 *   9200 物品欄[5,169,110,306] 内含 武器/馬/書 三槽（cmd 11/12/13）
 *   9300 選項[123,134,121,161] 五页签 陣形/兵種/必殺技/武將技/軍師技（cmd 21~25）
 *   9311~9319 子選單[251,139,103,H]（cmd 26）· 9400 陣形（8 小队）[401,139,233,335]（cmd 31~38）
 *   9500 學技大列表（自右滑入）· 9520 訊息欄「所需功勳：%d」· 9531/9532 是/否（cmd 52/53）
 * 详细表见 `docs/城池信息面板与行政菜单.md` 第八节。
 *
 * 数据依据：`Text.ini 9045`（信息块排版）· 9040~9044（学技文案）·
 *   `GenTitle.ini`（69 官位，含带兵 Soldiers）· `Rank.ini`（8 阵形）· `Soldier.ini`（9 兵种）。
 *
 * 本模块只负责"长什么样 + 命中 + 结果回传"，不碰游戏数据（roster 可变仅为学技写回）。
 */
#ifndef SANGO3_OFFICER_UI_H
#define SANGO3_OFFICER_UI_H

#include <stdint.h>
#include "render.h"
#include "shp.h"
#include "roster.h"
#include "gamedata.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*S3OuiDrawText)(void *ud, Sango3Canvas *cv, const char *utf8,
                              int32_t x, int32_t y, int32_t w, int32_t h,
                              uint32_t rgb, int font, uint32_t style);
typedef uint8_t *(*S3OuiReadAsset)(void *ud, const char *path, uint32_t *out_len);

/* 静态表（app 从 PAK 解析后传入，生命周期由调用方保证） */
typedef struct {
    const char *form_names[8];      int n_form;       /* 阵形名（Rank.ini） */
    const char *soldier_names[20];  const char *soldier_adv[20]; int n_soldier;  /* 兵种名（Soldier.ini） */
    const char *sa_names[8];                          /* 必杀技名（Text.ini 9031~9038） */
    const S3Magic *bf; int n_bf;                      /* 武将技表（全量，模块内过滤） */
    const S3Magic *sf; int n_sf;                      /* 军师技表 */
} S3ArrayTables;

typedef struct S3OfficerUI S3OfficerUI;

typedef enum {
    S3_OUI_NONE  = 0,
    S3_OUI_CARD  = 1,     /* 武将信息块浮层（情報 用；点任意处关闭） */
    S3_OUI_ARRAY = 2      /* 整备全屏界面 */
} S3OuiMode;

S3OfficerUI *s3_oui_new(S3OuiDrawText draw_text, void *text_ud,
                        S3OuiReadAsset read_asset, void *asset_ud);
void         s3_oui_free(S3OfficerUI *u);
void         s3_oui_set_tables(S3OfficerUI *u, const S3ArrayTables *t);

/* ---- 武将信息块浮层（情報） ---- */
void s3_oui_show_card(S3OfficerUI *u, const S3Officer *o, const char *lord,
                      int rank_soldiers);

/* ---- 整备全屏界面 ---- */
void s3_oui_open_array(S3OfficerUI *u, S3Roster *roster, int off_idx);
/* 设置「君主」显示名（信息块第 2 行）。切换武将后调用方需重设。 */
void s3_oui_set_lord(S3OfficerUI *u, const char *lord_name);
/* 设置官位名（未任命传 NULL/"" → 显示「—」）。
 * 原版官位是运行时任命的，首版未做任免 → 恒为「—」，见 docs 第八节。 */
void s3_oui_set_rank(S3OfficerUI *u, const char *rank_name, int rank_soldiers);
void s3_oui_close(S3OfficerUI *u);
int  s3_oui_active(const S3OfficerUI *u);
int  s3_oui_mode(const S3OfficerUI *u);
int  s3_oui_off(const S3OfficerUI *u);

void s3_oui_on_move(S3OfficerUI *u, int32_t x, int32_t y);
/* 返回：-1 未处理 · -2 已关闭 · -3 已消费（切页签/选中/取消确认）·
 *       0 切换了武将（调用方刷新提示）· >=0 = **学成了第 i 项技**（调用方记账/提示） */
int  s3_oui_on_click(S3OfficerUI *u, int32_t x, int32_t y);

/* 最近一次学技的结果（供调用方拼提示） */
const char *s3_oui_last_learn_name(const S3OfficerUI *u);
int  s3_oui_last_learn_is_sf(const S3OfficerUI *u);
int  s3_oui_last_learn_cost(const S3OfficerUI *u);
int  s3_oui_last_learn_failed(const S3OfficerUI *u);   /* 1 = 功勋不足 */

void s3_oui_render(S3OfficerUI *u, Sango3Canvas *cv);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_OFFICER_UI_H */
