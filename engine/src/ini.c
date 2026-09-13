/*
 * ini.c —— Setting\*.ini 解析器
 *
 * 全部字符串都指向 ini->buf（解码后的 UTF-8 全文）内部，不额外分配，
 * 因此释放时只需 free(buf) + free 各层数组。
 */
#include "ini.h"
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- 小工具 */
static char *trim_inplace(char *s) {
    while (*s == ' ' || *s == '\t') ++s;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = '\0';
    return s;
}

/* 严格整数：整段（去首尾空白）必须合法，否则返回 def —— 与 Python int() 抛异常的口径一致 */
static int parse_int_strict(const char *s, int def) {
    if (!s) return def;
    while (*s == ' ' || *s == '\t') ++s;
    const char *p = s;
    int sign = 1;
    if (*p == '+' || *p == '-') { if (*p == '-') sign = -1; ++p; }
    if (*p < '0' || *p > '9') return def;
    long v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        if (v > 2000000000L) v = 2000000000L;   /* 防溢出（游戏数据不会有这么大） */
        ++p;
    }
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != '\0') return def;
    return (int)(sign * v);
}

/* 取一段以 ',' 分隔的第 idx 段，写入 out（不超出 cap）。返回 0 成功，-1 越界。 */
static int nth_segment(const char *s, int idx, char *out, size_t cap) {
    if (!s || idx < 0) return -1;
    int cur = 0;
    const char *p = s;
    while (1) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (cur == idx) {
            while (len > 0 && (*p == ' ' || *p == '\t')) { ++p; --len; }
            while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t')) --len;
            if (len + 1 > cap) len = cap - 1;
            memcpy(out, p, len);
            out[len] = '\0';
            return 0;
        }
        if (!comma) return -1;
        p = comma + 1;
        ++cur;
    }
}

