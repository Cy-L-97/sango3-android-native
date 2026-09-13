/*
 * shp_probe.c —— SHP 解码验证工具
 *
 * 打印头部字段、采样像素与整幅 RGBA 的 FNV-1a 哈希。
 * 配套 tools/verify_shp_c.py 用同一组采样点与同一哈希算法在 Python 侧复算，
 * 两边数值一致即证明 C 解码器与已 100% 验证的 Python 解码器逐像素等价。
 *
 * 用法: sango3shp <file.shp>
 */
#include "shp.h"

#include <stdio.h>
#include <stdlib.h>

static int read_file(const char *path, uint8_t **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return 0; }
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); return 0; }
    *out = buf;
    *out_len = got;
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: sango3shp <file.shp>\n");
        return 1;
    }
    const char *path = argv[1];

    uint8_t *data = NULL;
    size_t len = 0;
    if (!read_file(path, &data, &len)) {
        printf("FAIL : cannot read input file\n");
        return 2;
    }

    ShpImage img;
    const char *err = NULL;
    if (!shp_decode(data, len, &img, &err)) {
        /* 不输出路径：Windows 下 argv 为 ANSI 字节，交给调用方(Python)负责路径显示，
         * 工具自身只输出纯 ASCII 数据，从根本上避开编码问题。 */
        printf("FAIL : %s\n", err ? err : "unknown");
        free(data);
        return 3;
    }

    printf("OK\n");
    printf("  file_bytes = %zu\n", len);
    printf("  type       = %u\n", img.type);
    printf("  key        = 0x%04X\n", img.key);
    printf("  width      = %u\n", img.width);
    printf("  height     = %u\n", img.height);
    printf("  frames     = %u\n", img.frame_cnt);
    printf("  hash_fnv1a = 0x%08X\n", shp_fnv1a(img.rgba, (size_t)img.width * img.height * 4));

    /* 采样点：两侧必须使用完全相同的顺序与算法 */
    struct { uint32_t x, y; } pts[] = {
        {0, 0},
        {10, 10},
        {50, 60},
        {img.width / 2, img.height / 2},
        {img.width ? img.width - 1 : 0, 0},
        {0, img.height ? img.height - 1 : 0},
        {img.width ? img.width - 1 : 0, img.height ? img.height - 1 : 0},
    };
    for (size_t i = 0; i < sizeof(pts) / sizeof(pts[0]); i++) {
        uint8_t r, g, b, a;
        shp_get_pixel(&img, pts[i].x, pts[i].y, &r, &g, &b, &a);
        printf("  px(%u,%u) = %u,%u,%u,%u\n", pts[i].x, pts[i].y, r, g, b, a);
    }

    shp_free(&img);
    free(data);
    return 0;
}
