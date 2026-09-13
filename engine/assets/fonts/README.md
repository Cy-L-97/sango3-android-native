# 内置字体（engine/assets/fonts）

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
