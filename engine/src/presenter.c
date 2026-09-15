/*
 * presenter.c —— GPU 呈现器实现（M1-b 路径 A）
 *
 * 把逻辑画布作为静态纹理上传，由 GPU 缩放绘制到物理分辨率窗口。
 * 详见 presenter.h。
 */
#include "presenter.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Sango3Presenter {
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *tex;
    int32_t       logical_w, logical_h;
    int32_t       out_w, out_h;        /* 当前窗口物理尺寸（拖动缩放后会变） */
    Sango3Aspect  aspect;
    Sango3Filter  filter;
    Sango3Viewport vp;                  /* 内容矩形数学（pillarbox/stretch） */
    uint32_t      drawn;
    int           valid;
    /* 指针状态（逻辑坐标，每帧更新） */
    float         ptr_lx, ptr_ly;
    int           ptr_inside;
    int           ptr_ldown, ptr_rdown;
    int           ptr_lclick, ptr_rclick;
    /* 文本输入 / 按键队列（供表单类界面轮询；frame() 收集，poll 取走） */
    char          text_q[8][32];
    int           text_n;
    int32_t       key_q[16];
    int           key_n;
};

/* 按当前窗口尺寸重算内容矩形。
 * EXTEND：内容按**长边**撑满（不留黑边、不变形），余下两侧由 frame() 用
 *   画布边缘条带拉伸填充；UI 仍锚定 4:3 安全区，命中判定不受影响。
 * 其他（pillarbox / stretch）：沿用 render.c 的视口数学。 */
static void recompute_rect(Sango3Presenter *p) {
    if (p->aspect == SANGO3_ASPECT_COVER) {
        /* 铺满裁切：按短边撑满，目标矩形可能超出窗口（由 SDL 自动裁剪）。
         * 地图类场景用它 —— 既没黑边也没变形，只是裁掉一部分边缘。 */
        float sw = (float)p->out_w / (float)p->logical_w;
        float sh = (float)p->out_h / (float)p->logical_h;
        float scale = sw > sh ? sw : sh;
        p->vp.dst_w = (int32_t)(p->logical_w * scale + 0.5f);
        p->vp.dst_h = (int32_t)(p->logical_h * scale + 0.5f);
        p->vp.dst_x = (p->out_w - p->vp.dst_w) / 2;
        /* 高度方向**顶对齐**：画布顶部（信息条/标题）必须可见，多出来的裁在底部。
         * 注意不能居中裁 —— 那样顶部信息条会被裁到屏幕外（实测踩坑）。 */
        p->vp.dst_y = p->vp.dst_h > p->out_h ? 0 : (p->out_h - p->vp.dst_h) / 2;
        p->vp.logical_w = p->logical_w;
        p->vp.logical_h = p->logical_h;
        p->vp.out_w = p->out_w;
        p->vp.out_h = p->out_h;
        p->vp.aspect = p->aspect;
        p->vp.filter = p->filter;
        p->vp.scale = scale;
        return;
    }
    if (p->aspect == SANGO3_ASPECT_EXTEND) {
        float sr = (float)p->out_w / (float)p->out_h;          /* 屏幕比例 */
        float lr = (float)p->logical_w / (float)p->logical_h;  /* 逻辑比例 4:3 */
        float scale;
        if (sr >= lr) {                    /* 屏幕更宽（横屏）→ 按高度撑满 */
            scale = (float)p->out_h / (float)p->logical_h;
            p->vp.dst_w = (int32_t)(p->logical_w * scale + 0.5f);
            p->vp.dst_h = (int32_t)(p->logical_h * scale + 0.5f);
            p->vp.dst_x = (p->out_w - p->vp.dst_w) / 2;
            p->vp.dst_y = 0;
        } else {                           /* 屏幕更窄（竖屏）→ 按宽度撑满 */
            scale = (float)p->out_w / (float)p->logical_w;
            p->vp.dst_w = (int32_t)(p->logical_w * scale + 0.5f);
            p->vp.dst_h = (int32_t)(p->logical_h * scale + 0.5f);
            p->vp.dst_x = 0;
            p->vp.dst_y = (p->out_h - p->vp.dst_h) / 2;
        }
        p->vp.logical_w = p->logical_w;
        p->vp.logical_h = p->logical_h;
        p->vp.out_w = p->out_w;
        p->vp.out_h = p->out_h;
        p->vp.aspect = p->aspect;
        p->vp.filter = p->filter;
        p->vp.scale = scale;
        return;
    }
    sango3_viewport_init(&p->vp, p->logical_w, p->logical_h,
                         p->out_w, p->out_h, p->aspect, p->filter);
}

