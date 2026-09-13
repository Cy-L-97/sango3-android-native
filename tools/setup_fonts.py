# -*- coding: utf-8 -*-
"""把下载好的开源字体部署到 engine/assets/fonts/，并生成字体说明与许可证。

================================ 显示语言策略 ================================
用户口径（2026-09-13）：「不需要完全复刻原版，最好是能支持简中显示。」

因此本项目**默认简中显示**：游戏数据是 Big5 繁体，运行时经「繁→简映射表」
（tools/build_lang_table.py 离线生成）转换后渲染。繁体保留为可选模式。

================================ 选型说明 ================================
候选字体的覆盖率必须用【游戏真实文本】量化（详见 tools/check_font_coverage.py）：

| 字体 | 简中集覆盖 | 结论 |
|---|---|---|
| Ark Pixel 12px (zh_cn/tw) | 89.8%（缺「杀旋拖」等高频字） | ✗ 不可用 |
| Fusion Pixel 12px (zh_hans) | 见量化报告 | 像素模式候选 |
| Fusion Pixel 12px (zh_hant) | 见量化报告 | 繁中模式候选 |
| **LXGW WenKai** | **100%** | ✅ 高清模式默认 |

⚠ 像素字体必须配置【字体回退链】：缺字自动落到霞鹜文楷，绝不显示方框。
  引擎：`TTF_AddFallbackFont(pixelFont, wenkaiFont)`

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
    (os.path.join(DL, "FusionPixel12", "fusion-pixel-12px-proportional-zh_hans.ttf"),
     "FusionPixel12-zh_hans.ttf",
     "「原版像素」显示模式 · 简中（默认）"),
    (os.path.join(DL, "FusionPixel12", "fusion-pixel-12px-proportional-zh_hant.ttf"),
     "FusionPixel12-zh_hant.ttf",
     "「原版像素」显示模式 · 繁中（可选）"),
    (os.path.join(DL, "LXGWWenKai-Regular.ttf"),
     "LXGWWenKai-Regular.ttf",
     "「高清」显示模式（默认）· 繁简通吃，缺字兜底"),
]

# 已弃用的字体（覆盖率不足），部署时顺手清理
OBSOLETE = ["ArkPixel12-zh_tw.ttf", "OFL-ArkPixel.txt"]

print("=" * 74)
print("1) 部署字体文件")
print("=" * 74)
deployed = []
for src, dst, use in PLAN:
    if not os.path.isfile(src):
        print(f"  ✗ 缺少源文件 {os.path.basename(src)}")
        continue
    d = os.path.join(OUT, dst)
    shutil.copy2(src, d)
    sz = os.path.getsize(d)
    deployed.append((dst, sz, use))
    print(f"  ✓ {dst:<30} {sz:>12,} 字节   {use}")

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
lic = os.path.join(DL, "FusionPixel12", "OFL.txt")
if os.path.isfile(lic):
    shutil.copy2(lic, os.path.join(OUT, "OFL-FusionPixel.txt"))
    print(f"  ✓ OFL-FusionPixel.txt         {os.path.getsize(lic):,} 字节")
else:
    print("  ✗ 未找到 FusionPixel 的 OFL.txt")
lx = os.path.join(OUT, "OFL-LXGWWenKai.txt")
if os.path.isfile(lx):
    print(f"  ✓ OFL-LXGWWenKai.txt          {os.path.getsize(lx):,} 字节（已存在）")
else:
    print("  ✗ OFL-LXGWWenKai.txt 缺失")

print()
print("=" * 74)
print("4) 写入字体说明")
print("=" * 74)
README = """# 内置字体（engine/assets/fonts）

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

<!--COVERAGE_TABLE-->

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
"""

# 把最新量化结果写进 README
cov_path = os.path.join(ROOT, ".workbuddy", "data", "font_coverage.json")
if os.path.isfile(cov_path):
    import json
    cov = json.load(open(cov_path, encoding="utf-8"))
    rows = ["| 字体 | 用途 | 内码位 | 繁缺 | 简缺 | 简中覆盖率 | 判定 |",
            "|---|---|---|---|---|---|---|"]
    for name, v in cov.get("fonts", {}).items():
        c = v.get("coverage_simplified", 0)
        verdict = "✅ 可用" if c >= 99.5 else ("⚠ 配回退链" if c >= 90 else "✗ 弃用")
        rows.append(f"| `{name}` | {v.get('use','')} | {v.get('codepoints',0):,} | "
                    f"{v.get('missing_traditional',0)} | {v.get('missing_simplified',0)} | "
                    f"{c:.2f}% | {verdict} |")
    rows.append("")
    rows.append(f"> 字符集：繁体 {cov.get('traditional_count',0)} 字 / "
                f"简体 {cov.get('simplified_count',0)} 字"
                f"（来源：{cov.get('charset_source','')}）")
    README = README.replace("<!--COVERAGE_TABLE-->", "\n".join(rows))
else:
    README = README.replace("<!--COVERAGE_TABLE-->",
                            "（尚未生成量化报告，请先运行 tools/check_font_coverage.py）")

with open(os.path.join(OUT, "README.md"), "w", encoding="utf-8") as f:
    f.write(README)
print("  ✓ README.md（含最新覆盖率表）")

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
