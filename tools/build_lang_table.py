# -*- coding: utf-8 -*-
"""从游戏自身文本生成【繁 → 简】字符映射表（离线生成，供 C 引擎运行时使用）。

为什么离线生成：
  引擎是 C/C++，不能依赖 Python 的 opencc。但游戏用到的汉字集合是**固定且有限**的
  （数据表 + 剧情文本），所以离线把「用到的繁体字 → 简体字」做成静态表，
  引擎侧只查表即可，零运行时依赖、零内存开销。

做法：
  1. 从 Update.PAK 提取全部文本类条目（.ini / .txt / .dat），Big5 解码；
  2. 用 OpenCC(t2s) 逐段转换（含词组级上下文，非简单逐字）；
  3. 比对原文与转换结果，提取**字符级映射**，并检测一对多冲突；
  4. 输出映射表 + 繁/简两套字符集（后者供字体覆盖率量化使用）。

局限（已在输出中标注）：
  字符级映射无法表达词组级歧义（如「著/着」「乾/干」在个别词中的差异）。
  本脚本检测出的冲突会列在报告里，供人工裁定。
"""
import json
import os
import re
import struct
import sys

sys.stdout.reconfigure(encoding="utf-8")

from opencc import OpenCC

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
GAME = r"E:\Program Files (x86)\steam\steamapps\common\Sango3"
JSOND = os.path.join(ROOT, ".workbuddy", "data", "json")
os.makedirs(JSOND, exist_ok=True)

CJK = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")
TEXT_EXT = (".ini", ".txt", ".dat")
# 常见标点/空白（允许出现，不计为「伪字符」）
OK_PUNCT = set("　、。，；：？！「」『』（）〈〉《》…—－·【】％＃＆＊＋－／｜~\"'(),.:;!?-_=+*/[]{}<>@#$%^&|\\")


def clean_text(raw):
    """把字节流当作 Big5 文本解出，并判断「这是不是真的文本」。

    关键教训：Sango3.PAK 里的 `Setting\\AllFont.dat` 是**字体二进制数据**，
    被当 Big5 解码后能产出 5920 个「伪汉字」（全角空格 + 乱码），
    会把 1970 字的真实语料虚增到 7105 字，直接影响字体覆盖率结论。
    """
    strict = True
    try:
        text = raw.decode("big5")
    except UnicodeDecodeError:
        text = raw.decode("big5", errors="replace")
        strict = False
    if not text:
        return None, strict, 1.0
    junk = sum(1 for ch in text
               if not (CJK.match(ch) or ch.isascii() or ch in OK_PUNCT or ch == "\ufffd"))
    ratio = junk / len(text)
    # 判据：伪字符 > 2% 或 严格解码失败且伪字符 > 0.5% → 不是纯文本
    if ratio > 0.02 or (not strict and ratio > 0.005):
        return None, strict, ratio
    return text, strict, ratio

# ---------------------------------------------------------------- 取字体？不，先取文本
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pak import read_index  # noqa: E402

print("=" * 76)
print("1) 从 PAK 提取文本类条目")
print("=" * 76)

sources = {}
skipped = []
for pak_name in ("Update.PAK", "Sango3.PAK"):
    pak = os.path.join(GAME, pak_name)
    if not os.path.isfile(pak):
        print(f"  · 跳过（不存在）{pak}")
        continue
    entries, _, _, _ = read_index(pak)
    hit = 0
    with open(pak, "rb") as f:
        for name, off, size in entries:
            if not name.lower().endswith(TEXT_EXT):
                continue
            if size <= 0 or size > 8 * 1024 * 1024:
                continue
            f.seek(off)
            raw = f.read(size)
            text, strict, ratio = clean_text(raw)
            if text is None:
                skipped.append((f"{pak_name}::{name}", size, f"伪字符 {ratio*100:.1f}%"))
                continue
            sources[f"{pak_name}::{name}"] = text
            hit += 1
    print(f"  ✓ {pak_name} 提取文本条目 {hit} 个")

# 磁盘外置 Setting 目录（第三方补丁，可能更新）
disk_setting = os.path.join(ROOT, ".workbuddy", "data", "Setting")
if os.path.isdir(disk_setting):
    n = 0
    for fn in sorted(os.listdir(disk_setting)):
        p = os.path.join(disk_setting, fn)
        if not os.path.isfile(p):
            continue
        text, strict, ratio = clean_text(open(p, "rb").read())
        if text is None:
            skipped.append((f"disk::Setting/{fn}", os.path.getsize(p), f"伪字符 {ratio*100:.1f}%"))
            continue
        sources[f"disk::Setting/{fn}"] = text
        n += 1
    print(f"  ✓ 磁盘 Setting 目录 {n} 个文件")

if skipped:
    print()
    print("  ⚠ 已排除的非文本条目（避免污染语料）：")
    for name, size, why in skipped:
        print(f"      {name}  {size:,} 字节  ({why})")

print()
print("=" * 76)
print("2) OpenCC 繁 → 简 转换")
print("=" * 76)