/* EXTEND：把内容矩形之外的区域用画布**边缘条带**拉伸填满。
 * 取 1 列会退化成纯色块，故取较宽的条带（默认 48 逻辑列）保留纹理细节。
 * 对侧一律用"近端条带镜像"填充：内容区边缘可能带 UI 文字（如右下 V2.2C），
 * 直接拉伸会出残影；镜像左/上条带则永远干净，星空类背景对称无妨。 */
#define S3_EXTEND_BAND 48

static void draw_extend_bands(Sango3Presenter *p) {
    int band = S3_EXTEND_BAND;
    if (band > p->logical_w / 2) band = p->logical_w / 2;
    if (p->vp.dst_x > 0) {                    /* 横屏：左右两侧 */
        SDL_Rect sl = { 0, 0, band, p->logical_h };
        SDL_Rect dl = { 0, 0, p->vp.dst_x, p->out_h };
        int rw = p->out_w - p->vp.dst_x - p->vp.dst_w;   /* 取整余量 */
        SDL_Rect dr = { p->vp.dst_x + p->vp.dst_w, 0,
                        rw > 0 ? rw : p->vp.dst_x, p->out_h };
        SDL_RenderCopy(p->ren, p->tex, &sl, &dl);
        SDL_RenderCopyEx(p->ren, p->tex, &sl, &dr, 0, NULL, SDL_FLIP_HORIZONTAL);
    } else if (p->vp.dst_y > 0) {             /* 竖屏：上下两侧 */
        SDL_Rect st = { 0, 0, p->logical_w, band };
        SDL_Rect dt = { 0, 0, p->out_w, p->vp.dst_y };
        int bh = p->out_h - p->vp.dst_y - p->vp.dst_h;
        SDL_Rect db = { 0, p->vp.dst_y + p->vp.dst_h, p->out_w,
                        bh > 0 ? bh : p->vp.dst_y };
        SDL_RenderCopy(p->ren, p->tex, &st, &dt);
        SDL_RenderCopyEx(p->ren, p->tex, &st, &db, 0, NULL, SDL_FLIP_VERTICAL);
    }
}

/* 物理鼠标坐标 → 逻辑坐标，并更新 inside。 */
static void update_pointer(Sango3Presenter *p, int mx, int my) {
    float lx = 0.0f, ly = 0.0f;
    sango3_physical_to_logical(&p->vp, (float)mx, (float)my, &lx, &ly);
    p->ptr_lx = lx;
    p->ptr_ly = ly;
    p->ptr_inside = (lx >= 0.0f && ly >= 0.0f &&
                     lx < (float)p->logical_w && ly < (float)p->logical_h);
}

