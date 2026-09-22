/*
 * admin_menu.h —— 行政主選單（原版 Menu.ini 的 8000 面板）
 *
 * 逆向结论（详见 docs/城池信息面板与行政菜单.md 第五节）：
 *   · 7 个按钮 121×29、纵向间距 33，素材 Shape\AD\Btn\<Icon>[2|3].shp（三态）
 *       8001 內政 Interior · 8002 軍政 Military · 8003 外交 Diplomacy · 8004 任免 Appoint
 *       8005 計略 Strategy · 8006 系統 System   · 8007 休息 Rest
 *   · 7 个子选单 8011~8017，x = 134（相对 8000），y = 6 + 33*i（与按钮对齐）；
 *     框素材 Shape\AD\Base\MenuFrame0N.shp，**N = 项数**（原版 rows 字段）。
 *   · 命令名**不在 INI 里**（原版由代码填充），本模块自定（线索见 Setting\Text.ini）。
 *
 * 本模块只管"菜单长什么样 + 点击命中 + 命令分发"，命令的**效果**由调用方实现
 * （通过 S3AdminCmd 回调），因此它不依赖游戏数据层，可独立复用/测试。
 */
#ifndef SANGO3_ADMIN_MENU_H
#define SANGO3_ADMIN_MENU_H

#include <stdint.h>
#include "render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t *(*S3AdminReadAsset)(void *ud, const char *path, uint32_t *out_len);
typedef void (*S3AdminDrawText)(void *ud, Sango3Canvas *cv, const char *utf8,
                                int32_t x, int32_t y, int32_t w, int32_t h,
                                uint32_t rgb, int font, uint32_t style);

/* 命令回执：group 0..6，item 从 0 起，label = 命令名（UTF-8）。
 * 返回 1 = 已执行（调用方应自行 s3_admin_set_hint 给出结果）；
 * 返回 0 = 尚未实现 → 菜单自动提示「暫未實現」。 */
typedef int (*S3AdminCmd)(void *ud, int group, int item, const char *label);

#define S3_ADMIN_GROUPS    7
#define S3_ADMIN_MAX_ITEMS 5

typedef struct S3AdminMenu S3AdminMenu;

S3AdminMenu *s3_admin_new(S3AdminReadAsset read_asset, void *asset_ud,
                          S3AdminDrawText draw_text, void *text_ud,
                          S3AdminCmd on_cmd, void *cmd_ud);
void s3_admin_free(S3AdminMenu *m);

void s3_admin_set_visible(S3AdminMenu *m, int vis);
int  s3_admin_visible(const S3AdminMenu *m);
/* 收起子选单（+ 复位 hover）。进命令阶段/离开朝堂隐藏菜单时一并调用。 */
void s3_admin_collapse(S3AdminMenu *m);
void s3_admin_set_zoom(S3AdminMenu *m, int32_t zoom);
void s3_admin_set_origin(S3AdminMenu *m, int32_t x, int32_t y);
void s3_admin_set_hint(S3AdminMenu *m, const char *hint);

int         s3_admin_open_group(const S3AdminMenu *m);   /* -1 = 未展开 */
const char *s3_admin_hint(const S3AdminMenu *m);
const char *s3_admin_group_label(int g);
const char *s3_admin_item_label(int group, int item);
int         s3_admin_group_items(int g);

void s3_admin_render(S3AdminMenu *m, Sango3Canvas *cv);

/* 命中：点是否落在菜单范围内（按钮区或展开的子选单）。
 * ⚠ 调用方应先用它决定"这一次点击/拖动归菜单还是归地图"，再分发。 */
int  s3_admin_hit(const S3AdminMenu *m, int32_t x, int32_t y);
/* 悬停更新（每帧调用；不在菜单上时传 -1,-1 即可清空高亮） */
void s3_admin_on_move(S3AdminMenu *m, int32_t x, int32_t y);
/* 点击：返回 1 = 已被菜单消费 */
int  s3_admin_on_click(S3AdminMenu *m, int32_t x, int32_t y);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_ADMIN_MENU_H */
