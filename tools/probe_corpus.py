# -*- coding: utf-8 -*-
"""诊断：新语料（7105 字）是否被二进制文件污染。"""
import os
import re
import struct
import sys

sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
GAME = r"E:\Program Files (x86)\steam\steamapps\common\Sango3"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pak import read_index  # noqa: E402

CJK = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")
TEXT_EXT = (".ini", ".txt", ".dat")

rows = []
for pak_name in ("Update.PAK", "Sango3.PAK"):
    pak = os.path.join(GAME, pak_name)
    entries, _, _, _ = read_index(pak)
    with open(pak, "rb") as f:
        for name, off, size in entries:
            if not name.lower().endswith(TEXT_EXT) or size <= 0 or size > 8 * 1024 * 1024:
                continue
            f.seek(off)
            raw = f.read(size)
            try:
                text = raw.decode("big5")
                dec = "big5-ok"
            except UnicodeDecodeError:
                text = raw.decode("big5", errors="replace")
                dec = "big5-replace"
            cs = set(CJK.findall(text))
            rows.append((pak_name, name, size, dec, len(text), len(cs), text))

disk = os.path.join(ROOT, ".workbuddy", "data", "Setting")
if os.path.isdir(disk):
    for fn in sorted(os.listdir(disk)):
        p = os.path.join(disk, fn)
        if not os.path.isfile(p):
            continue
        raw = open(p, "rb").read()
        try:
            text = raw.decode("big5")
            dec = "big5-ok"
        except UnicodeDecodeError:
            text = raw.decode("big5", errors="replace")
            dec = "big5-replace"
        rows.append(("disk", fn, len(raw), dec, len(text), len(set(CJK.findall(text))), text))

rows.sort(key=lambda r: -r[5])
print("=" * 96)
print("按汉字数量排序（前 25），检查是否存在「二进制被当文本」的伪文本")
print("=" * 96)
print(f"{'来源':<11}{'条目':<34}{'字节':>9}{'解码':<14}{'字符':>8}{'汉字':>7}  伪字符%")
print("-" * 96)
for pak, name, size, dec, ntext, ncs, text in rows[:25]:
    # 伪字符：非汉字、非 ASCII、非常见标点/空白 的比例
    junk = sum(1 for ch in text
               if not (CJK.match(ch) or ch.isascii())
               and ch not in "　、。，；：？！「」『』（）〈〉《》…—－·【】％＃＆＊＋－／｜")
    jr = junk / max(1, ntext) * 100
    flag = "  ← 疑似污染" if jr > 8 else ""
    print(f"{pak:<11}{name[:33]:<34}{size:>9,}{dec:<14}{ntext:>8,}{ncs:>7}  {jr:>6.2f}%{flag}")

print()
print("=" * 96)
print("汇总")
print("=" * 96)
allc = set()
for *_, text in rows:
    allc |= set(CJK.findall(text))
print(f"  文本条目总数 {len(rows)}，合计不同汉字 {len(allc)}")

# 分来源统计
for grp in ("Update.PAK", "Sango3.PAK", "disk"):
    s = set()
    for pak, name, size, dec, ntext, ncs, text in rows:
        if pak == grp:
            s |= set(CJK.findall(text))
    print(f"  {grp:<12} 汉字 {len(s):>6}")

# 疑似污染文件贡献的独有字
print()
print("=" * 96)
print("疑似污染条目（伪字符 > 8%）明细")
print("=" * 96)
bad = [r for r in rows if (lambda t: sum(1 for ch in t if not (CJK.match(ch) or ch.isascii())) / max(1, len(t)) > 0.08)(r[6])]
if bad:
    for pak, name, size, dec, ntext, ncs, text in bad[:12]:
        print(f"  {pak}::{name}  {size:,} 字节  汉字 {ncs}")
        print(f"     预览: {text[:120]!r}")
else:
    print("  （无）")
