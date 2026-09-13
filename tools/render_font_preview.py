# -*- coding: utf-8 -*-
"""用内置字体渲染对比图，验收「高清模式 vs 像素模式」的实际观感，
并验证像素模式的【字体回退链】是否真的消除了方框。

模拟引擎的真实行为（见 docs/分辨率与高清化架构.md 第六节）：
  · 高清模式 (native)  ：矢量字体【按目标物理尺寸直接栅格化】
                        逻辑 14px 在 2K(×3) 下 → 直接以 42px 渲染
  · 像素模式 (pixel)   ：12px 点阵【整数倍放大】
                        12px 渲染后用 NEAREST ×3 → 36px 像素块，边缘保持锐利
                        缺字（繇/鋻/鎩）自动落到霞鹜文楷 —— 对应 SDL_ttf 的
                        TTF_AddFallbackFont()
"""
import os
import sys

sys.stdout.reconfigure(encoding="utf-8")
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
FONTS = os.path.join(ROOT, "engine", "assets", "fonts")
OUT = os.path.join(ROOT, ".workbuddy", "render")
os.makedirs(OUT, exist_ok=True)

HIGH = os.path.join(FONTS, "LXGWWenKai-Regular.ttf")
PIXEL = os.path.join(FONTS, "FusionPixel12-zh_hant.ttf")

for p in (HIGH, PIXEL):
    print(f"{'OK ' if os.path.isfile(p) else 'MISSING '} {os.path.basename(p)}  "
          f"{os.path.getsize(p) if os.path.isfile(p) else 0:,} 字节")

# 像素字体的 cmap —— 用来判断哪些字需要回退
_f = TTFont(PIXEL, fontNumber=0, lazy=True)
PIXEL_CMAP = set()
for t in _f["cmap"].tables:
    PIXEL_CMAP |= set(t.cmap.keys())
_f.close()
print(f"像素字体内码位 {len(PIXEL_CMAP):,}")

W, H = 1600, 1080
BG = (247, 245, 240)
INK = (44, 44, 42)
MUTE = (110, 108, 103)
ACCENT = (24, 95, 165)
AMBER = (133, 79, 11)

img = Image.new("RGB", (W, H), BG)
dr = ImageDraw.Draw(img)

f_title = ImageFont.truetype(HIGH, 40)
f_sub = ImageFont.truetype(HIGH, 24)
f_note = ImageFont.truetype(HIGH, 18)

dr.text((70, 50), "内置字体渲染验收 · 三国群英传3", font=f_title, fill=INK)
dr.text((70, 105), "两种显示模式在 2K（×3 缩放）下的实际观感，覆盖率为对游戏全部文本的实测", font=f_sub, fill=MUTE)
dr.line((70, 152, W - 70, 152), fill=(210, 205, 195), width=2)


def pixel_line(draw_img, y, text, box_w=1100, box_h=60, scale=3):
    """像素模式渲染：12px 绘制 → NEAREST 放大（缺字回退楷体）。"""
    tmp = Image.new("RGB", (box_w, box_h), BG)
    td = ImageDraw.Draw(tmp)
    f_pix = ImageFont.truetype(PIXEL, 12)
    f_fb = ImageFont.truetype(HIGH, 13)
    x = 2
    for ch in text:
        f = f_pix if ord(ch) in PIXEL_CMAP else f_fb
        td.text((x, 2), ch, font=f, fill=INK)
        x += td.textlength(ch, font=f)
    big = tmp.resize((box_w * scale, box_h * scale), Image.NEAREST)
    draw_img.paste(big.crop((0, 0, 1560, box_h * scale)), (70, y))


# ---------------- 高清模式 ----------------
y = 190
dr.rectangle((70, y, 84, y + 26), fill=ACCENT)
dr.text((100, y - 4), "高清模式（默认） · LXGWWenKai-Regular.ttf（霞鹜文楷，矢量）· 覆盖率 100%",
        font=f_sub, fill=INK)
y += 46
dr.text((70, y), "做法：矢量按物理尺寸直接栅格化（逻辑 14px × 3 = 42px 渲染）", font=f_note, fill=MUTE)
y += 40

dr.text((70, y), "三國群英傳三 · 必殺技", font=ImageFont.truetype(HIGH, 42), fill=INK)
y += 62
dr.text((70, y), "武力 81  士兵 1000  等級 50", font=ImageFont.truetype(HIGH, 42), fill=(60, 58, 55))
y += 60
dr.text((70, y), "小字號亦清晰：軍師 智 96 · 城防 12,000 · 武將劉繇", font=ImageFont.truetype(HIGH, 30),
        fill=(85, 82, 78))
y += 70

dr.line((70, y, W - 70, y), fill=(220, 215, 205), width=2)
y += 30

# ---------------- 像素模式 ----------------
dr.rectangle((70, y, 84, y + 26), fill=AMBER)
dr.text((100, y - 4), "像素模式 · FusionPixel12-zh_hant.ttf（缝合像素字体，OFL）· 覆盖率 99.85%",
        font=f_sub, fill=INK)
y += 46
dr.text((70, y), "做法：12px 点阵整数倍放大 ×3 → 36px，边缘保持锐利", font=f_note, fill=MUTE)
y += 38

pixel_line(img, y, "三國群英傳三 · 必殺技")
y += 62
pixel_line(img, y, "武力 81  士兵 1000  等級 50")
y += 62

dr.text((70, y), "回退验证：以下两字为像素字体缺失字（繇、鎩），自动落到霞鹜文楷，不出方框",
        font=f_note, fill=AMBER)
y += 34
pixel_line(img, y, "劉繇持鎩上陣")
y += 70

dr.line((70, y, W - 70, y), fill=(220, 215, 205), width=2)
y += 26

dr.text((70, y), "两套字体均为 SIL OFL-1.1，可自由使用、修改、随软件分发（含商业分发）。", font=f_note, fill=MUTE)
dr.text((70, y + 28), "唯一限制：不得单独出售字体本体；衍生字体须沿用 OFL。许可证全文见 engine/assets/fonts/。",
        font=f_note, fill=MUTE)

out = os.path.join(OUT, "font_preview.png")
img.save(out)
print(f"\n已写出 {out}  ({os.path.getsize(out):,} 字节, {W}x{H})")