static char *dup_n(const char *s, size_t n) {
    char *r = (char *)malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

/* ---------------------------------------------------------------- 解析 */
S3Ini *s3_ini_parse(const unsigned char *data, size_t n) {
    if (!data) return NULL;

    S3Ini *ini = (S3Ini *)calloc(1, sizeof(S3Ini));
    if (!ini) return NULL;

    /* Big5 → UTF-8。最坏情况是每 2 字节 Big5 变 3 字节 UTF-8，取 2n+16 足够。 */
    size_t cap = n * 2 + 16;
    ini->buf = (char *)malloc(cap);
    if (!ini->buf) { free(ini); return NULL; }
    s3_big5_to_utf8(data, n, ini->buf, cap);

    int sec_cap = 0, key_cap = 0;
    S3IniSection *cur = NULL;

    char *p = ini->buf;
    while (*p) {
        char *line = p;
        char *nl = strchr(p, '\n');
        if (nl) { *nl = '\0'; p = nl + 1; } else { p += strlen(p); }

        char *s = trim_inplace(line);
        if (!*s) continue;
        if (*s == ';' || *s == '#') continue;

        size_t sl = strlen(s);
        if (*s == '[' && s[sl - 1] == ']') {
            char *name = dup_n(s + 1, sl - 2);
            if (!name) break;
            if (ini->n_sections == sec_cap) {
                int nc = sec_cap ? sec_cap * 2 : 16;
                S3IniSection *ns = (S3IniSection *)realloc(ini->sections, sizeof(S3IniSection) * (size_t)nc);
                if (!ns) { free(name); break; }
                ini->sections = ns;
                sec_cap = nc;
            }
            cur = &ini->sections[ini->n_sections++];
            memset(cur, 0, sizeof(*cur));
            cur->name = name;
            key_cap = 0;
            continue;
        }

        char *eq = strchr(s, '=');
        if (!eq || !cur) continue;          /* 无 '='，或出现在任何 section 之前 → 丢弃 */
        *eq = '\0';
        char *k = trim_inplace(s);
        char *v = trim_inplace(eq + 1);
        if (!*k) continue;

        /* 同 section 内查同键 */
        S3IniKey *slot = NULL;
        for (int i = 0; i < cur->n_keys; ++i) {
            if (strcmp(cur->keys[i].key, k) == 0) { slot = &cur->keys[i]; break; }
        }
        if (!slot) {
            if (cur->n_keys == key_cap) {
                int nc = key_cap ? key_cap * 2 : 32;
                S3IniKey *nk = (S3IniKey *)realloc(cur->keys, sizeof(S3IniKey) * (size_t)nc);
                if (!nk) break;
                cur->keys = nk;
                key_cap = nc;
            }
            slot = &cur->keys[cur->n_keys++];
            slot->key = k;
            slot->vals = NULL;
            slot->n_vals = 0;
        }
        char **nv = (char **)realloc(slot->vals, sizeof(char *) * (size_t)(slot->n_vals + 1));
        if (!nv) break;
        slot->vals = nv;
        slot->vals[slot->n_vals++] = v;
    }
    return ini;
}

/* ------------------------------------------------ #include 预处理（M2 UI 布局） */
#define S3_INI_INCLUDE_DEPTH_MAX 8

typedef struct { unsigned char *p; size_t n, cap; } IncBuf;

static int incbuf_reserve(IncBuf *b, size_t extra) {
    if (b->n + extra <= b->cap) return 0;
    size_t nc = b->cap ? b->cap : 4096;
    while (nc < b->n + extra) nc *= 2;
    unsigned char *np = (unsigned char *)realloc(b->p, nc);
    if (!np) return -1;
    b->p = np; b->cap = nc;
    return 0;
}
static int incbuf_append(IncBuf *b, const void *d, size_t n) {
    if (!n) return 0;
    if (incbuf_reserve(b, n) != 0) return -1;
    memcpy(b->p + b->n, d, n);
    b->n += n;
    return 0;
}

static int is_space_ch(unsigned char c) { return c == ' ' || c == '\t' || c == '\r'; }

/* 逐行扫描：#include 行替换为被包含文件内容（递归）；其余原样保留（含换行）。 */
static int expand_includes(const unsigned char *data, size_t n, S3IniIncludeCb cb,
                           void *ud, int depth, IncBuf *out) {
    if (!cb || depth >= S3_INI_INCLUDE_DEPTH_MAX) return incbuf_append(out, data, n);
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && data[j] != '\n') ++j;
        size_t s = i;
        while (s < j && is_space_ch(data[s])) ++s;

        int is_inc = 0;
        if (j > s && data[s] == '#') {
            static const char KW[] = "#include";
            const size_t KL = sizeof(KW) - 1;
            if (j - s >= KL + 1 && memcmp(data + s, KW, KL) == 0) is_inc = 1;
        }
        if (is_inc) {
            size_t fs = s + 8, fe = j;              /* 8 = strlen("#include") */
            while (fs < fe && is_space_ch(data[fs])) ++fs;
            while (fe > fs && is_space_ch(data[fe - 1])) --fe;
            if (fe > fs && data[fs] == '"') {        /* 允许 #include "x.ini" */
                ++fs;
                if (fe > fs && data[fe - 1] == '"') --fe;
            }
            if (fe > fs) {
                char name[512];
                size_t nl = fe - fs;
                if (nl > sizeof(name) - 1) nl = sizeof(name) - 1;
                memcpy(name, data + fs, nl);
                name[nl] = '\0';
                unsigned char *sub = NULL; size_t subn = 0;
                if (cb(name, &sub, &subn, ud) == 0 && sub && subn) {
                    expand_includes(sub, subn, cb, ud, depth + 1, out);
                    free(sub);
                    i = (j < n) ? j + 1 : n;         /* include 行本身不保留 */
                    continue;
                }
                if (sub) free(sub);
            }
        }
        size_t copy_len = (j < n) ? (j - i + 1) : (n - i);
        if (incbuf_append(out, data + i, copy_len) != 0) return -1;
        i = (j < n) ? j + 1 : n;
    }
    return 0;
}

S3Ini *s3_ini_parse_inc(const unsigned char *data, size_t n, S3IniIncludeCb cb, void *ud) {
    if (!data) return NULL;
    if (!cb) return s3_ini_parse(data, n);
    IncBuf b; b.p = NULL; b.n = 0; b.cap = 0;
    if (expand_includes(data, n, cb, ud, 0, &b) != 0) { free(b.p); return NULL; }
    S3Ini *ini = s3_ini_parse(b.p, b.n);
    free(b.p);
    return ini;
}

