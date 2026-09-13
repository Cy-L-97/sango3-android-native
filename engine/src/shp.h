/*
 * shp.h —— 《三国群英传3》SHP/TLHS 精灵解码器
 *
 * 格式（M0 阶段逆向结论，已在 Python 侧 617 张素材上 100% 验证）:
 *   头部 0x00~0x23:
 *     +0x00  'TLHS'  魔数
 *     +0x04  u32     type  (0 = 常规, 1 = 其它布局, 暂不支持)
 *     +0x08  u32     = 2
 *     +0x0C  u32     未知
 *     +0x10  u32     key   透明键色 (RGB565; Portrait=0xFFFF, UI 条=0x07E0)
 *     +0x14  u32     width
 *     +0x18  u32     height (常规情形下等于帧数)
 *     +0x1C  u32     = 0
 *     +0x20  u32     = 0
 *     +0x24  u32×N   帧偏移表；N = (delta 至首个数据块) / 4
 *   每帧:
 *     +0x00  u16     flags
 *     +0x02  u16     span   本帧有效像素宽度
 *     +0x04  u16×span RGB565 小端像素
 *   语义：每帧 = 图像的一行（第 i 帧填充第 i 行）。
 *
 * 解码输出统一为 RGBA8888（行优先，width*height*4 字节），便于直接喂给
 * SDL 纹理或 OpenGL ES。透明键像素 alpha=0。
 */
#ifndef SANGO3_SHP_H
#define SANGO3_SHP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t  type;
    uint32_t  key;        /* 透明键色 RGB565 */
    uint32_t  width;
    uint32_t  height;     /* 实际解码行数（= 帧数） */
    uint32_t  frame_cnt;
    uint32_t *offsets;    /* frame_cnt 个帧偏移 */
    uint8_t  *rgba;       /* width * height * 4 */
} ShpImage;

/* 从内存中的 SHP 数据解码。成功返回 1，失败返回 0（并置 *err 说明原因）。 */
int  shp_decode(const void *data, size_t len, ShpImage *out, const char **err);

/* 释放 shp_decode 分配的资源。可安全重复调用。 */
void shp_free(ShpImage *img);

/* 采样：返回指定像素的 R/G/B/A。越界返回 0。 */
void shp_get_pixel(const ShpImage *img, uint32_t x, uint32_t y,
                   uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);

/* 对 RGBA 缓冲区做 FNV-1a 32 位哈希，用于跨语言一致性校验。 */
uint32_t shp_fnv1a(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_SHP_H */