cc = None
for cfg in ("t2s", "t2s.json", "t2s"):
    try:
        cc = OpenCC(cfg)
        print(f"  ✓ OpenCC 配置 '{cfg}' 加载成功")
        break
    except Exception as e:  # noqa: BLE001
        print(f"  · 配置 '{cfg}' 失败: {type(e).__name__}")
if cc is None:
    print("  ✗ OpenCC 不可用，终止")
    sys.exit(1)

hant_chars = set()
hans_chars = set()
mapping = {}
conflicts = {}
changed_pairs = {}

total_segments = 0
for key, text in sources.items():
    total_segments += 1
    hant_chars |= set(CJK.findall(text))
    conv = cc.convert(text)
    hans_chars |= set(CJK.findall(conv))
    # 逐字对齐：两串长度应一致（t2s 不增删字符）
    if len(conv) == len(text):
        for a, b in zip(text, conv):
            if a != b and CJK.match(a):
                if a in mapping and mapping[a] != b:
                    conflicts.setdefault(a, set()).add(mapping[a])
                    conflicts[a].add(b)
                mapping[a] = b
                changed_pairs[a] = b
    else:
        # 长度不一致时退化为逐字转换，保证不漏字
        for a in set(CJK.findall(text)):
            b = cc.convert(a)
            if len(b) == 1 and b != a and CJK.match(b):
                if a in mapping and mapping[a] != b:
                    conflicts.setdefault(a, set()).add(mapping[a])
                    conflicts[a].add(b)
                mapping[a] = b
                changed_pairs[a] = b

print(f"  处理文本源 {total_segments} 个")
print(f"  繁体字符集 {len(hant_chars)} 字 / 简体字符集 {len(hans_chars)} 字")
print(f"  发生变化的字 {len(mapping)} 个")
if conflicts:
    print(f"  ⚠ 一对多冲突 {len(conflicts)} 个（需人工裁定）：")
    for k, v in sorted(conflicts.items()):
        print(f"      {k} → {' / '.join(sorted(v))}")
else:
    print("  ✓ 无双射冲突（每个繁体字唯一对应一个简体字）")

print()
print("=" * 76)
print("3) 抽样对照（改动的字）")
print("=" * 76)
sample_keys = ["武", "將", "兵", "國", "軍", "戰", "關", "張", "趙", "孫", "權", "殺",
               "擊", "氣", "連", "亂", "書", "馬", "劍", "錢", "體", "經", "驗", "級",
               "敵", "勝", "敗", "陣", "營", "糧", "寶", "獎", "幣", "額", "頭", "額"]
pairs = [(k, mapping[k]) for k in sample_keys if k in mapping]
lines = []
for i in range(0, len(pairs), 8):
    lines.append("  " + "  ".join(f"{a}→{b}" for a, b in pairs[i:i + 8]))
print("\n".join(lines))

print()
print("=" * 76)
print("4) 关键词转换验证")
print("=" * 76)
KEY = ["必殺技", "孫權", "曹操", "劉備", "關羽", "張飛", "呂布", "諸葛亮",
       "黃巾頭目", "黃巾將軍", "青龍偃月刀", "方天畫戟", "丈八蛇矛", "赤兔馬",
       "士兵", "武力", "智力", "體力", "經驗", "等級", "城池", "武將", "攻擊", "撤退"]
for k in KEY:
    print(f"  {k:<12} → {cc.convert(k)}")

# ---------------------------------------------------------------- 输出
out = {
    "note": ("繁→简字符映射表（由 OpenCC t2s 从游戏自身文本离线生成）。"
             "引擎侧查表即可，无需运行时依赖。字符级映射无法表达词组级歧义。"),
    "generator": "tools/build_lang_table.py",
    "excluded_non_text": [{"entry": n, "size": s, "reason": w} for n, s, w in skipped],
    "source_charset_traditional_count": len(hant_chars),
    "source_charset_simplified_count": len(hans_chars),
    "mapping_count": len(mapping),
    "conflicts": {k: sorted(v) for k, v in sorted(conflicts.items())},
    "map": dict(sorted(mapping.items())),
}
p = os.path.join(JSOND, "lang_hant2hans.json")
json.dump(out, open(p, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print()
print(f"✓ 写出 {os.path.relpath(p, ROOT)}  ({os.path.getsize(p):,} 字节)")

# 字符集（繁体原始 / 简体转换后），供字体覆盖率量化
p2 = os.path.join(JSOND, "text_charset.json")
json.dump({
    "note": "游戏文本用字集合。traditional = 原版 Big5 文本；simplified = t2s 转换后（简中显示模式用）。",
    "traditional": "".join(sorted(hant_chars)),
    "simplified": "".join(sorted(hans_chars)),
    "traditional_count": len(hant_chars),
    "simplified_count": len(hans_chars),
}, open(p2, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print(f"✓ 写出 {os.path.relpath(p2, ROOT)}  ({os.path.getsize(p2):,} 字节)")
