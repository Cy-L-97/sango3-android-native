/*
 * sango3view_sdl.c —— sango3view 的 SDL2 显示后端
 *
 * 把一张 RGBA8888 画布显示到窗口，按 ESC / 关闭窗口退出。
 * 注意：SDL_MAIN_HANDLED 已由构建系统定义，这里的 main 保持为真 main。
 */
#include "sango3view.h"

#include <SDL.h>
#include <stdio.h>

int sango3view_show_sdl(const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t frames) {
    if (!rgba || w == 0 || h == 0) return 10;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("FAIL : SDL_Init: %s\n", SDL_GetError());
        return 11;
    }
    printf("sdl_video_driver = %s\n", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(null)");

    SDL_Window *win = SDL_CreateWindow("Sango3 Native - Render Probe",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       (int)w, (int)h, SDL_WINDOW_SHOWN);
    if (!win) {
        printf("FAIL : SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 12;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        printf("FAIL : SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 13;
    }

    /* RGBA8888 在小端机内存里就是 R,G,B,A 字节序 → SDL_PIXELFORMAT_ABGR8888 */
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888,
                                         SDL_TEXTUREACCESS_STATIC, (int)w, (int)h);
    if (!tex) {
        printf("FAIL : SDL_CreateTexture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 14;
    }
    SDL_UpdateTexture(tex, NULL, rgba, (int)w * 4);

    int running = 1;
    uint32_t drawn = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = 0;
        }
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        drawn++;
        if (frames > 0 && drawn >= frames) running = 0;
        SDL_Delay(16);
    }
    printf("frames_drawn = %u\n", drawn);

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    printf("window_closed = 1\n");
    return 0;
}
