/*
 * strategy_scene.h —— 战略层地图（M3-lite）
 *
 * 原版做法（逆向确认）：战略地图是**一张预烘焙整图**
 *   Shape\AD\Base\Map.shp（1024×768，含地形/城池图标/道路网），
 *   地图框 Shape\AD\Base\MapFrame.shp（520×345），
 *   70 个城市按钮（MenuMap.ini 的 WND_CLASS_CITYBUTTON）按**地图像素坐标**叠在上面。
 *   blk 文件（Map001~067.blk）是**通行属性逻辑层**，不参与渲染。
 *
 * 本模块：整图缩放到逻辑画布 + 城市标记（我方/选中）+ 点击选中 + 顶部信息条。
 */
#ifndef SANGO3_STRATEGY_SCENE_H
#define SANGO3_STRATEGY_SCENE_H

#include <stdint.h>
#include "render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t *(*S3StratReadAsset)(void *ud, const char *path, uint32_t *out_len);
typedef void (*S3StratDrawText)(void *ud, Sango3Canvas *cv, const char *utf8,
                                int32_t x, int32_t y, int32_t w, int32_t h,
                                uint32_t rgb, int font, uint32_t style);

#define S3_STRAT_MAX_CITIES 96

/* 城池详情 —— 对应原版 Menu.ini 的 7400 面板九行（标签烘焙在素材里，这里只给值）。
 * 字符串内部拷贝，调用方无需保持生命周期。 */
typedef struct {
    char    lord[32];        /* 太守：City0N.ini 的 Lord */
    char    adviser[32];     /* 軍師：空 = 未任命（原版由「任免」指派） */
    int32_t people;          /* 人口 */
    int32_t money;           /* 金錢 */
    int32_t dev;             /* 開發 */
    int32_t reserve;         /* 兵士：City0N.ini 的 ReserveForce */
    int32_t n_generals;      /* 武將：General02.ini 归属统计 */
    int32_t friendliness;    /* 友好：Nation.ini 相对我方君主，0~100 */
    int32_t size;            /* City.ini 的 Size（城规模） */

    /* 执行者（General01.ini 能力 + General02.ini 归属，各取该城最高）。
     * 原版口径：內政效果量看执行者智力，軍事（徵兵）看武力 —— 2026-09-16 用户口述。
     * ⚠ 2026-09-16 复核定稿后，执行者改为**玩家在武将选择界面指定**（roster 模块），
     *   这里保留的"最佳者"仅作朝堂面板/日志的展示用。 */
    char    worker_int[32];  int32_t worker_int_val;   /* 智力最高 */
    char    worker_str[32];  int32_t worker_str_val;   /* 武力最高 */

    /* 士气（定稿 J4/C2）：**城池整体**，上限 100、初值 70、每回合不衰减；
     * 只在 战斗撤退 / 不敢单挑 / 受军师技影响 时下降，战斗胜利上升。 */
    int32_t morale;

    /* 調查有效期（定稿 F1/F2）：**非我方**城池的详情要經「計略 → 調查」才看得见，
     * 有效期 6 个月（含调查当月 → invest_until = 调查月 + 5）。
     * 0 = 从未调查；我方城池不需要调查（调用方把 mine 城直接判为 known）。 */
    int32_t invest_until;
} S3CityDetail;

typedef struct S3Strategy S3Strategy;

S3Strategy *s3_strategy_new(S3StratReadAsset read_asset, void *asset_ud,
                            S3StratDrawText draw_text, void *text_ud);
void        s3_strategy_free(S3Strategy *s);

/* 载入地图整图（pak 路径，如 "Shape\\AD\\Base\\Map.shp"）。返回 0 成功。 */
int  s3_strategy_set_map(S3Strategy *s, const char *pak_path);
void s3_strategy_clear_cities(S3Strategy *s);
/* mine: 1 = 己方城池（金色框）
 * mx,my = 城池图标**中心**的地图像素坐标；mw,mh = 图标尺寸（用于标记与命中） */
