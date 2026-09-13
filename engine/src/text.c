/*
 * text.c —— Big5 → UTF-8 解码 + 繁→简显示语言层
 *
 * 表文件格式见 tools/gen_encoding_tables.py 的文档注释（两侧必须严格一致）。
 */
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- 表结构 */
#define B5_LEAD_LO   0xA1
#define B5_LEAD_HI   0xF9
#define B5_TRAILS    157   /* 0x40..0x7E (63) + 0xA1..0xFE (94) */
#define B5_COUNT     ((B5_LEAD_HI - B5_LEAD_LO + 1) * B5_TRAILS)   /* 13973 */

#define REPLACEMENT  0xFFFDu

static uint16_t *g_big5 = NULL;      /* B5_COUNT 项，0 = 无映射 */
static uint32_t *g_h2s_in = NULL;    /* 繁（升序） */
static uint32_t *g_h2s_out = NULL;   /* 简 */
static uint32_t  g_h2s_n = 0;
static S3Lang    g_lang = S3_LANG_HANS;

int s3_text_ready(void) { return g_big5 != NULL; }

/* ---------------------------------------------------------------- 文件读取 */
static uint8_t *read_file(const char *path, size_t *out_n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return NULL; }
    *out_n = got;
    return buf;
}

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* 拼路径：dir + '/' + name。返回 0 成功。 */
static int join_path(char *dst, size_t cap, const char *dir, const char *name) {
    size_t a = strlen(dir), b = strlen(name);
    if (a + 1 + b + 1 > cap) return -1;
    memcpy(dst, dir, a);
    size_t i = a;
    if (i && dst[i - 1] != '/' && dst[i - 1] != '\\') dst[i++] = '/';
    memcpy(dst + i, name, b + 1);
    return 0;
}

/* ---------------------------------------------------------------- 初始化 */
int s3_text_init(const char *encoding_dir) {
    if (!encoding_dir) return -1;
    if (g_big5) return 0;

    char path[1024];
    size_t n = 0;

    /* --- Big5 表 --- */
    if (join_path(path, sizeof path, encoding_dir, "big5_cp950.bin") != 0) return -1;
    uint8_t *raw = read_file(path, &n);
    if (!raw) return -1;
    if (n < 8 || memcmp(raw, "B5TB", 4) != 0) { free(raw); return -1; }
    uint32_t cnt = rd_u32(raw + 4);
    if (cnt != B5_COUNT || n < 8 + (size_t)cnt * 2) { free(raw); return -1; }
    uint16_t *tbl = (uint16_t *)malloc(sizeof(uint16_t) * cnt);
    if (!tbl) { free(raw); return -1; }
    for (uint32_t i = 0; i < cnt; ++i) tbl[i] = rd_u16(raw + 8 + i * 2);
    free(raw);
    g_big5 = tbl;

    /* --- 繁→简 表（缺失不致命：退化为"不转换"） --- */
    if (join_path(path, sizeof path, encoding_dir, "hant2hans.bin") == 0) {
        raw = read_file(path, &n);
        if (raw && n >= 8 && memcmp(raw, "H2S1", 4) == 0) {
            uint32_t c2 = rd_u32(raw + 4);
            if (n >= 8 + (size_t)c2 * 8) {
                g_h2s_in  = (uint32_t *)malloc(sizeof(uint32_t) * (c2 ? c2 : 1));
                g_h2s_out = (uint32_t *)malloc(sizeof(uint32_t) * (c2 ? c2 : 1));
                if (g_h2s_in && g_h2s_out) {
                    for (uint32_t i = 0; i < c2; ++i) {
                        g_h2s_in[i]  = rd_u32(raw + 8 + i * 8);
                        g_h2s_out[i] = rd_u32(raw + 8 + i * 8 + 4);
                    }
                    g_h2s_n = c2;
                }
            }
        }
        if (raw) free(raw);
    }
    return 0;
}

void s3_text_shutdown(void) {
    free(g_big5);     g_big5 = NULL;
    free(g_h2s_in);   g_h2s_in = NULL;
    free(g_h2s_out);  g_h2s_out = NULL;
    g_h2s_n = 0;
}

