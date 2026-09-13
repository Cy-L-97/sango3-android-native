# -*- coding: utf-8 -*-
"""渲染字体验收图：验证「简中显示」与两种显示模式的实际观感。

本图不是手工画的示意图 —— 图中的简体文本全部由
`tools/build_lang_table.py` 生成的映射表（lang_hant2hans.json）**现场转换**而来，
因此它同时是那张映射表的端到端验证。

模拟引擎的真实行为（见 docs/分辨率与高清化架构.md 第六节）：
  · 高清模式 (native) ：矢量字体【按目标物理尺寸直接栅格化】，覆盖率 100%
  · 像素模式 (pixel)  ：12px 点阵【整数倍放大 ×3】，缺字回退到霞鹜文楷
                        —— 对应 SDL_ttf 的 TTF_AddFallbackFont()
"""
import json
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
PIX_HANS = os.path.join(FONTS, "FusionPixel12-zh_hans.ttf")
PIX_HANT = os.path.join(FONTS, "FusionPixel12-zh_hant.ttf")

for p in (HIGH, PIX_HANS, PIX_HANT):
    print(f"{'OK  ' if os.path.isfile(p) else 'MISS '}{os.path.basename(p):<32}"
          f"{os.path.getsize(p) if os.path.isfile(p) else 0:>12,} 字节")

# ---------------- 载入映射表（本图的简体文本由它生成） ----------------
MAP_PATH = os.path.join(ROOT, ".workbuddy", "data", "json", "lang_hant2hans.json")
tab = json.load(open(MAP_PATH, encoding="utf-8"))
H2S = tab["map"]
print(f"\n载入映射表 {os.path.relpath(MAP_PATH, ROOT)}")
print(f"  映射条目 {len(H2S)} 条，冲突 {len(tab.get('conflicts', {}))} 个")


def t2s(s):
    """按离线映射表转简体（引擎运行时行为一致）。"""
    return "".join(H2S.get(ch, ch) for ch in s)


def load_cmap(path):
    f = TTFont(path, fontNumber=0, lazy=True)
    cm = set()
    for t in f["cmap"].tables:
        cm |= set(t.cmap.keys())
    f.close()
    return cm


CM_HANS = load_cmap(PIX_HANS)
CM_HANT = load_cmap(PIX_HANT)
print(f"  像素字体内码位: zh_hans {len(CM_HANS):,} / zh_hant {len(CM_HANT):,}")

W, H = 1600, 1520
BG = (247, 245, 240)
INK = (44, 44, 42)
MUTE = (112, 110, 105)
FAINT = (150, 147, 142)
ACCENT = (24, 95, 165)
AMBER = (140, 82, 12)
GREEN = (28, 110, 70)

img = Image.new("RGB", (W, H), BG)
dr = ImageDraw.Draw(img)

f_title = ImageFont.truetype(HIGH, 40)
f_sub = ImageFont.truetype(HIGH, 22)
f_h = ImageFont.truetype(HIGH, 26)
f_note = ImageFont.truetype(HIGH, 18)
f_small = ImageFont.truetype(HIGH, 15)
f_trad = ImageFont.truetype(HIGH, 26)

y = 46
dr.text((70, y), "字体与简中显示验收 · 三国群英传3", font=f_title, fill=INK)
y += 56
dr.text((70, y), "图中简体文本由离线映射表现场转换 —— 本图同时是转换表的端到端验证",
        font=f_sub, fill=MUTE)
y += 40
dr.line((70, y, W - 70, y), fill=(210, 205, 195), width=2)
y += 34


def section(y, color, title, note):
    dr.rectangle((70, y + 4, 84, y + 28), fill=color)
    dr.text((100, y), title, font=f_h, fill=INK)
    y += 40
    dr.text((70, y), note, font=f_note, fill=MUTE)
    return y + 34


def pixel_line(y, text, cmap, px=12, scale=3, x0=78, width=1030):
    """像素模式：像素字按小尺寸绘制 → NEAREST 整数倍放大；缺字回退。

    回退字的正确做法（引擎实现必须照做）：
      **按目标物理尺寸直接栅格化（px × scale），不参与位图放大。**
      反例（错误做法）：把 12px 矢量字与像素字一起画在 12px 画布上再整体放大 ×3 ——
      12px 矢量字笔画本就纤细，放大后要么糊成一团灰，要么二值化后笔画断裂。
      这正是本轮实测踩到的坑，已写入 docs/分辨率与高清化架构.md。
    """
    bh = px + 12
    tmp = Image.new("RGB", (width, bh), BG)
    td = ImageDraw.Draw(tmp)
    f_pix = ImageFont.truetype(PIX_HANS, px)
    cell = td.textlength("国", font=f_pix)

    x = 2.0
    fell = []
    for ch in text:
        if ord(ch) in cmap:
            td.text((x, 3), ch, font=f_pix, fill=INK)
            x += td.textlength(ch, font=f_pix)
        else:
            fell.append((x, ch))
            x += cell

    big = tmp.resize((width * scale, bh * scale), Image.NEAREST)
    img.paste(big, (x0, y))

    if fell:
        # 收小到 0.80 并在格内居中，给相邻像素字留出间隙
        fb_size = int(round(px * scale * 0.80))
        f_fb = ImageFont.truetype(HIGH, fb_size)
        f_ref = ImageFont.truetype(PIX_HANS, px)
        dy = f_ref.getbbox("国")[1] * scale - f_fb.getbbox("国")[1]
        d2 = ImageDraw.Draw(img)
        for sx, ch in fell:
            adv = d2.textlength(ch, font=f_fb)
            draw_x = x0 + sx * scale + max(0.0, (cell * scale - adv) / 2)
            d2.text((draw_x, y + 3 * scale + dy), ch, font=f_fb, fill=INK)
        print(f"    回退字 {len(fell)} 个，字号 {fb_size}px（像素格 {cell*scale:.1f}px）")

    return bh * scale, [c for _, c in fell]


