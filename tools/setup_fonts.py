# -*- coding: utf-8 -*-
"""把下载好的开源字体部署到 engine/assets/fonts/，并生成字体说明与许可证。

字体选型（两套并置，因为用途不同）：
  · ArkPixel12-zh_tw.ttf   像素字体 —— "原版像素"显示模式
      12px 点阵，在 2K(=×3 整数倍) 放大后仍是干净锐利的像素块，
      观感最接近原版 14/16/20px 点阵字。选 zh_tw 是因为【游戏数据是 Big5 繁体】。
  · LXGWWenKai-Regular.ttf 矢量楷体 —— "高清"显示模式（默认）
      任意分辨率下清晰，楷体气质契合三国题材；2K/4K 不糊。

两者均为 SIL Open Font License 1.1，允许自由使用、修改、嵌入与再分发
（含随商业软件分发），唯一限制是不得单独出售字体本体、衍生字体须沿用 OFL。
"""
import os
import shutil
import sys
import urllib.request

sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DL = os.path.join(ROOT, ".workbuddy", "downloads", "fonts")
OUT = os.path.join(ROOT, "engine", "assets", "fonts")
os.makedirs(OUT, exist_ok=True)

PLAN = [
    (os.path.join(DL, "ark", "ark-pixel-12px-proportional-zh_tw.ttf"),
     "ArkPixel12-zh_tw.ttf"),
    (os.path.join(DL, "LXGWWenKai-Regular.ttf"),
     "LXGWWenKai-Regular.ttf"),
]

print("=" * 74)
print("1) 部署字体文件")
print("=" * 74)
ok = []
for src, dst in PLAN:
    if not os.path.isfile(src):
        print(f"  ✗ 缺少源文件 {src}")
        continue
    d = os.path.join(OUT, dst)
    shutil.copy2(src, d)
    ok.append((dst, os.path.getsize(d)))
    print(f"  ✓ {dst:<28} {os.path.getsize(d):>12,} 字节")

# 许可证
print()
print("=" * 74)
print("2) 许可证")
print("=" * 74)

ark_of = os.path.join(DL, "ark", "OFL.txt")
if os.path.isfile(ark_of):
    shutil.copy2(ark_of, os.path.join(OUT, "OFL-ArkPixel.txt"))
    print(f"  ✓ OFL-ArkPixel.txt      {os.path.getsize(ark_of):,} 字节（取自字体包内）")
else:
    print("  ✗ 未找到 Ark Pixel 的 OFL.txt")

# LXGW 许可证：仓库里文件名可能是 OFL.txt 或 SIL_Open_Font_License_1.1.txt
LIC_CANDS = [
    "https://raw.githubusercontent.com/lxgw/LxgwWenKai/main/OFL.txt",
    "https://raw.githubusercontent.com/lxgw/LxgwWenKai/main/SIL_Open_Font_License_1.1.txt",
    "https://raw.githubusercontent.com/lxgw/LxgwWenKai/master/OFL.txt",
    "https://github.com/lxgw/LxgwWenKai/raw/main/OFL.txt",
]
lic = None
for u in LIC_CANDS:
    try:
        req = urllib.request.Request(u, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=40) as r:
            lic = r.read()
        print(f"  ✓ LXGW 许可证来自 {u}")
        break
    except Exception as e:  # noqa: BLE001
        print(f"    ✗ {u}  {type(e).__name__}")
if lic:
    p = os.path.join(OUT, "OFL-LXGWWenKai.txt")
    with open(p, "wb") as f:
        f.write(lic)
    print(f"  ✓ OFL-LXGWWenKai.txt    {len(lic):,} 字节")
else:
    print("  ! 未能自动获取，将写入占位说明（后续手工补）")
    with open(os.path.join(OUT, "OFL-LXGWWenKai.txt"), "w", encoding="utf-8") as f:
        f.write("LXGW WenKai 使用 SIL Open Font License 1.1（OFL-1.1）。\n"
                "许可证全文见 https://github.com/lxgw/LxgwWenKai/blob/main/OFL.txt\n")