void     s3_text_set_language(S3Lang lang) { g_lang = lang; }
S3Lang   s3_text_language(void)            { return g_lang; }

/* ---------------------------------------------------------------- UTF-8 */
uint32_t s3_utf8_next(const char *s, size_t *i) {
    const uint8_t *p = (const uint8_t *)s + *i;
    uint8_t b = p[0];
    if (b < 0x80) { *i += 1; return b; }
    if ((b & 0xE0) == 0xC0) {
        if ((p[1] & 0xC0) != 0x80) { *i += 1; return REPLACEMENT; }
        *i += 2;
        return ((uint32_t)(b & 0x1F) << 6) | (p[1] & 0x3F);
    }
    if ((b & 0xF0) == 0xE0) {
        if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) { *i += 1; return REPLACEMENT; }
        *i += 3;
        return ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    if ((b & 0xF8) == 0xF0) {
        if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) {
            *i += 1; return REPLACEMENT;
        }
        *i += 4;
        return ((uint32_t)(b & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12)
             | ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    }
    *i += 1;
    return REPLACEMENT;
}

int s3_utf8_encode(uint32_t cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* ---------------------------------------------------------------- 繁→简 */
uint32_t s3_hant_to_hans_cp(uint32_t cp) {
    if (!g_h2s_n) return cp;
    uint32_t lo = 0, hi = g_h2s_n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (g_h2s_in[mid] < cp) lo = mid + 1;
        else                     hi = mid;
    }
    if (lo < g_h2s_n && g_h2s_in[lo] == cp) return g_h2s_out[lo];
    return cp;
}

/* ---------------------------------------------------------------- 整串转换 */
int s3_big5_to_utf8(const uint8_t *src, size_t n, char *out, size_t cap) {
    size_t o = 0;
    if (cap == 0) return 0;
    for (size_t i = 0; i < n; ) {
        uint8_t b = src[i];
        uint32_t cp;
        if (b < 0x80) { cp = b; i += 1; }
        else if (b >= B5_LEAD_LO && b <= B5_LEAD_HI && i + 1 < n) {
            uint8_t t = src[i + 1];
            int ti = -1;
            if (t >= 0x40 && t <= 0x7E)      ti = t - 0x40;
            else if (t >= 0xA1 && t <= 0xFE) ti = 63 + (t - 0xA1);
            if (ti >= 0) {
                uint16_t m = g_big5 ? g_big5[(size_t)(b - B5_LEAD_LO) * B5_TRAILS + (size_t)ti] : 0;
                if (m) { cp = m; i += 2; }
                else   { cp = REPLACEMENT; i += 1; }
            } else { cp = REPLACEMENT; i += 1; }
        } else { cp = REPLACEMENT; i += 1; }

        char tmp[4];
        int k = s3_utf8_encode(cp, tmp);
        if (o + (size_t)k + 1 > cap) break;   /* 预留结尾 '\0' */
        memcpy(out + o, tmp, (size_t)k);
        o += (size_t)k;
    }
    out[o] = '\0';
    return (int)o;
}

int s3_text_display(const char *utf8, char *out, size_t cap) {
    size_t i = 0, o = 0;
    if (cap == 0) return 0;
    while (utf8[i]) {
        uint32_t cp = s3_utf8_next(utf8, &i);
        if (g_lang == S3_LANG_HANS) cp = s3_hant_to_hans_cp(cp);
        char tmp[4];
        int k = s3_utf8_encode(cp, tmp);
        if (o + (size_t)k + 1 > cap) break;   /* 预留结尾 '\0' */
        memcpy(out + o, tmp, (size_t)k);
        o += (size_t)k;
    }
    out[o] = '\0';
    return (int)o;
}

int s3_big5_to_display(const uint8_t *src, size_t n, char *out, size_t cap) {
    /* 两段式：先 UTF-8，再按显示语言替换。中间缓冲区按 4 字节/源字节上界估算。 */
    size_t mid_cap = n * 4 + 1;
    char *mid = (char *)malloc(mid_cap);
    if (!mid) return s3_big5_to_utf8(src, n, out, cap);
    s3_big5_to_utf8(src, n, mid, mid_cap);
    int r = s3_text_display(mid, out, cap);
    free(mid);
    return r;
}