# ================= A. 繁 → 简 转换 =================
y = section(y, ACCENT, "① 繁 → 简 转换（离线映射表，引擎运行时查表）",
            "原版数据是 Big5 繁体；下表左为原文，右为转换后 —— 右侧文本即为屏幕上实际显示的内容")

PAIRS = [
    ("三國群英傳三 · 必殺技", "武将名"),
    ("孫權  曹操  劉備  呂布  諸葛亮", "物品名"),
    ("青龍偃月刀  方天畫戟  赤兔馬", "部队/界面"),
    ("武力 81  士兵 1000  等級 50", "数值界面"),
    ("黃巾頭目  黃巾將軍", "第4项需求名单"),
]
for trad, tag in PAIRS:
    simp = t2s(trad)
    dr.text((70, y + 6), trad, font=f_trad, fill=FAINT)
    dr.text((452, y), "→", font=f_h, fill=FAINT)
    dr.text((486, y), simp, font=ImageFont.truetype(HIGH, 30), fill=INK)
    dr.text((W - 190, y + 8), tag, font=f_small, fill=FAINT)
    y += 46

y += 8
dr.line((70, y, W - 70, y), fill=(220, 215, 205), width=2)
y += 30

# ================= B. 简中 · 高清模式 =================
y = section(y, ACCENT, "② 简中显示 · 高清模式（默认） · LXGWWenKai 矢量 · 覆盖率 100%",
            "矢量按物理尺寸直接栅格化：逻辑 14px 在 2K(×3) 下以 42px 渲染，不参与位图放大，2K/4K 不糊")

for text, size, color in [
    ("三国群英传三 · 必杀技", 44, INK),
    ("武力 81　士兵 1000　等级 50", 38, (58, 56, 53)),
    ("小字号亦清晰：军师 智 96 · 城防 12,000 · 武将刘繇", 28, (86, 83, 79)),
]:
    dr.text((70, y), text, font=ImageFont.truetype(HIGH, size), fill=color)
    y += size + 22

y += 6
dr.line((70, y, W - 70, y), fill=(220, 215, 205), width=2)
y += 30

# ================= C. 简中 · 像素模式 =================
y = section(y, AMBER, "③ 简中显示 · 像素模式（可选） · FusionPixel12-zh_hans · 简中覆盖 99.75%",
            "12px 点阵整数倍放大 ×3 → 36px，保留原版点阵观感；非整数倍缩放时会降级为高清模式")

for text in ["三国群英传三 · 必杀技",
             "武力 81　士兵 1000　等级 50　黄巾头目"]:
    hgt, _ = pixel_line(y, text, CM_HANS)
    y += hgt + 10

y += 6
dr.line((70, y, W - 70, y), fill=(220, 215, 205), width=2)
y += 30

# ================= D. 回退链验证 =================
y = section(y, GREEN, "④ 字体回退链验证 · 像素字体缺字自动落到楷体，任何文本不出方框",
            "简中像素字体缺 5 字（繇 谡 鋻 铩 鹭），均为生僻字；刻意拼成一行以验证回退")

hgt, fell = pixel_line(y, "刘繇 马谡 铩羽 白鹭", CM_HANS)
y += hgt + 8
dr.text((78, y), f"本行触发回退的字：{' '.join(fell) if fell else '（无）'}"
                 f"　→ 未出现方框即回退链生效", font=f_small, fill=GREEN)
y += 30

# 繁中模式（可选）对比
dr.text((70, y), "对照 · 繁中模式（可选）FusionPixel12-zh_hant：", font=f_note, fill=MUTE)
y += 30
hgt, _ = pixel_line(y, "三國群英傳三 · 必殺技", CM_HANT)
y += hgt + 26

dr.line((70, y, W - 70, y), fill=(220, 215, 205), width=2)
y += 22
dr.text((70, y), "三套字体均为 SIL OFL-1.1，可自由使用、修改、随软件分发（含商业分发）。",
        font=f_small, fill=MUTE)
y += 24
dr.text((70, y), "限制：不得单独出售字体本体；衍生字体须沿用 OFL。许可证全文见 engine/assets/fonts/。",
        font=f_small, fill=MUTE)

out = os.path.join(OUT, "font_preview.png")
img.save(out)
print(f"\n已写出 {out}  ({os.path.getsize(out):,} 字节, {W}x{H})")
print(f"画布用满高度: {y + 20} / {H}")
