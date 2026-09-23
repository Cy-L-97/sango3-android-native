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
    S3_OUI_PICK  = 2,     /* 武将名单（先选人，再进整备页；用户 2026-09-23 要求） */
    S3_OUI_ARRAY = 3      /* 整备全屏界面 */
} S3OuiMode;

S3OfficerUI *s3_oui_new(S3OuiDrawText draw_text, void *text_ud,
                        S3OuiReadAsset read_asset, void *asset_ud);
void         s3_oui_free(S3OfficerUI *u);
void         s3_oui_set_tables(S3OfficerUI *u, const S3ArrayTables *t);
/* 日志通道（可选）：传进来后，模块内的诊断信息（如肖像加载失败原因）会走这里。
 * app 侧一般接到 ALOG，便于在 logcat 里定位"界面里某块没出来"的原因（2026-09-23）。 */
typedef void (*S3OuiLog)(void *ud, const char *msg);
void         s3_oui_set_log(S3OfficerUI *u, S3OuiLog fn, void *ud);

/* ---- 武将信息块浮层（情報） ---- */
void s3_oui_show_card(S3OfficerUI *u, const S3Officer *o, const char *lord,
                      int rank_soldiers);

/* ---- 整备：① 武将名单（先选人）→ ② 整备全屏界面 ---- */
/* 打开武将名单（列出**我方全部非在野武将**）。点某行 → 自动切进该将的整备页。 */
void s3_oui_open_pick(S3OfficerUI *u, S3Roster *roster);
/* 打开某武将的整备页（名单里选中后调用；也供外部直达） */
void s3_oui_open_array(S3OfficerUI *u, S3Roster *roster, int off_idx);
/* 设置「君主」显示名（信息块第 2 行）。切换武将后调用方需重设。 */
void s3_oui_set_lord(S3OfficerUI *u, const char *lord_name);
void s3_oui_close(S3OfficerUI *u);
int  s3_oui_active(const S3OfficerUI *u);
int  s3_oui_mode(const S3OfficerUI *u);
int  s3_oui_off(const S3OfficerUI *u);

void s3_oui_on_move(S3OfficerUI *u, int32_t x, int32_t y);
/* 返回：-1 未处理 · -2 已关闭 · -3 已消费（切页签/切筛选/翻页/选中/取消确认）·
 *       0 当前武将变了（进入整备页 或 ←→ 换将；调用方刷新「君主」行）·
 *       1 = **学成了**（结果见 s3_oui_last_learn_*） */
int  s3_oui_on_click(S3OfficerUI *u, int32_t x, int32_t y);
/* 长按/返回：整备页 → 回武将名单（返回 1，界面仍激活）；名单/信息块 → 关闭（返回 0）。
 * 对应原版的层级退出（用户 2026-09-23 之前的习惯是"长按退一层"）。 */
int  s3_oui_on_rclick(S3OfficerUI *u);

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
