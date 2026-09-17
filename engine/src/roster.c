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
};

S3Roster *s3_roster_new(void) {
    return (S3Roster *)calloc(1, sizeof(S3Roster));
}

void s3_roster_free(S3Roster *r) { free(r); }

void s3_roster_clear(S3Roster *r) {
    if (r) { memset(r->o, 0, sizeof r->o); r->n = 0; }
}

int s3_roster_add(S3Roster *r, const char *name, const char *city,
                  int str, int intel, int wild) {
    if (!r || !name || !*name || r->n >= S3_ROSTER_MAX) return -1;
    S3Officer *o = &r->o[r->n];
    memset(o, 0, sizeof *o);
    snprintf(o->name, sizeof o->name, "%.31s", name);
    snprintf(o->city, sizeof o->city, "%.31s", city ? city : "");
    o->str   = str;
    o->intel = intel;
    o->level = 1;                       /* 原版无等级数据，运行时成长 */
    o->wild  = wild ? 1 : 0;
    return r->n++;
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

int s3_officer_troop_limit(const S3Officer *o) {
    if (!o) return 0;
    int lv = o->level;
    if (lv < 1) lv = 1;
    if (lv > S3_LEVEL_MAX) lv = S3_LEVEL_MAX;
    return lv * S3_TROOPS_PER_LEVEL;
}