static unsigned char *read_all_bytes(const char *path, size_t *out_n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return NULL; }
    *out_n = got;
    return buf;
}

S3Ini *s3_ini_load_inc(const char *path, S3IniIncludeCb cb, void *ud) {
    size_t n = 0;
    unsigned char *buf = read_all_bytes(path, &n);
    if (!buf) return NULL;
    S3Ini *ini = s3_ini_parse_inc(buf, n, cb, ud);
    free(buf);
    return ini;
}

S3Ini *s3_ini_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return NULL; }
    S3Ini *ini = s3_ini_parse(buf, got);
    free(buf);
    return ini;
}

void s3_ini_free(S3Ini *ini) {
    if (!ini) return;
    for (int i = 0; i < ini->n_sections; ++i) {
        S3IniSection *s = &ini->sections[i];
        for (int j = 0; j < s->n_keys; ++j) free(s->keys[j].vals);
        free(s->keys);
        free(s->name);
    }
    free(ini->sections);
    free(ini->buf);
    free(ini);
}

/* ---------------------------------------------------------------- 查询 */
const S3IniSection *s3_ini_section_at(const S3Ini *ini, const char *name, int idx) {
    if (!ini || !name) return NULL;
    int seen = 0;
    for (int i = 0; i < ini->n_sections; ++i) {
        if (strcmp(ini->sections[i].name, name) == 0) {
            if (seen == idx) return &ini->sections[i];
            ++seen;
        }
    }
    return NULL;
}

const S3IniSection *s3_ini_section(const S3Ini *ini, const char *name) {
    return s3_ini_section_at(ini, name, 0);
}

static const S3IniKey *find_key(const S3IniSection *sec, const char *key) {
    if (!sec || !key) return NULL;
    for (int i = 0; i < sec->n_keys; ++i) {
        if (strcmp(sec->keys[i].key, key) == 0) return &sec->keys[i];
    }
    return NULL;
}

const char *s3_ini_val_at(const S3IniSection *sec, const char *key, int idx) {
    const S3IniKey *k = find_key(sec, key);
    if (!k || idx < 0 || idx >= k->n_vals) return NULL;
    return k->vals[idx];
}

const char *s3_ini_str(const S3IniSection *sec, const char *key, const char *def) {
    const char *v = s3_ini_val_at(sec, key, 0);
    return v ? v : def;
}

int s3_ini_count(const S3IniSection *sec, const char *key) {
    const S3IniKey *k = find_key(sec, key);
    return k ? k->n_vals : 0;
}

int s3_ini_int_at(const S3IniSection *sec, const char *key, int idx, int def) {
    const char *v = s3_ini_val_at(sec, key, 0);
    if (!v) return def;
    char seg[64];
    if (nth_segment(v, idx, seg, sizeof seg) != 0) return def;
    return parse_int_strict(seg, def);
}

int s3_ini_int(const S3IniSection *sec, const char *key, int def) {
    return s3_ini_int_at(sec, key, 0, def);
}

int s3_ini_seg(const S3IniSection *sec, const char *key, int idx, char *out, size_t cap) {
    const char *v = s3_ini_val_at(sec, key, 0);
    if (!v) return -1;
    return nth_segment(v, idx, out, cap);
}

int s3_ini_int_list(const S3IniSection *sec, const char *key, int *out, int out_max) {
    if (out_max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < out_max; ++i) {
        char seg[64];
        if (s3_ini_seg(sec, key, i, seg, sizeof seg) != 0) break;
        if (!*seg) continue;                 /* 空白段丢弃 */
        out[n++] = parse_int_strict(seg, 0); /* 非法段记 0，与 to_int 一致 */
    }
    return n;
}

static int all_digits(const char *s) {
    if (!*s) return 0;
    for (const char *p = s; *p; ++p) if (*p < '0' || *p > '9') return 0;
    return 1;
}

int s3_ini_int_list_digits(const S3IniSection *sec, const char *key, int *out, int out_max) {
    if (out_max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < out_max; ++i) {
        char seg[64];
        if (s3_ini_seg(sec, key, i, seg, sizeof seg) != 0) break;
        if (!*seg || !all_digits(seg)) continue;
        out[n++] = parse_int_strict(seg, 0);
    }
    return n;
}