void s3_strategy_add_city(S3Strategy *s, const char *name,
                          int32_t mx, int32_t my, int32_t mw, int32_t mh, int mine);

/* 给第 idx 个城市补详情（idx = add_city 的调用顺序）。NULL 或缺项按 0/空处理。 */
void s3_strategy_set_city_detail(S3Strategy *s, int idx, const S3CityDetail *d);

/* 载入城池信息面板素材（原版 Shape\AD\Base\CityInfo.shp，98×164，标签烘焙在图内）。
 * 载入后：选中城池即在视口右上角弹出面板（九行字段）。返回 0 成功。 */
int  s3_strategy_set_panel(S3Strategy *s, const char *pak_path);
/* 面板缩放档（1 = 原版尺寸 / 2 = 高清画布下放大一档，默认 2） */
void s3_strategy_set_panel_zoom(S3Strategy *s, int32_t zoom);
/* 我方君主名（仅用于信息条文案，可空） */
void s3_strategy_set_my_lord(S3Strategy *s, const char *lord);

/* 读回某城详情（未设置过返回 NULL）—— 供日志/自检核对数据层 */
const S3CityDetail *s3_strategy_city_detail(const S3Strategy *s, int idx);
/* 可写版本 —— 行政命令（開發/徵兵/徵稅…）就地改城池数值用；未设置过返回 NULL */
S3CityDetail *s3_strategy_city_detail_mut(S3Strategy *s, int idx);

/* ---------------------- 视图：战略地图 / 朝堂（内政阶段） ----------------------
 * 原版流程：選完勢力先進「朝堂」（640×480 全屏 CG，Shape\AD\Background\BG001~003）
 * 做内政，確定后才回到大地图（2026-09-16 用户指正）。 */
void s3_strategy_set_view(S3Strategy *s, int court);   /* 1 = 朝堂，0 = 大地图 */
int  s3_strategy_court(const S3Strategy *s);
int  s3_strategy_set_court_bg(S3Strategy *s, const char *pak_path);  /* 返回 0 成功 */
/* 朝堂里没有地图可点 → 用这个直接指定"当前城"（如我方主城） */
void s3_strategy_select(S3Strategy *s, int idx);

/* 月份（朝堂信息条显示；「確定」= 结束本月由调用方推进） */
void s3_strategy_set_month(S3Strategy *s, int month);
int  s3_strategy_month(const S3Strategy *s);

/* 信息条临时文案（命令阶段引导等）；置空串恢复默认。两视图共用。 */
void s3_strategy_set_banner(S3Strategy *s, const char *text);

/* 视口：画布尺寸 = 视口尺寸，渲染时 1:1 从整图裁取该区域（零重采样）。
 * 由调用方按屏幕宽高比调用 set_viewport —— 视口比例 = 屏幕比例时，
 * COVER 下恰好铺满且无裁切，同时地图可拖动查看全图。 */
void s3_strategy_set_viewport(S3Strategy *s, int32_t vw, int32_t vh);
/* 拖动（dx,dy 为**手指/鼠标**的移动量；内部取反并 clamp 到地图范围内） */
void s3_strategy_pan_view(S3Strategy *s, int32_t dx, int32_t dy);
int  s3_strategy_view_w(const S3Strategy *s);
int  s3_strategy_view_h(const S3Strategy *s);
int  s3_strategy_view_x(const S3Strategy *s);
int  s3_strategy_view_y(const S3Strategy *s);

/* ---- 朝堂 UI 层拆分（2026-09-23）---------------------------------------------
 * 背景（朝堂 CG → 画布铺满拉伸）与 UI（顶部信息条 + 城池信息面板）分开画：
 *   · s3_strategy_set_hud_enabled(st, 0) → 主画布只出背景（UI 另画）；
 *   · s3_strategy_draw_court_ui(st, cv) → 把信息条 + 城池面板画到**指定的 UI 画布**上。
 * 目的：朝堂画布做成"与屏幕同比例"后背景照旧铺满，而 UI 画在 640×480 层里 1:1 贴回 → UI 不变形。 */
