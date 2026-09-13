# -*- coding: utf-8 -*-
"""量化内置像素字体对【游戏实际文本】的覆盖度。

起因：渲染验收时发现 "必殺技" 的 "殺" 字在 ArkPixel12-zh_tw 里显示为方框。
必须回答：这套像素字体能不能用于本项目？缺多少字？缺哪些字？

字符集来源 = 游戏自身的全部可见文本：
  · 数据表：General01(武将名)/Thing(物品名+说明)/BFMagic(技名)/Soldier/Text/Menu
  · 文本表：Dialogue.ini / EventMsg.ini / Event*.ini
  · 数据字典 JSON 里的中文字段
"""
import json
import os
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")
from fontTools.ttLib import TTFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
FONTS = os.path.join(ROOT, "engine", "assets", "fonts")
SETTING = os.path.join(ROOT, ".workbuddy", "data", "Setting")
JSOND = os.path.join(ROOT, ".workbuddy", "data", "json")

CJK = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")


def read_text(path):
    with open(path, "rb") as f:
        raw = f.read()
    for enc in ("big5", "big5hkscs", "gbk", "utf-8"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("big5", errors="replace")


# ---------------- 收集游戏字符集 ----------------
chars = set()
src_stat = {}

for fn in sorted(os.listdir(SETTING)):
    if not fn.lower().endswith(".ini"):
        continue
    p = os.path.join(SETTING, fn)
    t = read_text(p)
    got = set(CJK.findall(t))
    chars |= got
    src_stat[fn] = len(got)

for fn in sorted(os.listdir(JSOND)):
    if not fn.endswith(".json"):
        continue
    p = os.path.join(JSOND, fn)
    t = open(p, encoding="utf-8").read()
    chars |= set(CJK.findall(t))
    src_stat[fn] = len(set(CJK.findall(t)))

print("=" * 76)
print(f"1) 游戏文本字符集：共 {len(chars)} 个不同的汉字")
print("=" * 76)
for k in sorted(src_stat):
    print(f"  {src_stat[k]:>5}  {k}")

# ---------------- 字体覆盖分析 ----------------
print()
print("=" * 76)
print("2) 字体覆盖分析")
print("=" * 76)

FONTS_TO_CHECK = [
    ("ArkPixel12-zh_tw", "ArkPixel12-zh_tw.ttf"),
    ("ArkPixel12-zh_cn", os.path.join(".workbuddy", "downloads", "fonts", "ark",
                                       "ark-pixel-12px-proportional-zh_cn.ttf")),
    ("LXGWWenKai", "LXGWWenKai-Regular.ttf"),
]


def load_cmap(path):
    f = TTFont(path, fontNumber=0, lazy=True)
    cmap = set()
    for table in f["cmap"].tables:
        cmap |= set(table.cmap.keys())
    f.close()
    return cmap


results = {}
for label, rel in FONTS_TO_CHECK:
    p = rel if os.path.isabs(rel) else os.path.join(FONTS, rel)
    if not os.path.isfile(p):
        p2 = os.path.join(ROOT, rel)
        if os.path.isfile(p2):
            p = p2
        else:
            print(f"  ? {label:<20} 文件不存在 ({rel})")
            continue
    try:
        cm = load_cmap(p)
    except Exception as e:  # noqa: BLE001
        print(f"  ✗ {label:<20} 读取失败 {type(e).__name__}: {str(e)[:50]}")
        continue
    missing = sorted(c for c in chars if ord(c) not in cm)
    cov = (len(chars) - len(missing)) / len(chars) * 100
    results[label] = (len(cm), len(missing), cov, missing)
    print(f"\n  {label}")
    print(f"    字体内码位数 {len(cm):,}")
    print(f"    游戏用字 {len(chars)}  缺失 {len(missing)}  覆盖率 {cov:.2f}%")
    if missing:
        print(f"    缺失字符（前 80）：{''.join(missing[:80])}")

# ---------------- 结论 ----------------
print()
print("=" * 76)
print("3) 参考：按用途分组的关键词覆盖")
print("=" * 76)
KEY = {
    "必杀技相关": "必殺技氣旋大喝連刺拖刀挑斬一擊生擒亂舞",
    "武将名抽样": "曹操劉備孫權關羽張飛呂布貂蟬諸葛亮司馬懿趙雲黃忠馬超",
    "界面词抽样": "武力智力體力速度士兵等級經驗金錢城池武將指令攻擊防禦撤退",
    "物品名抽样": "青龍偃月刀方天畫戟丈八蛇矛赤兔馬的盧青囊書遁甲天書干將莫邪",
}
for label, (_, path) in [(l, (None, os.path.join(FONTS, r))) for l, r in FONTS_TO_CHECK]:
    p = path
    if not os.path.isfile(p):
        continue
    cm = load_cmap(p)
    print(f"\n  {label}")
    for k, s in KEY.items():
        miss = [c for c in s if ord(c) not in cm]
        tag = "✓ 全覆盖" if not miss else f"✗ 缺 {len(miss)}: {''.join(miss)}"
        print(f"    {k}: {tag}")

json.dump({k: {"codepoints": v[0], "missing": v[1], "coverage": round(v[2], 2),
               "missing_chars": "".join(v[3])} for k, v in results.items()},
          open(os.path.join(ROOT, ".workbuddy", "data", "font_coverage.json"), "w",
               encoding="utf-8"), ensure_ascii=False, indent=1)
print()
print(f"已写出 .workbuddy/data/font_coverage.json")
