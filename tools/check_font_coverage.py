# -*- coding: utf-8 -*-
"""量化候选字体对【游戏实际文本】的覆盖度 —— 繁、简两套字符集分别统计。

起因：
  渲染验收时发现 "必殺技" 的 "殺" 字在 ArkPixel12-zh_tw 里显示为方框，说明
  「看起来能用」和「真的能用」是两件事。字体必须用真实文本量化。

字符集来源（由 tools/build_lang_table.py 产出）：
  · traditional = 原版 Big5 文本用字（数据表 + 剧情 + Sango3.PAK 内文本条目）
  · simplified  = OpenCC t2s 转换后（简中显示模式用）

关键判据：
  ① 简中为默认显示模式 → 重点看「简体集」覆盖率；
  ② 缺字必须是**生僻字**才可接受；缺高频字（如 孙/杀/击）等于不可用。
"""
import json
import os
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")
from fontTools.ttLib import TTFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
FONTS = os.path.join(ROOT, "engine", "assets", "fonts")
DL = os.path.join(ROOT, ".workbuddy", "downloads", "fonts")
JSOND = os.path.join(ROOT, ".workbuddy", "data", "json")
SETTING = os.path.join(ROOT, ".workbuddy", "data", "Setting")

CJK = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")


# ---------------------------------------------------------------- 字符集
def read_text(path):
    raw = open(path, "rb").read()
    for enc in ("big5", "big5hkscs", "gbk", "utf-8"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("big5", errors="replace")


cs_path = os.path.join(JSOND, "text_charset.json")
if os.path.isfile(cs_path):
    d = json.load(open(cs_path, encoding="utf-8"))
    hant = set(d["traditional"])
    hans = set(d["simplified"])
    src = "text_charset.json（由 build_lang_table.py 生成）"
else:
    print("! text_charset.json 缺失，回退到现场扫描（不含简中集）")
    hant = set()
    for fn in sorted(os.listdir(SETTING)):
        if fn.lower().endswith(".ini"):
            hant |= set(CJK.findall(read_text(os.path.join(SETTING, fn))))
    hans = set()
    src = "现场扫描 .workbuddy/data/Setting"

print("=" * 78)
print("1) 字符集")
print("=" * 78)
print(f"  来源：{src}")
print(f"  繁体集（原版 Big5 文本）  {len(hant):>6} 字")
print(f"  简体集（t2s 转换后）      {len(hans):>6} 字")

# ---------------------------------------------------------------- 候选字体
CANDIDATES = [
    ("FusionPixel12-zh_hans", "像素·简中", os.path.join(FONTS, "FusionPixel12-zh_hans.ttf")),
    ("FusionPixel12-zh_hant", "像素·繁中", os.path.join(FONTS, "FusionPixel12-zh_hant.ttf")),
    ("LXGWWenKai", "矢量·楷体", os.path.join(FONTS, "LXGWWenKai-Regular.ttf")),
    ("ArkPixel12-zh_cn", "像素·参考", os.path.join(DL, "ark", "ark-pixel-12px-proportional-zh_cn.ttf")),
    ("ArkPixel12-zh_tw", "像素·参考", os.path.join(DL, "ark", "ark-pixel-12px-proportional-zh_tw.ttf")),
]

# 供下载包内的候选（未部署也算）
for nm, lbl, rel in list(CANDIDATES):
    if not os.path.isfile(rel):
        alt = os.path.join(DL, "FusionPixel12", os.path.basename(rel))
        if os.path.isfile(alt):
            CANDIDATES[CANDIDATES.index((nm, lbl, rel))] = (nm, lbl, alt)


def load_cmap(path):
    f = TTFont(path, fontNumber=0, lazy=True)
    cm = set()
    for t in f["cmap"].tables:
        cm |= set(t.cmap.keys())
    f.close()
    return cm


print()
print("=" * 78)
print("2) 覆盖率（分别对繁体集 / 简体集）")
print("=" * 78)
print(f"  {'字体':<24}{'用途':<10}{'内码位':>9}{'繁缺':>7}{'简缺':>7}{'简覆盖':>9}  判定")
print("  " + "-" * 76)

results = {}
for label, use, path in CANDIDATES:
    if not os.path.isfile(path):
        print(f"  {label:<24}{use:<10}  文件不存在")
        continue
    try:
        cm = load_cmap(path)
    except Exception as e:  # noqa: BLE001
        print(f"  {label:<24}{use:<10}  读取失败 {type(e).__name__}")
        continue
    miss_h = sorted(c for c in hant if ord(c) not in cm)
    miss_s = sorted(c for c in hans if ord(c) not in cm)
    cov_s = (len(hans) - len(miss_s)) / len(hans) * 100 if hans else 0.0
    verdict = "✓ 可用" if cov_s >= 99.5 else ("△ 勉强" if cov_s >= 98 else "✗ 不可用")
    print(f"  {label:<24}{use:<10}{len(cm):>9,}{len(miss_h):>7}{len(miss_s):>7}"
          f"{cov_s:>8.2f}%  {verdict}")
    results[label] = {
        "use": use, "codepoints": len(cm),
        "missing_traditional": len(miss_h), "missing_simplified": len(miss_s),
        "coverage_simplified": round(cov_s, 3),
        "missing_simplified_chars": "".join(miss_s),
        "missing_traditional_chars": "".join(miss_h),
    }

# ---------------------------------------------------------------- 关键词
print()
print("=" * 78)
print("3) 关键词实测（简中模式下最常出现在屏幕上的字）")
print("=" * 78)
KEY = {
    "武将名": "曹操刘备孙权关羽张飞吕布貂蝉诸葛亮司马懿赵云黄忠马超周瑜陆逊",
    "技能名": "必杀技气旋大喝连刺拖刀挑斩一击生擒乱舞",
    "界面词": "武力智力体力速度士兵等级经验金钱城池武将指令攻击防御撤退内政外交",
    "物品名": "青龙偃月刀方天画戟丈八蛇矛赤兔马的卢青囊书遁甲天书干将莫邪",
    "黄巾": "黄巾头目黄巾将军",
}
for label, _, path in CANDIDATES:
    if label not in results:
        continue
    cm = load_cmap(path)
    print(f"\n  {label}")
    for k, s in KEY.items():
        miss = [c for c in s if ord(c) not in cm]
        tag = "✓ 全覆盖" if not miss else f"✗ 缺 {len(miss)}: {''.join(miss)}"
        print(f"    {k}: {tag}")

json.dump({
    "charset_source": src,
    "traditional_count": len(hant),
    "simplified_count": len(hans),
    "fonts": results,
}, open(os.path.join(ROOT, ".workbuddy", "data", "font_coverage.json"), "w",
        encoding="utf-8"), ensure_ascii=False, indent=1)
print()
print("已写出 .workbuddy/data/font_coverage.json")