# 说明文件
print()
print("=" * 74)
print("3) 写入字体说明")
print("=" * 74)
README = """# 内置字体（engine/assets/fonts）

本项目内置两套开源中文字体，**均为 SIL Open Font License 1.1（OFL-1.1）**，
可自由使用、修改、嵌入、随软件分发（含商业分发）。
限制：不得单独出售字体本体；基于本字体制作的衍生字体须继续使用 OFL 授权。

## 文件清单

| 文件 | 用途 | 许可 |
|---|---|---|
| `ArkPixel12-zh_tw.ttf` | 「原版像素」显示模式的点阵字体 | OFL-1.1（见 `OFL-ArkPixel.txt`） |
| `LXGWWenKai-Regular.ttf` | 「高清」显示模式（默认）的矢量字体 | OFL-1.1（见 `OFL-LXGWWenKai.txt`） |

### ArkPixel12-zh_tw
- 来源：方舟像素字体 Ark Pixel Font 12px proportional
  https://github.com/TakWolf/ark-pixel-font  （版本 2026.09.01）
- 为何选 12px：原始游戏中文为 14/16/20px 点阵位图。12px 点阵在 **整数倍**
  放大（1080p ×2、2K ×3、4K ×4）时仍是干净的像素方块，观感最接近原版；
  非整数倍放大则会因采样不均而出现断笔，故该模式建议配合整数倍缩放使用。
- 为何选 zh_tw：**游戏数据表是 Big5 繁体**（General01.ini / Menu.ini 等），
  zh_tw 子字体覆盖繁体用字。

### LXGWWenKai-Regular
- 来源：霞鹜文楷 LXGW WenKai v1.522
  https://github.com/lxgw/LxgwWenKai
- 矢量轮廓，任意物理尺寸下清晰，是 2K/4K 下的默认字体；
  楷体气质也与三国题材相称。

## 两种显示模式

引擎按「逻辑 640×480 → 物理分辨率」的视口变换渲染。文字不参与位图放大，
而是**按目标物理尺寸直接栅格化**（见 `docs/分辨率与高清化架构.md`）：

- **高清模式（默认）**：用 LXGWWenKai，按物理像素尺寸渲染字形。
  例：逻辑 14px 字号在 2K(×3) 下按 42px 栅格化，边缘依旧干净。
- **原版像素模式**：用 ArkPixel12，仅在整数倍缩放下启用，保留复古观感。

## 再生成方式

字体文件已随仓库分发，正常无需重新获取。若需更新版本或在新机器上重建：

```
python tools/fetch_fonts.py     # 从 GitHub 下载（失败自动走 gh-proxy 镜像）
python tools/setup_fonts.py     # 部署到本目录并写入许可证
```

## 体积优化方向（后续可选）

`LXGWWenKai-Regular.ttf` 约 25 MB，全量字符集。若需压缩 Android 安装包体积，
可基于游戏实际用字（Text.ini / Dialogue.ini / EventMsg.ini / Menu.ini 等）
做字体子集化（fonttools subset）。**但子集化有缺字风险**（未收录的字符会显示方框），
需在文本全部提取完成后、并保留完整字体作为兜底，才建议启用。
"""
with open(os.path.join(OUT, "README.md"), "w", encoding="utf-8") as f:
    f.write(README)
print("  ✓ README.md")

print()
print("=" * 74)
print("4) 最终目录")
print("=" * 74)
total = 0
for f in sorted(os.listdir(OUT)):
    p = os.path.join(OUT, f)
    total += os.path.getsize(p)
    print(f"  {os.path.getsize(p):>12,}  {f}")
print(f"  {'-'*12}")
print(f"  {total:>12,}  合计（约 {total/1048576:.1f} MB）")
