/*
 * render.h —— 分辨率无关渲染核心
 *
 * 设计前提（数据实证，见 docs/分辨率与高清化架构.md）：
 *   原版界面数据（Setting\Menu.ini）的逻辑坐标空间是 640×480，
 *   且与素材像素尺寸 1:1 对应（例：Range=105,107,433,34 ↔ 素材 433×34）。
 *
 * 因此本引擎的铁律：
 *   ① 所有玩法逻辑 / UI 布局 / 命中测试 一律用 640×480 逻辑坐标，绝不出现物理像素；
 *   ② 物理分辨率（1080p / 2K / 4K）只在这一层做"逻辑画布 → 输出"的变换；
 *   ③ 素材按 1×/2×/3× 分级，引擎按输出分辨率自动选档，缺少高清档时回退到 1× 缩放。
 *
 * 这样做的收益：先把游戏跑起来（只有原版素材也能上 2K），
 * 高清素材包最后再做 —— 做完了丢进目录即生效，不用改代码、不用重编译。
 */
#ifndef SANGO3_RENDER_H
#define SANGO3_RENDER_H

#include <stdint.h>

/* ---------------------------------------------------------------- 画布 */
/* 逻辑画布：RGBA8888，尺寸恒定 640×480（或原版坐标空间尺寸） */
typedef struct {
    uint8_t *px;
    int32_t  w, h;
} Sango3Canvas;

Sango3Canvas *sango3_canvas_new(int32_t w, int32_t h, uint8_t r, uint8_t g, uint8_t b);
void          sango3_canvas_free(Sango3Canvas *c);

/* 逻辑坐标填充矩形 */
void sango3_canvas_fill(Sango3Canvas *c, int32_t x, int32_t y,
                        int32_t w, int32_t h, uint8_t r, uint8_t g, uint8_t b);

/* 逻辑坐标描边矩形（粗细 t） */
void sango3_canvas_frame(Sango3Canvas *c, int32_t x, int32_t y, int32_t w, int32_t h,
                         int32_t t, uint8_t r, uint8_t g, uint8_t b);

/* 把 RGBA 位图以整数倍 zoom 贴到逻辑坐标 (dx,dy)，alpha over 混合。
 * zoom 模拟"素材分级"：1=原版档，2=2× 高清档，3=3× 高清档。 */
void sango3_canvas_blit(Sango3Canvas *c, const uint8_t *rgba, int32_t sw, int32_t sh,
                        int32_t dx, int32_t dy, int32_t zoom);

/* ---------------------------------------------------------------- 视口 */
/* 放大滤镜 */
typedef enum {
    SANGO3_FILTER_NEAREST  = 0, /* 最近邻：硬边，整数倍缩放时像素完美 */
    SANGO3_FILTER_BILINEAR = 1, /* 双线性：最平滑，像素艺术会糊 */
    SANGO3_FILTER_SHARP    = 2  /* 锐化双线性：整数预放大(最近邻) + 双线性收尾
                                 * —— 非整数倍放大时既保像素块又不生台阶，像素画推荐 */
} Sango3Filter;

/* 宽高比策略（4:3 逻辑画布 → 16:9 / 19.5:9 物理屏） */
typedef enum {
    SANGO3_ASPECT_PILLARBOX = 0, /* 保持 4:3，两侧留边（默认，PC 桌面） */
    SANGO3_ASPECT_STRETCH   = 1, /* 拉伸铺满（会变形，仅作对照） */
    SANGO3_ASPECT_EXTEND    = 2, /* 横向扩展视野：内容按**长边**撑满，
                                  * 余下的两侧由画布边缘条带拉伸填充（不留黑边、不变形）。
                                  * UI 仍锚定 4:3 安全区 —— 手机长屏首选。 */
    SANGO3_ASPECT_COVER     = 3  /* 铺满裁切：按**短边**撑满，溢出部分裁掉。
                                  * 适合"内容本身就是画面"的场景（战略地图）——
                                  * 无黑边、无变形、无条带痕迹。 */
} Sango3Aspect;

typedef struct {
    int32_t      logical_w, logical_h;         /* 逻辑画布尺寸（本作 640×480） */
    int32_t      out_w, out_h;                 /* 物理输出分辨率 */
    Sango3Aspect aspect;
    Sango3Filter filter;
    float        scale;                        /* 统一缩放系数（逻辑→物理） */
    int32_t      dst_x, dst_y, dst_w, dst_h;   /* 内容在输出中的矩形（居中） */
} Sango3Viewport;

void sango3_viewport_init(Sango3Viewport *vp,
                          int32_t logical_w, int32_t logical_h,
                          int32_t out_w, int32_t out_h,
                          Sango3Aspect aspect, Sango3Filter filter);

/* 逻辑画布 → 物理输出（RGBA8888）。out 需预分配 out_w*out_h*4 字节。
 * 内容矩形之外用 (bg_r,bg_g,bg_b) 填充。 */
void sango3_present(const uint8_t *logical, const Sango3Viewport *vp,
                    uint8_t *out, uint8_t bg_r, uint8_t bg_g, uint8_t bg_b);

/* ------------------------------------------------- 坐标变换（UI 命中测试用） */
void sango3_logical_to_physical(const Sango3Viewport *vp,
                                float lx, float ly, float *px, float *py);
void sango3_physical_to_logical(const Sango3Viewport *vp,
                                float px, float py, float *lx, float *ly);

/* --------------------------------------------- 物理分辨率直绘（原生清晰 UI） */
/* 在输出缓冲区上按**物理像素**画矩形。
 * 这是"2K 下界面依然锐利"的关键：边框 / 文字由本函数按目标分辨率直接绘出，
 * 而不是把 640×480 的位图放大（放大必糊）。 */
void sango3_fill_rect(uint8_t *out, int32_t out_w, int32_t out_h,
                      int32_t x, int32_t y, int32_t w, int32_t h,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a);

/* --------------------------------------------------- 素材分级（高清包选档） */
/* 按当前缩放系数挑选最合适的素材档位，返回 >=1 的整数倍。
 * 例：scale=3.0 → 3（用 3× 高清包）；scale=2.4 → 2；scale=0.8 → 1（原版档）。
 * max_tier 是磁盘上实际存在的高清档上限（由资源层探测）。 */
int32_t sango3_pick_asset_tier(float scale, int32_t max_tier);

#endif /* SANGO3_RENDER_H */
