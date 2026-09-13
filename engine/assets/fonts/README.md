# 内置字体（engine/assets/fonts）

本项目内置两套开源中文字体，**均为 SIL Open Font License 1.1（OFL-1.1）**，
可自由使用、修改、嵌入、随软件分发（含商业分发）。
限制：不得单独出售字体本体；基于本字体制作的衍生字体须继续使用 OFL 授权。

## 文件清单

| 文件 | 用途 | 许可 |
|---|---|---|
| `LXGWWenKai-Regular.ttf` | 「高清」显示模式（默认）的矢量字体 | OFL-1.1（`OFL-LXGWWenKai.txt`） |
| `FusionPixel12-zh_hant.ttf` | 「原版像素」显示模式的点阵字体 | OFL-1.1（`OFL-FusionPixel.txt`） |

## 选型依据：对【游戏实际文本】的覆盖率实测

用游戏自身的全部可见文本（16 个 INI 数据表 + 文本表）提取出 **1970 个不同汉字**，
逐个检查字体 cmap 覆盖情况（脚本：`tools/check_font_coverage.py`）：

| 候选字体 | 内码位 | 缺失 | 覆盖率 | 结论 |
|---|---|---|---|---|
| Ark Pixel 12px | 24,471 | 158 | 92.10% | ✗ 弃用 |
| Ark Pixel 16px | 3,252 | 1909 | 3.10% | ✗ 弃用 |
| **Fusion Pixel 12px** | 36,558 | **3** | **99.85%** | ✅ **采用** |
| **LXGW WenKai** | 46,490 | **0** | **100%** | ✅ **采用** |

> Ark Pixel 12px 被弃用的原因值得记下：它的缺失字**不是生僻字**，而是
> 孫 / 殺 / 擊 / 旋 / 拖 / 換 / 施 / 應 / 懷 / 錢 这类高频字 —— 意味着
> 「孫權」「必殺技」都会显示方框，实际不可用。**覆盖率必须用真实文本量化，不能凭感觉选。**

## 两种显示模式

引擎按「逻辑 640×480 → 物理分辨率」做视口变换。文字不参与位图放大，
而是按目标物理尺寸栅格化（详见 `docs/分辨率与高清化架构.md` 第六节）：

- **高清模式（默认）**：`LXGWWenKai`，按物理像素尺寸渲染字形。
  例：逻辑 14px 字号在 2K（×3）下按 42px 栅格化，边缘依旧干净。
- **原版像素模式**：`FusionPixel12`，仅在整数倍缩放下启用，保留复古观感。
  12px 在 ×2/×3/×4 下都是干净的像素块；非整数倍会因采样不均出现断笔，
  该模式下应自动提示或降级为高清模式。

### ⚠ 像素模式必须配置字体回退链

`FusionPixel12` 缺 **3 个字**：**繇 / 鋻 / 鎩**。它们都在真实文本中出现：

| 字 | 出现位置 |
|---|---|
| 繇 | 武将名 **劉繇**（EventCond.ini / EventMsg.ini 孙策剧情） |
| 鋻 | 桃园结义剧情文本「實**鋻**此心」 |
| 鎩 | 武器名（Thing.ini，武力 +4） |

因此像素模式开启时必须调用 SDL_ttf 的回退机制：

```c
TTF_Font *pixel    = TTF_OpenFont("engine/assets/fonts/FusionPixel12-zh_hant.ttf", 12);
TTF_Font *fallback = TTF_OpenFont("engine/assets/fonts/LXGWWenKai-Regular.ttf", 12);
TTF_AddFallbackFont(pixel, fallback);   /* 缺字自动落到楷体，不出现方框 */
```

这样 1967/1970 个字是纯正像素字形，仅 3 个生僻字换成楷体，
**任何文本都不会显示方框**。高清模式覆盖 100%，无需回退。

## 再生成方式

字体文件已随仓库分发，正常无需重新获取。若需更新版本或在新机器上重建：

```
python tools/fetch_fonts.py        # 下载霞鹜文楷 + 像素字体候选
python tools/fetch_pixel_alt.py    # 下载像素字体候选（Fusion / Ark Pixel 16）
python tools/check_font_coverage.py  # 量化覆盖率（换字体后务必重跑）
python tools/setup_fonts.py        # 部署到本目录并写入许可证
python tools/render_font_preview.py  # 生成渲染验收图
```

## 体积优化方向（后续可选）

两套字体合计约 32 MB，均为全量字符集。若需压缩 Android 安装包体积，
可在全部文本提取完成后做子集化（fonttools subset，只保留游戏用到的字）。
本项目文本已全部可提取（16 个 INI），子集化后预计可压到 1 MB 以内；
**但务必保留完整字体作为回退兜底**，避免出现未收录字符。