void s3_strategy_set_hud_enabled(S3Strategy *s, int on);
void s3_strategy_draw_court_ui(S3Strategy *s, Sango3Canvas *cv);

void s3_strategy_render(S3Strategy *s, Sango3Canvas *cv);
/* 逻辑坐标点击：命中某城则选中它（命中范围按地图缩放后的城市图标大小放宽） */
void s3_strategy_on_click(S3Strategy *s, int32_t lx, int32_t ly);
/* 拖动地图（长按拖动时调用，dx/dy 为逻辑像素增量） */
void s3_strategy_pan(S3Strategy *s, int32_t dx, int32_t dy);

int  s3_strategy_selected(const S3Strategy *s);      /* 下标，-1 = 无 */
int  s3_strategy_count(const S3Strategy *s);
const char *s3_strategy_city_name(const S3Strategy *s, int idx);
int  s3_strategy_city_mine(const S3Strategy *s, int idx);

/* -------- I1 势力插旗：旗号取自 Nation.ini 的 Flag（1~35，0 = 无主） -------- */
void s3_strategy_set_city_flag(S3Strategy *s, int idx, int flag);
int  s3_strategy_city_flag(const S3Strategy *s, int idx);

/* -------- I2 选君主（大地图版）：换君主 → 重算归属高亮 -------- */
/* 返回"我方"城池数；同时把 lord 记进 my_lord（信息条文案）。 */
int  s3_strategy_remark_owner(S3Strategy *s, const char *lord);
int  s3_strategy_first_city_of(const S3Strategy *s, const char *lord);

/* -------- 調查 / 情報（定稿 F1/F2） --------
 * known = 我方城池（永远可见）或"非我方但仍在調查有效期内"。
 * 非 known 的城池只露 城市/太守，其余数值一律不显示（否则調查没有意义）。 */
int  s3_strategy_city_known(const S3Strategy *s, int idx);
/* 记一次调查：有效期至 until_month（含）。返回 0 成功。 */
int  s3_strategy_city_investigate(S3Strategy *s, int idx, int until_month);
/* 非我方的已调查城 → 有效至该月；未调查/我方 → 0 */
int  s3_strategy_city_invest_until(const S3Strategy *s, int idx);

/* -------- D 区外交（2026-09-28）：势力友好度 / 同盟 / 威望 / 高亮 --------
 * 原版 `Nation.ini` 的 `Friendship` 是**势力对势力**的（0~70）；我方不列入 → 固定 70。
 * 城池面板的「友好」行显示 = 该城太守所属势力在本表里的值（改表后调 apply_friend 同步）。 */
void s3_strategy_set_friend(S3Strategy *s, const char *lord, int value);
int  s3_strategy_friend(const S3Strategy *s, const char *lord);
int  s3_strategy_apply_friend(S3Strategy *s, const char *lord);   /* 返回同步的城数 */
void s3_strategy_set_ally(S3Strategy *s, const char *lord, int on);
int  s3_strategy_is_ally(const S3Strategy *s, const char *lord);
/* 威望 0~100（L4；仅玩家势力维护，AI 视为 50）—— 显示在顶部信息条 */
void s3_strategy_set_prestige(S3Strategy *s, int v);
int  s3_strategy_prestige(const S3Strategy *s);
/* 外交"选国家"的**临时高亮**（青色框，**不改归属**）；传 NULL/"" 取消 */
void s3_strategy_set_highlight_lord(S3Strategy *s, const char *lord);
const char *s3_strategy_highlight_lord(const S3Strategy *s);

#ifdef __cplusplus
}
#endif

#endif /* SANGO3_STRATEGY_SCENE_H */
