/*
 * itemstore.c —— 最小物品库实现（见 itemstore.h）
 */
#include "itemstore.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    char city[S3_STORE_NAME_CAP];
    char item[S3_STORE_NAME_CAP];
    int  qty;
} S3StoreSlot;

struct S3ItemStore {
    S3StoreSlot s[S3_STORE_MAX_SLOTS];
    int         n;          /* 已用槽位（含 qty == 0 的历史槽，便于同城同名复用） */
};

static const char *clip(const char *s) { return (s && *s) ? s : ""; }

S3ItemStore *s3_store_new(void) {
    return (S3ItemStore *)calloc(1, sizeof(S3ItemStore));
}

void s3_store_free(S3ItemStore *s) { free(s); }

void s3_store_clear(S3ItemStore *s) {
    if (s) { memset(s->s, 0, sizeof s->s); s->n = 0; }
}

/* 查找 (城, 物品) 槽位下标；not_found 返回 -1 */
static int find_slot(const S3ItemStore *s, const char *city, const char *item) {
    if (!s || !city || !item) return -1;
    for (int i = 0; i < s->n; ++i)
        if (!strcmp(s->s[i].city, city) && !strcmp(s->s[i].item, item)) return i;
    return -1;
}

int s3_store_add(S3ItemStore *s, const char *city, const char *item, int n) {
    if (!s || !city || !*city || !item || !*item || n <= 0) return 0;
    int i = find_slot(s, city, item);
    if (i >= 0) { s->s[i].qty += n; return 0; }
    if (s->n >= S3_STORE_MAX_SLOTS) return -1;
    i = s->n++;
    snprintf(s->s[i].city, sizeof s->s[i].city, "%.63s", city);
    snprintf(s->s[i].item, sizeof s->s[i].item, "%.63s", item);
    s->s[i].qty = n;
    return 0;
}

int s3_store_take(S3ItemStore *s, const char *city, const char *item, int n) {
    if (!s || n <= 0) return 0;
    int i = find_slot(s, city, item);
    if (i < 0 || s->s[i].qty < n) return 0;      /* 不足 → 不改动 */
    s->s[i].qty -= n;
    return 1;
}

int s3_store_count(const S3ItemStore *s, const char *city, const char *item) {
    int i = find_slot(s, city, item);
    return (i >= 0) ? s->s[i].qty : 0;
}

int s3_store_list(const S3ItemStore *s, const char *city, int *out, int out_max) {
    if (!s || !city || !out || out_max <= 0) return 0;
    int k = 0;
    for (int i = 0; i < s->n && k < out_max; ++i) {
        if (s->s[i].qty <= 0) continue;
        if (strcmp(s->s[i].city, city)) continue;
        out[k++] = i;
    }
    return k;
}

const char *s3_store_item_at(const S3ItemStore *s, int idx) {
    return (s && idx >= 0 && idx < s->n) ? s->s[idx].item : NULL;
}

int s3_store_qty_at(const S3ItemStore *s, int idx) {
    return (s && idx >= 0 && idx < s->n) ? s->s[idx].qty : 0;
}

const char *s3_store_city_at(const S3ItemStore *s, int idx) {
    return (s && idx >= 0 && idx < s->n) ? s->s[idx].city : NULL;
}

int s3_store_used(const S3ItemStore *s) {
    if (!s) return 0;
    int k = 0;
    for (int i = 0; i < s->n; ++i) if (s->s[i].qty > 0) ++k;
    return k;
}
