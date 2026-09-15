/*
 * editor_scene.h —— 自定义武将创建表单（M3-lite，玩法优先的轻量实现）
 *
 * 与 menu_scene 的分工：menu_scene 渲染 Menu.ini 声明的界面；本模块渲染一个
 * **自绘表单**（输入框/按钮/头像预览），供「登录武将」创建自定义武将。
 * 不依赖 S3UiLayout：背景由调用方渲染（借 root 1 的 ICON_ONLY），本模块只画表单。
 *
 * 输入：调用方把 SDL_TEXTINPUT / SDL_KEYDOWN 转发进来（presenter 轮询接口）。
 * 保存：JSON Lines 追加（每行一个武将），开局注入属后续里程碑。
 */
#ifndef SANGO3_EDITOR_SCENE_H
#define SANGO3_EDITOR_SCENE_H

#include <stdint.h>
#include "render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*S3EditorDrawText)(void *ud, Sango3Canvas *cv, const char *utf8,
                                 int32_t x, int32_t y, int32_t w, int32_t h,
                                 uint32_t rgb, int font, uint32_t style);
/* 读素材（PAK），返回 malloc 缓冲（调用方内部 free） */
typedef uint8_t *(*S3EditorReadAsset)(void *ud, const char *path, uint32_t *out_len);

typedef struct S3Editor S3Editor;

S3Editor *s3_editor_new(S3EditorDrawText draw_text, void *text_ud,
                        S3EditorReadAsset read_asset, void *asset_ud);
void      s3_editor_free(S3Editor *e);

/* 渲染整帧表单（画布已由调用方画好背景）。 */
void s3_editor_render(S3Editor *e, Sango3Canvas *cv);

/* 交互（逻辑坐标）：
 *   click      —— 左键抬起且位置未变（与主菜单同语义，由 app 判定后转发）
 *   text       —— SDL_TEXTINPUT 的 UTF-8 文本
 *   key        —— SDL_KEYDOWN 的 keysym.sym（本模块只认 BACKSPACE=8）
 * 返回值（每次调用后查询 result）：
 *   0 = 继续编辑；1 = 已请求创建（调 save 后回主菜单）；2 = 取消 */
void s3_editor_on_click(S3Editor *e, int32_t lx, int32_t ly);
void s3_editor_on_text(S3Editor *e, const char *utf8);
void s3_editor_on_key(S3Editor *e, int32_t sym);
int  s3_editor_result(const S3Editor *e);

/* 把当前表单追加写入 JSONL 文件（UTF-8，每行一个对象）。返回 0 成功。 */
int  s3_editor_save(const S3Editor *e, const char *jsonl_path);

/* 是否处于文本输入态（调用方据此开/关 SDL_StartTextInput）。 */
int  s3_editor_typing(const S3Editor *e);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_EDITOR_SCENE_H */
