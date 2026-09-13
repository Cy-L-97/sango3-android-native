/*
 * sango3scene.c —— 分辨率无关渲染演示（M1 里程碑产物）
 *
 * 做什么：
 *   ① 用**原版真实素材**在 640×480 逻辑坐标画布上搭一个模拟游戏界面；
 *   ② 把同一张逻辑画布呈现在 1080p / 2K / 4K 三种物理分辨率上，
 *      并对比 nearest / bilinear / sharp 三种放大滤镜；
 *   ③ 用**物理像素直绘**的方式在内容区画边框与刻线 ——
 *      这些线条在任何分辨率下都是 1 像素锐利，而画面内容是放大的。
 *      这一对比就是"整帧放大"与"原生分辨率渲染 UI"两条路径的直观分界。
 *
 * 输出：outdir/<name>.raw（RGBA8888 原始像素），由 tools/render_scene.py 转 PNG。
 * 元数据以 `OUT ...` 行输出，供 Python 侧解析。
 */
#include "pak.h"
#include "shp.h"
#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOGICAL_W 640
#define LOGICAL_H 480

typedef struct {
    int32_t      w, h;
    Sango3Filter f;
    const char  *name;
} Config;

static const Config CONFIGS[] = {
    {  640,  480, SANGO3_FILTER_NEAREST,  "01_original_640x480"        },
    { 1920, 1080, SANGO3_FILTER_NEAREST,  "02_fhd_1920x1080_nearest"   },
    { 1920, 1080, SANGO3_FILTER_SHARP,    "03_fhd_1920x1080_sharp"     },
    { 1920, 1080, SANGO3_FILTER_BILINEAR, "04_fhd_1920x1080_bilinear"  },
    { 2560, 1440, SANGO3_FILTER_NEAREST,  "05_qhd_2560x1440_nearest"   },
    { 2560, 1440, SANGO3_FILTER_SHARP,    "06_qhd_2560x1440_sharp"     },
    { 3840, 2160, SANGO3_FILTER_SHARP,    "07_uhd_3840x2160_sharp"     },
};
#define NCONFIG ((int)(sizeof(CONFIGS) / sizeof(CONFIGS[0])))

static const char *filter_name(Sango3Filter f) {
    switch (f) {
        case SANGO3_FILTER_BILINEAR: return "bilinear";
        case SANGO3_FILTER_SHARP:    return "sharp";
        default:                     return "nearest";
    }
}

static const char *aspect_name(Sango3Aspect a) {
    return (a == SANGO3_ASPECT_STRETCH) ? "stretch" : "pillarbox";
}

/* 按关键字取前 limit 个 SHP，按网格贴到逻辑画布（zoom 模拟素材分级档位） */
static int32_t draw_sprites(const PakArchive *ar, const char *kw, int32_t limit,
                            Sango3Canvas *c, int32_t x0, int32_t y0,
                            int32_t step_x, int32_t step_y, int32_t cols,
                            int32_t zoom) {
    int32_t drawn = 0, found = 0;
    for (uint32_t i = 0; i < ar->entries && found < limit; i++) {
        if (!pak_contains_ci(ar->tab[i].name, kw)) continue;
        found++;
        uint32_t len = 0;
        uint8_t *raw = pak_read(ar, i, &len);
        if (!raw) continue;
        ShpImage im;
        const char *err = NULL;
        if (shp_decode(raw, len, &im, &err)) {
            int32_t gx = x0 + (drawn % cols) * step_x;
            int32_t gy = y0 + (drawn / cols) * step_y;
            sango3_canvas_blit(c, im.rgba, (int32_t)im.width, (int32_t)im.height,
                               gx, gy, zoom);
            drawn++;
            shp_free(&im);
        }
        free(raw);
    }
    return drawn;
}