Sango3Presenter *sango3_presenter_new(int32_t logical_w, int32_t logical_h,
                                      int32_t out_w, int32_t out_h,
                                      int resizable,
                                      Sango3Aspect aspect, Sango3Filter filter,
                                      const char *title) {
    if (logical_w <= 0 || logical_h <= 0) return NULL;
    if (out_w <= 0) out_w = logical_w;
    if (out_h <= 0) out_h = logical_h;

    Sango3Presenter *p = (Sango3Presenter *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->logical_w = logical_w;
    p->logical_h = logical_h;
    p->out_w = out_w;
    p->out_h = out_h;
    p->aspect = aspect;
    p->filter = filter;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("FAIL : SDL_Init: %s\n", SDL_GetError());
        free(p);
        return NULL;
    }

    Uint32 flags = SDL_WINDOW_SHOWN;
    if (resizable) flags |= SDL_WINDOW_RESIZABLE;
    p->win = SDL_CreateWindow(title ? title : "Sango3 Native",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              (int)out_w, (int)out_h, flags);
    if (!p->win) {
        printf("FAIL : SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        free(p);
        return NULL;
    }

    p->ren = SDL_CreateRenderer(p->win, -1, SDL_RENDERER_ACCELERATED);
    if (!p->ren) p->ren = SDL_CreateRenderer(p->win, -1, SDL_RENDERER_SOFTWARE);
    if (!p->ren) {
        printf("FAIL : SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(p->win);
        SDL_Quit();
        free(p);
        return NULL;
    }

    /* 画布内存序为 R,G,B,A（字节序）→ SDL_PIXELFORMAT_RGBA8888。
     * 注意：不可用 SDL_PIXELFORMAT_RGBA32 —— 该别名在小端机上等于 ABGR8888，
     * 会把我们的 R,G,B,A 当成 A,B,G,R，导致通道错位（旧 sango3view_sdl.c 的坑）。
     * ⚠ Android(GLES) 后端实测需 BGRA8888：用 RGBA8888 会 R/B 交换（整屏偏红）。 */
#if defined(__ANDROID__)
    /* Android(GLES) 实测：SDL 的 GL 后端在打包格式上通道顺序相对 SDL 定义是"反转"的。
     * 现象：RGBA8888 → alpha 落到 R（整屏偏红）；BGRA8888 → alpha 落到 B（整屏偏蓝）。
     * 因此指定 ABGR8888，反转后正好得到内存序 R,G,B,A。 */
    Uint32 texfmt = SDL_PIXELFORMAT_ABGR8888;
#else
    Uint32 texfmt = SDL_PIXELFORMAT_RGBA8888;
#endif
    p->tex = SDL_CreateTexture(p->ren, texfmt,
                               SDL_TEXTUREACCESS_STATIC, (int)logical_w, (int)logical_h);
    if (!p->tex) {
        printf("FAIL : SDL_CreateTexture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(p->ren);
        SDL_DestroyWindow(p->win);
        SDL_Quit();
        free(p);
        return NULL;
    }

    /* GPU 缩放滤镜：nearest 像素完美；bilinear/sharp 在 GPU 上统一走 linear。 */
    SDL_ScaleMode sm = (filter == SANGO3_FILTER_NEAREST)
                       ? SDL_ScaleModeNearest : SDL_ScaleModeLinear;
    SDL_SetTextureScaleMode(p->tex, sm);

    /* 以实际窗口尺寸为准：Android 全屏 / 高 DPI 下，SDL_CreateWindow 的尺寸可能被系统改写 */
    {
        int aw = 0, ah = 0;
        SDL_GetWindowSize(p->win, &aw, &ah);
        if (aw > 0 && ah > 0) { p->out_w = (int32_t)aw; p->out_h = (int32_t)ah; }
    }
    recompute_rect(p);
    p->valid = 1;
    return p;
}

void sango3_presenter_free(Sango3Presenter *p) {
    if (!p) return;
    if (p->tex)  SDL_DestroyTexture(p->tex);
    if (p->ren)  SDL_DestroyRenderer(p->ren);
    if (p->win)  SDL_DestroyWindow(p->win);
    SDL_Quit();
    free(p);
}

int sango3_presenter_valid(const Sango3Presenter *p) {
    return p && p->valid;
}

void sango3_presenter_upload(Sango3Presenter *p, const uint8_t *rgba) {
    if (!p || !p->valid || !rgba) return;
    int pitch = (int)(p->logical_w * 4);
    if (SDL_UpdateTexture(p->tex, NULL, rgba, pitch) != 0) {
        printf("WARN : SDL_UpdateTexture: %s\n", SDL_GetError());
    }
}

int sango3_presenter_frame(Sango3Presenter *p, uint32_t *out_drawn) {
    if (!p || !p->valid) return 1;

    int quit = 0;
    p->ptr_lclick = 0;   /* 按下边沿每帧清零 */
    p->ptr_rclick = 0;
    p->text_n = 0;
    p->key_n = 0;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) quit = 1;
        else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) quit = 1;
        else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_AC_BACK) {
            /* Android 返回键：当作"右键返回"的按下边沿交给应用（不退出）。
             * 系统默认行为（Back 退出 Activity）已由 SDL_HINT_ANDROID_TRAP_BACK_BUTTON 拦截。 */
            p->ptr_rclick = 1;
        }
        else if (e.type == SDL_KEYDOWN) {
            if (p->key_n < 16) p->key_q[p->key_n++] = (int32_t)e.key.keysym.sym;
        }
        else if (e.type == SDL_TEXTINPUT) {
            if (p->text_n < 8) snprintf(p->text_q[p->text_n++], 32, "%s", e.text.text);
        }
        else if (e.type == SDL_WINDOWEVENT &&
                 e.window.event == SDL_WINDOWEVENT_RESIZED) {
            /* 拖动缩放：读取新窗口尺寸并重算内容矩形 */
            int w = 0, h = 0;
            SDL_GetWindowSize(p->win, &w, &h);
            if (w > 0 && h > 0) { p->out_w = (int32_t)w; p->out_h = (int32_t)h; recompute_rect(p); }
        }
        else if (e.type == SDL_MOUSEMOTION) {
            update_pointer(p, e.motion.x, e.motion.y);
        }
        else if (e.type == SDL_MOUSEBUTTONDOWN) {
            if (e.button.button == SDL_BUTTON_LEFT)  { p->ptr_ldown = 1; p->ptr_lclick = 1; }
            if (e.button.button == SDL_BUTTON_RIGHT) { p->ptr_rdown = 1; p->ptr_rclick = 1; }
            update_pointer(p, e.button.x, e.button.y);
        }
        else if (e.type == SDL_MOUSEBUTTONUP) {
            if (e.button.button == SDL_BUTTON_LEFT)  p->ptr_ldown = 0;
            if (e.button.button == SDL_BUTTON_RIGHT) p->ptr_rdown = 0;
            update_pointer(p, e.button.x, e.button.y);
        }
    }
    if (quit) return 1;

    SDL_SetRenderDrawColor(p->ren, 0, 0, 0, 255);
    SDL_RenderClear(p->ren);

    if (p->aspect == SANGO3_ASPECT_EXTEND) draw_extend_bands(p);

    SDL_Rect dst = { p->vp.dst_x, p->vp.dst_y, p->vp.dst_w, p->vp.dst_h };
    if (SDL_RenderCopy(p->ren, p->tex, NULL, &dst) != 0) {
        printf("WARN : SDL_RenderCopy: %s\n", SDL_GetError());
    }
    SDL_RenderPresent(p->ren);

    p->drawn++;
    if (out_drawn) *out_drawn = p->drawn;
    return 0;
}

