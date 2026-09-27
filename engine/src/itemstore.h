/*
 * itemstore.h —— 最小物品库（城池级）
 *
 * 用途（2026-09-27 新增，本批"O-3 赏赐"落地的前置）：
 *   原版物品属于**城池**：搜索搜到的物品入库、赏赐时从库里取。
 *   我们此前完全没有物品库（`S3CityDetail` 无物品字段，「物品」指令 B5 仍是待办），
 *   导致"赏赐（忠诚 +4~+12）"无从谈起。
 *
 * 设计：**极简** —— 一张 (城池, 物品, 数量) 的扁平表，不引入槽位/装备语义。
 *   装备槽（武器/马/书）仍走 `S3Officer` 的既有字段，与本库无关。
 *
 * 数据来源：物品名 = `Thing.ini` 的 `Name`（app 侧按 `Type` 过滤后才入库/展示）。
 * 本模块只存名字与数量，不认识"赏赐类/宝物"等语义（那属 gamedata 的 Type）。
 */
#ifndef SANGO3_ITEMSTORE_H
#define SANGO3_ITEMSTORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define S3_STORE_MAX_SLOTS 512          /* 扁平表容量（城 × 物品） */
#define S3_STORE_NAME_CAP  64           /* 与 gamedata.h 的 S3_NAME_CAP 同值 */

typedef struct S3ItemStore S3ItemStore;

S3ItemStore *s3_store_new(void);
void         s3_store_free(S3ItemStore *s);
void         s3_store_clear(S3ItemStore *s);

/* 入库：同城同名**累加**。n <= 0 不做事。返回 0 成功、-1 表满。 */
int  s3_store_add(S3ItemStore *s, const char *city, const char *item, int n);
/* 出库：数量不足返回 0（**不做任何改动**）；成功返回 1 并扣减（扣到 0 保留空位）。 */
int  s3_store_take(S3ItemStore *s, const char *city, const char *item, int n);
/* 查询：某城某物品的现有数量（无则 0）。 */
int  s3_store_count(const S3ItemStore *s, const char *city, const char *item);

/* 列出某城**数量 > 0** 的全部物品 → 把**表内下标**写进 out，返回条数。 */
int  s3_store_list(const S3ItemStore *s, const char *city, int *out, int out_max);
/* 按表内下标取物品名 / 数量 / 所在城（下标非法返回 NULL/0）。 */
const char *s3_store_item_at(const S3ItemStore *s, int idx);
int         s3_store_qty_at(const S3ItemStore *s, int idx);
const char *s3_store_city_at(const S3ItemStore *s, int idx);

/* 全库条目数（数量 > 0），供日志/自检。 */
int  s3_store_used(const S3ItemStore *s);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_ITEMSTORE_H */
