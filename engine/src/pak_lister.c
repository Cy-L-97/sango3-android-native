/*
 * pak_lister.c —— PAK 资源包检查器（命令行工具）
 *
 * 只做「列出/筛选条目」一件事，容器解析逻辑全部复用 engine/src/pak.c。
 *
 * 输出约定：条目的「名称」是 PAK 内原始 GBK 字节，本工具原样输出到 stdout；
 * 其余信息（统计数字）均为 ASCII。调用方（Python）按 GBK 解码读取名称。
 *
 * 用法: sango3pak <pak> [关键字] [显示条数]
 */
#include "pak.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: sango3pak <pak> [keyword] [limit]\n");
        return 1;
    }
    const char *path = argv[1];
    const char *kw   = (argc > 2 && argv[2][0]) ? argv[2] : NULL;
    int limit        = (argc > 3) ? atoi(argv[3]) : 25;
    if (limit < 0) limit = 0;

    const char *err = NULL;
    PakArchive ar;
    if (!pak_open(path, &ar, &err)) {
        printf("FAIL : %s\n", err ? err : "unknown");
        return 2;
    }

    printf("count_hint = %u\n", ar.count_hint);
    printf("entries = %u\n", ar.entries);

    int shown = 0;
    uint32_t match = 0;
    for (uint32_t i = 0; i < ar.entries; i++) {
        if (!pak_contains_ci(ar.tab[i].name, kw)) continue;
        match++;
        if (shown >= limit) continue;
        printf("ENTRY %u %u ", ar.tab[i].off, ar.tab[i].size);
        fputs(ar.tab[i].name, stdout);   /* GBK 原始字节 */
        putchar('\n');
        shown++;
    }
    printf("matched = %u\n", match);
    printf("shown = %d\n", shown);

    pak_close(&ar);
    return 0;
}