/* 运行时切换宽高比策略（如：菜单用 EXTEND、战略地图用 COVER）。 */
void sango3_presenter_set_aspect(Sango3Presenter *p, Sango3Aspect aspect) {
    if (!p || p->aspect == aspect) return;
    p->aspect = aspect;
    recompute_rect(p);
}

/* ---- 文本输入 / 按键轮询（frame() 收集，表单类界面取用；取走即出队） ---- */
int sango3_presenter_poll_text(Sango3Presenter *p, char out[32]) {
    if (!p || p->text_n <= 0) return 0;
    memcpy(out, p->text_q[0], 32);
    for (int i = 1; i < p->text_n; ++i)
        memcpy(p->text_q[i - 1], p->text_q[i], 32);
    --p->text_n;
    return 1;
}

int sango3_presenter_poll_key(Sango3Presenter *p, int32_t *sym) {
    if (!p || p->key_n <= 0) return 0;
    if (sym) *sym = p->key_q[0];
    for (int i = 1; i < p->key_n; ++i) p->key_q[i - 1] = p->key_q[i];
    --p->key_n;
    return 1;
}

void sango3_presenter_content_rect(const Sango3Presenter *p,
                                  int32_t *x, int32_t *y, int32_t *w, int32_t *h) {    if (!p) return;
    if (x) *x = p->vp.dst_x;
    if (y) *y = p->vp.dst_y;
    if (w) *w = p->vp.dst_w;
    if (h) *h = p->vp.dst_h;
}

void sango3_presenter_window_size(const Sango3Presenter *p, int32_t *w, int32_t *h) {
    if (!p) return;
    if (w) *w = p->out_w;
    if (h) *h = p->out_h;
}

float sango3_presenter_scale(const Sango3Presenter *p) {
    return p ? p->vp.scale : 1.0f;
}

void sango3_presenter_pointer(const Sango3Presenter *p, S3Pointer *out) {
    if (!out) return;
    if (!p) { memset(out, 0, sizeof *out); return; }
    out->lx     = p->ptr_lx;
    out->ly     = p->ptr_ly;
    out->inside = p->ptr_inside;
    out->ldown  = p->ptr_ldown;
    out->rdown  = p->ptr_rdown;
    out->lclick = p->ptr_lclick;
    out->rclick = p->ptr_rclick;
}
