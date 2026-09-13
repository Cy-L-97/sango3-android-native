# -*- coding: utf-8 -*-
"""把下载好的开源字体部署到 engine/assets/fonts/，并生成字体说明与许可证。

================================ 选型说明 ================================
用【游戏自身全部文本】实测覆盖率后确定的组合（详见 tools/check_font_coverage.py）：

| 字体 | 内码位 | 对游戏 1970 字的覆盖 | 结论 |
|---|---|---|---|
| Ark Pixel 12px        | 24,471 | 92.10%（缺 158） | ✗ 弃用，缺的是高频字 |
| Ark Pixel 16px        | 3,252  | 3.10%            | ✗ 该档位覆盖不全 |
| **Fusion Pixel 12px** | 36,558 | **99.85%（缺 3）** | ✅ 采用（像素模式） |
| **LXGW WenKai**       | 46,490 | **100%**          | ✅ 采用（高清模式，默认） |

两套并置，用途不同：
  · LXGWWenKai-Regular.ttf   矢量楷体 —— 「高清」显示模式（默认）
      任意分辨率清晰；2K/4K 不糊；覆盖 100%。
  · FusionPixel12-zh_hant.ttf 像素字体 —— 「原版像素」显示模式
      12px 点阵，整数倍(×2/×3/×4)放大后仍是干净像素块，观感最接近原版点阵字。
      选 zh_hant（繁体）因游戏数据是 Big5 繁体。

⚠ 像素模式必须配置【字体回退链】：Fusion Pixel 12px 缺 3 个字 —— **繇 / 鋻 / 鎩**，
  它们都在真实文本里出现（劉繇=武将名、鎩=武器名、"實鋻此心"=桃园结义剧情）。
  引擎用 `TTF_AddFallbackFont(pixelFont, wenkaiFont)` 让缺字自动落到霞鹜文楷，
  不会出现方框。

两者均为 SIL Open Font License 1.1，允许自由使用、修改、嵌入与再分发
（含随商业软件分发），唯一限制是不得单独出售字体本体、衍生字体须沿用 OFL。
"""
import os
import shutil
import sys

sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DL = os.path.join(ROOT, ".workbuddy", "downloads", "fonts")
OUT = os.path.join(ROOT, "engine", "assets", "fonts")
os.makedirs(OUT, exist_ok=True)

PLAN = [
    (os.path.join(DL, "FusionPixel12", "fusion-pixel-12px-proportional-zh_hant.ttf"),
     "FusionPixel12-zh_hant.ttf"),
    (os.path.join(DL, "LXGWWenKai-Regular.ttf"), "LXGWWenKai-Regular.ttf"),
]

# 已弃用的字体（覆盖率不足），部署时顺手清理
OBSOLETE = ["ArkPixel12-zh_tw.ttf", "OFL-ArkPixel.txt"]

print("=" * 74)
print("1) 部署字体文件")
print("=" * 74)
deployed = []
for src, dst in PLAN:
    if not os.path.isfile(src):
        print(f"  ✗ 缺少源文件 {src}")
        continue
    d = os.path.join(OUT, dst)
    shutil.copy2(src, d)
    deployed.append((dst, os.path.getsize(d)))
    print(f"  ✓ {dst:<30} {os.path.getsize(d):>12,} 字节")

print()
print("=" * 74)
print("2) 清理已弃用字体")
print("=" * 74)
for f in OBSOLETE:
    p = os.path.join(OUT, f)
    if os.path.isfile(p):
        os.remove(p)
        print(f"  - 已删除 {f}")
    else:
        print(f"  · {f} 不存在（无需处理）")

print()
print("=" * 74)
print("3) 许可证")
print("=" * 74)
for src_dir, dst in [(os.path.join(DL, "FusionPixel12"), "OFL-FusionPixel.txt"),
                     (os.path.join(DL, "ark"), "OFL-FusionPixel.txt")]:
    s = os.path.join(src_dir, "OFL.txt")
    if os.path.isfile(s):
        shutil.copy2(s, os.path.join(OUT, dst))
        print(f"  ✓ {dst:<24} {os.path.getsize(s):,} 字节（取自已下载字体包）")
        break
else:
    print("  ✗ 未找到 OFL.txt")

lx = os.path.join(OUT, "OFL-LXGWWenKai.txt")
if os.path.isfile(lx):
    print(f"  ✓ OFL-LXGWWenKai.txt          {os.path.getsize(lx):,} 字节（已存在）")

print()
print("=" * 74)
print("4) 写入字体说明")
print("=" * 74)
README = """# 内置字体（engine/assets/fonts）

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
"""
with open(os.path.join(OUT, "README.md"), "w", encoding="utf-8") as f:
    f.write(README)
print("  ✓ README.md")

print()
print("=" * 74)
print("5) 最终目录")
print("=" * 74)
total = 0
for f in sorted(os.listdir(OUT)):
    p = os.path.join(OUT, f)
    if os.path.isfile(p):
        total += os.path.getsize(p)
        print(f"  {os.path.getsize(p):>12,}  {f}")
print(f"  {'-'*12}")
print(f"  {total:>12,}  合计（约 {total/1048576:.1f} MB）")
