/*
 * sango3pak —— 《三国群英传3》资源包检查器
 *
 * 这是原生引擎数据层的第 1 块砖：用 C 复现 M0 阶段已在 Python 侧验证过的
 * PAK 容器格式，证明「引擎用 C/C++ 读原版资源」这条路成立。
 *
 * 格式（M0 逆向结论）:
 *   头部: 'PAKS' + u32@0x04=条目数 + u32@0x08=索引偏移 + u32@0x0C=未知
 *   索引: 每 64 字节一条
 *         +0x00 u32  数据大小
 *         +0x04 u32  数据在文件中的偏移
 *         +0x08 16B  保留
 *         +0x18 40B  路径(GBK, 反斜杠, 以 NUL 结尾)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ENTRY_SIZE 64
#define NAME_OFF   0x18
#define NAME_LEN   40

typedef struct {
    uint32_t size;
    uint32_t off;
    char     name[NAME_LEN + 1];   /* GBK 原样保留，输出时按字节写 */
} PakEntry;

typedef struct {
    uint32_t  count_hint;
    uint32_t  index_off;
    uint32_t  entries;
    PakEntry *tab;
} Pak;

static uint32_t rd_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 自实现，避免依赖 MSVC 版本差异 */
static size_t bounded_len(const char *s, size_t max) {
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

static long file_size(FILE *f) {
    long cur = ftell(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, cur, SEEK_SET);
    return n;
}

/* 以 UTF-8 兼容方式输出 GBK 字节流：这里直接原样输出，由控制台/重定向决定呈现。
 * 为保证在 UTF-8 终端下可读，主程序统一用 -D CONSOLE_UTF8 时做转换（见 utf8.c）。*/
static void print_name(const char *gbk) {
    fputs(gbk, stdout);
}

static int pak_open(const char *path, Pak *pk) {
    memset(pk, 0, sizeof(*pk));
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "无法打开: %s\n", path); return 0; }

    unsigned char head[ENTRY_SIZE];
    if (fread(head, 1, ENTRY_SIZE, f) != ENTRY_SIZE) {
        fprintf(stderr, "文件过小，不是 PAK\n"); fclose(f); return 0;
    }
    if (memcmp(head, "PAKS", 4) != 0) {
        fprintf(stderr, "魔数不符: %.4s（期望 PAKS）\n", head); fclose(f); return 0;
    }
    pk->count_hint = rd_u32(head + 0x04);
    pk->index_off  = rd_u32(head + 0x08);

    long fsz = file_size(f);
    if ((long)pk->index_off > fsz) {
        fprintf(stderr, "索引偏移越界\n"); fclose(f); return 0;
    }
    size_t blob_len = (size_t)(fsz - (long)pk->index_off);
    size_t n = blob_len / ENTRY_SIZE;          /* 末尾零头丢弃 */
    unsigned char *blob = (unsigned char *)malloc(n * ENTRY_SIZE);
    if (!blob) { fprintf(stderr, "内存不足\n"); fclose(f); return 0; }

    fseek(f, (long)pk->index_off, SEEK_SET);
    size_t got = fread(blob, ENTRY_SIZE, n, f);
    fclose(f);

    pk->tab = (PakEntry *)calloc(got, sizeof(PakEntry));
    if (!pk->tab) { free(blob); fprintf(stderr, "内存不足\n"); return 0; }

    uint32_t k = 0;
    for (size_t i = 0; i < got; i++) {
        const unsigned char *rec = blob + i * ENTRY_SIZE;
        const char *nm = (const char *)(rec + NAME_OFF);
        size_t L = bounded_len(nm, NAME_LEN);
        if (L == 0) continue;                   /* 空名记录跳过 */
        pk->tab[k].size = rd_u32(rec + 0);
        pk->tab[k].off  = rd_u32(rec + 4);
        memcpy(pk->tab[k].name, nm, L);
        pk->tab[k].name[L] = '\0';
        k++;
    }
    pk->entries = k;
    free(blob);
    return 1;
}

static void pak_close(Pak *pk) { free(pk->tab); pk->tab = NULL; }

/* 极简大小写不敏感包含判断（只处理 ASCII 关键字） */
static int contains_ci(const char *hay, const char *needle) {
    if (!needle || !*needle) return 1;
    size_t hn = strlen(hay), nn = strlen(needle);
    if (nn > hn) return 0;
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t j = 0;
        for (; j < nn; j++) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
        }
        if (j == nn) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "sango3pak —— 三国群英传3 PAK 资源包检查器\n"
            "用法: sango3pak <pak文件> [关键字] [显示条数]\n"
            "例:   sango3pak Sango3.PAK Portrait 20\n");
        return 1;
    }
    const char *path = argv[1];
    const char *kw   = (argc > 2 && argv[2][0]) ? argv[2] : NULL;
    int limit        = (argc > 3) ? atoi(argv[3]) : 25;

    Pak pk;
    if (!pak_open(path, &pk)) return 2;

    printf("== %s ==\n", path);
    printf("头部声明条目数 = %u\n", pk.count_hint);
    printf("实际解析条目数 = %u\n", pk.entries);

    int shown = 0;
    for (uint32_t i = 0; i < pk.entries; i++) {
        if (!contains_ci(pk.tab[i].name, kw)) continue;
        if (shown >= limit) break;
        printf("  %12u  %10u  ", pk.tab[i].off, pk.tab[i].size);
        print_name(pk.tab[i].name);
        putchar('\n');
        shown++;
    }
    if (kw) {
        uint32_t match = 0;
        for (uint32_t i = 0; i < pk.entries; i++)
            if (contains_ci(pk.tab[i].name, kw)) match++;
        printf("关键字 \"%s\" 命中 %u 条，已显示 %d 条\n", kw, match, shown);
    }
    pak_close(&pk);
    return 0;
}
