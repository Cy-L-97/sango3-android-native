/*
 * shp.c —— SHP/TLHS 精灵解码实现（详见 shp.h）
 */
#include "shp.h"

#include <stdlib.h>
#include <string.h>

#define MAGIC        "TLHS"
#define HDR_SIZE     0x24      /* 头部固定长度，也是偏移表起点 */
#define FRAME_HDR    4         /* 每帧头部：u16 flags + u16 span */

static uint16_t rd_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (uint16_t)(p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* RGB565 -> 8/8/8，与 Python 侧 rgb565() 完全一致（整数截断规则相同） */
static void rgb565_to_888(uint16_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint32_t R = (v >> 11) & 0x1F;
    uint32_t G = (v >> 5)  & 0x3F;
    uint32_t B = v & 0x1F;
    *r = (uint8_t)(R * 255 / 31);
    *g = (uint8_t)(G * 255 / 63);
    *b = (uint8_t)(B * 255 / 31);
}

int shp_decode(const void *data, size_t len, ShpImage *out, const char **err) {
    const char *e = NULL;
    const uint8_t *d = (const uint8_t *)data;

    memset(out, 0, sizeof(*out));

    if (len < HDR_SIZE) { e = "file smaller than header"; goto fail; }
    if (memcmp(d, MAGIC, 4) != 0) { e = "bad magic (expected TLHS)"; goto fail; }

    out->type   = rd_u32(d + 0x04);
    out->key    = rd_u32(d + 0x10);
    out->width  = rd_u32(d + 0x14);
    out->height = rd_u32(d + 0x18);

    if (out->type != 0) { e = "unsupported shp type (only type 0 handled)"; goto fail; }
    if (out->width == 0) { e = "zero width"; goto fail; }

    /* 偏移表：从 0x24 到首个数据块之间，每 4 字节一项 */
    uint32_t first = rd_u32(d + HDR_SIZE);
    if (first < HDR_SIZE || first > len) { e = "first frame offset out of range"; goto fail; }
    uint32_t n = (first - HDR_SIZE) / 4;
    if (n == 0) { e = "no frames in offset table"; goto fail; }

    out->frame_cnt = n;
    out->offsets = (uint32_t *)malloc(sizeof(uint32_t) * n);
    if (!out->offsets) { e = "oom (offsets)"; goto fail; }
    for (uint32_t i = 0; i < n; i++)
        out->offsets[i] = rd_u32(d + HDR_SIZE + i * 4);

    /* 行数取帧数（M0 实测：height 字段与帧数一致） */
    out->height = n;

    size_t total = (size_t)out->width * out->height;
    out->rgba = (uint8_t *)calloc(total, 4);
    if (!out->rgba) { e = "oom (rgba)"; goto fail; }

    for (uint32_t y = 0; y < out->height; y++) {
        uint32_t off = out->offsets[y];
        uint32_t end = (y + 1 < out->frame_cnt) ? out->offsets[y + 1] : (uint32_t)len;
        if (off + FRAME_HDR > len || end > len || end < off) { e = "frame range invalid"; goto fail; }

        /* 帧数据区必须容纳 4 字节帧头；不足时该行留空 —— 容错，绝不越界读。
         * ⚠ 这里曾有 size_t 下溢 bug：end-off-FRAME_HDR 为负时被当成巨大无符号数，
         *   使 span 不被裁剪 → 越界读内存（root=20000/Statusbar 段错误即由此而来）。 */
        uint32_t span = rd_u16(d + off + 0x02);
        const uint8_t *body = d + off + FRAME_HDR;
        uint32_t avail = (end >= off + FRAME_HDR)
                       ? (uint32_t)((end - off - FRAME_HDR) / 2) : 0;
        if (span > avail) span = avail;             /* 容错裁剪（avail 可能为 0） */

        uint32_t npx = (span < out->width) ? span : out->width;
        uint8_t *row = out->rgba + (size_t)y * out->width * 4;
        for (uint32_t x = 0; x < npx; x++) {
            uint16_t v = rd_u16(body + x * 2);
            uint8_t r, g, b, a;
            rgb565_to_888(v, &r, &g, &b);
            a = (v == (uint16_t)out->key) ? 0 : 255;
            uint8_t *px = row + x * 4;
            px[0] = r; px[1] = g; px[2] = b; px[3] = a;
        }
    }
    return 1;

fail:
    if (err) *err = e;
    shp_free(out);
    return 0;
}

void shp_free(ShpImage *img) {
    if (!img) return;
    free(img->offsets); img->offsets = NULL;
    free(img->rgba);    img->rgba = NULL;
    img->frame_cnt = 0;
}

void shp_get_pixel(const ShpImage *img, uint32_t x, uint32_t y,
                   uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    uint8_t rr = 0, gg = 0, bb = 0, aa = 0;
    if (img && img->rgba && x < img->width && y < img->height) {
        const uint8_t *p = img->rgba + ((size_t)y * img->width + x) * 4;
        rr = p[0]; gg = p[1]; bb = p[2]; aa = p[3];
    }
    if (r) *r = rr;
    if (g) *g = gg;
    if (b) *b = bb;
    if (a) *a = aa;
}

uint32_t shp_fnv1a(const uint8_t *data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}
