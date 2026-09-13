# 内置字体（engine/assets/fonts）

本项目内置开源中文字体，**均为 SIL Open Font License 1.1（OFL-1.1）**，
可自由使用、修改、嵌入、随软件分发（含商业分发）。
限制：不得单独出售字体本体；基于本字体制作的衍生字体须继续使用 OFL 授权。

## 显示语言策略（默认简中）

游戏原版数据是 **Big5 繁体**。按用户口径「不必完全复刻原版，优先支持简中显示」，
本项目**默认以简体中文渲染**，繁体保留为可选模式：

| 模式 | 文本处理 | 字体 |
|---|---|---|
| **简中（默认）** | 运行时查「繁→简字符映射表」转换后渲染 | `FusionPixel12-zh_hans.ttf` / `LXGWWenKai-Regular.ttf` |
| 繁中（可选） | 原样输出 Big5 文本 | `FusionPixel12-zh_hant.ttf` / `LXGWWenKai-Regular.ttf` |

映射表由 `tools/build_lang_table.py` **离线生成**（OpenCC t2s，基于游戏自身全部文本）：
1838 个繁体字发生转换，**无双射冲突**（每个繁体字唯一对应一个简体字）。
引擎侧只查表，无运行时依赖。

## 文件清单

| 文件 | 用途 | 许可 |
|---|---|---|
| `FusionPixel12-zh_hans.ttf` | 像素模式 · **简中（默认）** | OFL-1.1（`OFL-FusionPixel.txt`） |
| `FusionPixel12-zh_hant.ttf` | 像素模式 · 繁中（可选） | OFL-1.1（`OFL-FusionPixel.txt`） |
| `LXGWWenKai-Regular.ttf` | **高清模式（默认）**，繁简通吃 | OFL-1.1（`OFL-LXGWWenKai.txt`） |

## 选型依据：对【游戏实际文本】的覆盖率实测

字符集来自游戏自身文本（`tools/build_lang_table.py` 产出，`text_charset.json`）：
繁体集 7105 字、简体集 6774 字。逐个检查字体 cmap 覆盖（`tools/check_font_coverage.py`）：

| 字体 | 用途 | 内码位 | 繁缺 | 简缺 | 简中覆盖率 | 判定 |
|---|---|---|---|---|---|---|
| `FusionPixel12-zh_hant` | 像素·繁中 | 36,558 | 462 | 453 | 93.31% | ⚠ 配回退链 |
| `LXGWWenKai` | 矢量·楷体 | 46,490 | 0 | 0 | 100.00% | ✅ 可用 |
| `ArkPixel12-zh_cn` | 像素·参考 | 24,471 | 866 | 693 | 89.77% | ✗ 弃用 |
| `ArkPixel12-zh_tw` | 像素·参考 | 24,471 | 866 | 693 | 89.77% | ✗ 弃用 |

> 字符集：繁体 7105 字 / 简体 6774 字（来源：text_charset.json（由 build_lang_table.py 生成））

> 结论：**矢量字体承担默认渲染，像素字体作为可选复古模式，且必须配回退链。**
> 「覆盖率必须用真实文本量化」这条教训来自 Ark Pixel 12px —— 它对小样本看似
> 99%，但对完整语料缺的是「杀 / 旋 / 拖」这类**高频字**，实际不可用。

## 两种显示模式

引擎按「逻辑 640×480 → 物理分辨率」做视口变换。文字不参与位图放大，
而是按目标物理尺寸栅格化（详见 `docs/分辨率与高清化架构.md` 第六节）：

- **高清模式（默认）**：`LXGWWenKai`，按物理像素尺寸渲染字形。覆盖率 100%。
  例：逻辑 14px 字号在 2K（×3）下按 42px 栅格化，边缘依旧干净。
- **像素模式（可选）**：`FusionPixel12`，仅在整数倍缩放下启用，保留复古观感。
  12px 在 ×2/×3/×4 下都是干净的像素块；非整数倍会因采样不均出现断笔，
  该模式下应自动提示或降级为高清模式。

### ⚠ 像素模式必须配置字体回退链

像素字体对完整语料存在缺字（缺字清单见 `.workbuddy/data/font_coverage.json`），
因此像素模式开启时必须调用 SDL_ttf 的回退机制：

```c
TTF_Font *pixel    = TTF_OpenFont("engine/assets/fonts/FusionPixel12-zh_hans.ttf", 12);
TTF_Font *fallback = TTF_OpenFont("engine/assets/fonts/LXGWWenKai-Regular.ttf", 12);
TTF_AddFallbackFont(pixel, fallback);   /* 缺字自动落到楷体，不出现方框 */
```

这样绝大多数文本是纯正像素字形，仅少量生僻字换成楷体，**任何文本都不会显示方框**。
高清模式覆盖率 100%，无需回退。

## 再生成方式

字体文件已随仓库分发，正常无需重新获取。若需更新版本或在新机器上重建：

```
python tools/fetch_fonts.py          # 下载霞鹜文楷 + 像素字体候选
python tools/fetch_pixel_alt.py      # 下载像素字体候选（Fusion / Ark Pixel 16）
python tools/build_lang_table.py     # 生成繁→简映射表 + 繁简字符集
python tools/check_font_coverage.py  # 量化覆盖率（换字体后务必重跑）
python tools/setup_fonts.py          # 部署到本目录并写入许可证
python tools/render_font_preview.py  # 生成渲染验收图
```

## 体积优化方向（后续可选）

三套字体合计约 39 MB，均为全量字符集。若需压缩 Android 安装包体积，
可在全部文本提取完成后做子集化（fonttools subset，只保留游戏用到的字）；
本项目文本已全部可提取，子集化后预计可压到 1 MB 以内。
**但务必保留完整字体作为回退兜底**，避免出现未收录字符。