static Sango3Canvas *build_scene(const PakArchive *ar, int32_t *gen_drawn, int32_t *ui_drawn) {
    Sango3Canvas *c = sango3_canvas_new(LOGICAL_W, LOGICAL_H, 14, 16, 26);
    if (!c) return NULL;

    /* 界面骨架由**代码按逻辑坐标绘制**（不是把原版界面位图放大）——
     * 这正是"2K 下界面依然锐利"的前提：布局是矢量式的。 */

    /* 顶部标题条 */
    sango3_canvas_fill(c, 0, 0, LOGICAL_W, 34, 62, 30, 34);
    sango3_canvas_fill(c, 0, 34, LOGICAL_W, 2, 150, 118, 70);
    /* 中央立绘面板 */
    sango3_canvas_frame(c, 14, 46, LOGICAL_W - 28, 374, 2, 120, 104, 72);

    /* 15 张原版武将立绘：100×120，5 列 3 行 */
    int32_t g = draw_sprites(ar, "Shape\\Portrait\\Portrait", 15, c,
                             26, 56, 120, 122, 5, 1);

    /* 底部指令条 + 5 个按钮（代码绘制） */
    sango3_canvas_fill(c, 0, LOGICAL_H - 46, LOGICAL_W, 46, 40, 32, 24);
    sango3_canvas_fill(c, 0, LOGICAL_H - 46, LOGICAL_W, 2, 150, 118, 70);
    for (int i = 0; i < 5; i++) {
        int32_t bx = 20 + i * 122;
        sango3_canvas_fill(c, bx, LOGICAL_H - 36, 104, 28, 78, 62, 42);
        sango3_canvas_frame(c, bx, LOGICAL_H - 36, 104, 28, 1, 168, 140, 92);
    }

    if (gen_drawn) *gen_drawn = g;
    if (ui_drawn) *ui_drawn = 5;   /* 代码绘制的按钮数 */
    return c;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: sango3scene <pak> <outdir> [--only NAME]\n");
        return 1;
    }
    const char *pak_path = argv[1];
    const char *outdir   = argv[2];
    const char *only     = NULL;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) only = argv[++i];
    }

    const char *err = NULL;
    PakArchive ar;
    if (!pak_open(pak_path, &ar, &err)) {
        printf("FAIL : pak_open: %s\n", err ? err : "?");
        return 2;
    }
    printf("logical = %dx%d\n", LOGICAL_W, LOGICAL_H);
    printf("pak_entries = %u\n", ar.entries);

    int32_t g = 0, u = 0;
    Sango3Canvas *scene = build_scene(&ar, &g, &u);
    if (!scene) {
        printf("FAIL : scene build failed\n");
        pak_close(&ar);
        return 3;
    }
    printf("portraits_drawn = %d\n", g);
    printf("ui_drawn = %d\n", u);
    printf("scene_hash_fnv1a = 0x%08X\n",
           shp_fnv1a(scene->px, (size_t)scene->w * scene->h * 4));

    int emitted = 0;
    for (int i = 0; i < NCONFIG; i++) {
        const Config *cfg = &CONFIGS[i];
        if (only && strcmp(only, cfg->name) != 0) continue;

        Sango3Viewport vp;
        sango3_viewport_init(&vp, LOGICAL_W, LOGICAL_H, cfg->w, cfg->h,
                             SANGO3_ASPECT_PILLARBOX, cfg->f);

        size_t n = (size_t)cfg->w * (size_t)cfg->h * 4;
        uint8_t *out = (uint8_t *)malloc(n);
        if (!out) { printf("FAIL : out of memory at %s\n", cfg->name); continue; }

        sango3_present(scene->px, &vp, out, 0, 0, 0);

        /* ---- 关键演示：物理像素直绘 UI（放大不会糊）---- */
        /* 内容区上下边框 + 左上角 40 像素长的 1px 刻线 */
        sango3_fill_rect(out, cfg->w, cfg->h, vp.dst_x, vp.dst_y,
                         vp.dst_w, 2, 220, 190, 120, 255);
        sango3_fill_rect(out, cfg->w, cfg->h, vp.dst_x, vp.dst_y + vp.dst_h - 2,
                         vp.dst_w, 2, 220, 190, 120, 255);
        for (int32_t t = 0; t < 40; t++)
            sango3_fill_rect(out, cfg->w, cfg->h, vp.dst_x + 10 + t, vp.dst_y + 46,
                             1, 1, 255, 96, 64, 255);

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s.raw", outdir, cfg->name);
        FILE *f = fopen(path, "wb");
        size_t wrote = 0;
        if (f) { wrote = fwrite(out, 1, n, f); fclose(f); }
        free(out);
        if (wrote != n) { printf("FAIL : write %s\n", path); continue; }

        /* 素材分级选档 + 逻辑→物理坐标变换（命中测试通路）自检 */
        int32_t tier = sango3_pick_asset_tier(vp.scale, 3);
        float px = 0, py = 0;
        sango3_logical_to_physical(&vp, 100.0f, 56.0f, &px, &py);

        /* 只回 ASCII 名（不输出路径）：中文路径经控制台代码页会被破坏，
         * 路径拼接由 Python 侧负责 —— 这是本项目验证过的可靠约定。 */
        printf("OUT name=%s w=%d h=%d filter=%s aspect=%s scale=%.4f tier=%d "
               "content=%d,%d,%d,%d probe_l2p=%.1f,%.1f\n",
               cfg->name, cfg->w, cfg->h, filter_name(cfg->f), aspect_name(vp.aspect),
               vp.scale, tier, vp.dst_x, vp.dst_y, vp.dst_w, vp.dst_h, px, py);
        emitted++;
    }

    printf("emitted = %d\n", emitted);
    sango3_canvas_free(scene);
    pak_close(&ar);
    return emitted ? 0 : 4;
}
