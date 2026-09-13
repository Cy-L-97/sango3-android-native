/*
 * sango3view.h —— sango3view 跨文件接口
 */
#ifndef SANGO3_VIEW_H
#define SANGO3_VIEW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SDL2 显示后端：把 RGBA8888 画布显示到窗口。
 * frames > 0 时渲染指定帧数后自动退出（用于自动化验证/截图）；
 * frames == 0 时持续显示直到用户关窗或按 ESC。
 * 返回 0 表示正常退出，非 0 为错误码。 */
int sango3view_show_sdl(const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_VIEW_H */
