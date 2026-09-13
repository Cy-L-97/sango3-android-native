/*
 * pak.c —— PAK 资源包读取实现（详见 pak.h）
 */
#include "pak.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENTRY_SIZE 64
#define NAME_OFF   0x18
#define NAME_LEN   40

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static size_t bounded_len(const char *s, size_t max) {
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

int pak_contains_ci(const char *hay, const char *needle) {
    if (!needle || !*needle) return 1;
    size_t hn = strlen(hay), nn = strlen(needle);
    if (nn > hn) return 0;
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t j = 0;
        for (; j < nn; j++) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (j == nn) return 1;
    }
    return 0;
}

int pak_open(const char *path, PakArchive *ar, const char **err) {
    const char *e = NULL;
    memset(ar, 0, sizeof(*ar));

    FILE *f = fopen(path, "rb");
    if (!f) { e = "cannot open file"; goto fail; }

    unsigned char head[ENTRY_SIZE];
    if (fread(head, 1, ENTRY_SIZE, f) != ENTRY_SIZE) { e = "file too small"; goto fail; }
    if (memcmp(head, "PAKS", 4) != 0) { e = "bad magic (expected PAKS)"; goto fail; }

    ar->count_hint = rd_u32(head + 0x04);
    ar->index_off  = rd_u32(head + 0x08);
    ar->path = dup_str(path);

    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    if ((long)ar->index_off > fsz) { e = "index offset out of range"; goto fail; }

    size_t n = (size_t)(fsz - (long)ar->index_off) / ENTRY_SIZE;
    unsigned char *blob = (unsigned char *)malloc(n * ENTRY_SIZE);
    if (!blob) { e = "oom (index)"; goto fail; }
    fseek(f, (long)ar->index_off, SEEK_SET);
    size_t got = fread(blob, ENTRY_SIZE, n, f);
    fclose(f);
    f = NULL;

    ar->tab = (PakEntry *)calloc(got ? got : 1, sizeof(PakEntry));
    if (!ar->tab) { free(blob); e = "oom (entries)"; goto fail; }

    uint32_t k = 0;
    for (size_t i = 0; i < got; i++) {
        const unsigned char *rec = blob + i * ENTRY_SIZE;
        const char *nm = (const char *)(rec + NAME_OFF);
        size_t L = bounded_len(nm, NAME_LEN);
        if (L == 0) continue;
        ar->tab[k].size = rd_u32(rec + 0);
        ar->tab[k].off  = rd_u32(rec + 4);
        memcpy(ar->tab[k].name, nm, L);
        ar->tab[k].name[L] = '\0';
        k++;
    }
    ar->entries = k;
    free(blob);
    return 1;

fail:
    if (f) fclose(f);
    if (err) *err = e;
    pak_close(ar);
    return 0;
}

void pak_close(PakArchive *ar) {
    if (!ar) return;
    free(ar->tab);  ar->tab = NULL;
    free(ar->path); ar->path = NULL;
    ar->entries = 0;
}

uint8_t *pak_read(const PakArchive *ar, uint32_t idx, uint32_t *out_len) {
    if (!ar || idx >= ar->entries) return NULL;
    FILE *f = fopen(ar->path, "rb");
    if (!f) return NULL;
    const PakEntry *en = &ar->tab[idx];
    uint8_t *buf = (uint8_t *)malloc(en->size ? en->size : 1);
    if (!buf) { fclose(f); return NULL; }
    fseek(f, (long)en->off, SEEK_SET);
    size_t got = fread(buf, 1, en->size, f);
    fclose(f);
    if (got != en->size) { free(buf); return NULL; }
    if (out_len) *out_len = en->size;
    return buf;
}

int32_t pak_find(const PakArchive *ar, const char *substr) {
    if (!ar) return -1;
    for (uint32_t i = 0; i < ar->entries; i++)
        if (pak_contains_ci(ar->tab[i].name, substr)) return (int32_t)i;
    return -1;
}
