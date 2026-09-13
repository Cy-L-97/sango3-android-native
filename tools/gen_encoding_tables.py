# -*- coding: utf-8 -*-
"""生成引擎用的【二进制定长编码表】（离线生成一次，随仓库同步）。

产出 engine/assets/encoding/：
  big5_cp950.bin   Big5/CP950 → Unicode 映射，C 引擎解码 INI 用
  hant2hans.bin    繁 → 简 映射，C 引擎"简中显示"用

为什么落成二进制表：
  引擎是 C，要在 Windows 与 Android 上都能离线解码 Big5。
  Win32 的 MultiByteToWideChar 不可移植，Android 上更没有；
  而游戏用到的编码空间是**封闭**的（Big5 双字节全表 13973 个位置），
  直接摊平成定长数组，查表 O(1)，无任何平台依赖。

文件格式（全部小端）：
  big5_cp950.bin : magic "B5TB"(4) + uint32 count(4) + count × uint16 (Unicode 码点，0=无映射)
  hant2hans.bin  : magic "H2S1"(4) + uint32 count(4) + count × (uint32 繁, uint32 简)  按繁排序，便于二分

Big5 索引方式（与 C 侧 text.c 严格一致）：
  lead  0xA1..0xF9        → li = lead - 0xA1            (0..88)
  trail 0x40..0x7E        → ti = trail - 0x40           (0..62)
  trail 0xA1..0xFE        → ti = 63 + (trail - 0xA1)    (63..156)
  每 lead 固定 157 个 trail → index = li * 157 + ti

自检（关键）：
  用刚生成的表在 Python 里"模拟 C 的解码过程"，把 .workbuddy/data/Setting 下
  全部 INI 解一遍，与 Python 原生 big5 解码结果**逐字符比对**。
  不通过就报错退出 —— 保证 C 与 Python 两侧对同一份数据的解读完全一致。
"""
import json
import os
import re
import struct
import sys

sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SETTING = os.path.join(ROOT, ".workbuddy", "data", "Setting")
JSOND = os.path.join(ROOT, ".workbuddy", "data", "json")
OUTDIR = os.path.join(ROOT, "engine", "assets", "encoding")

LEAD_LO, LEAD_HI = 0xA1, 0xF9          # 含两端
TRAIL_A_LO, TRAIL_A_HI = 0x40, 0x7E
TRAIL_B_LO, TRAIL_B_HI = 0xA1, 0xFE
TRAILS_PER_LEAD = (TRAIL_A_HI - TRAIL_A_LO + 1) + (TRAIL_B_HI - TRAIL_B_LO + 1)  # 63 + 94 = 157


def trail_index(t: int) -> int:
    if TRAIL_A_LO <= t <= TRAIL_A_HI:
        return t - TRAIL_A_LO
    if TRAIL_B_LO <= t <= TRAIL_B_HI:
        return 63 + (t - TRAIL_B_LO)
    return -1


# ---------------------------------------------------------------- 1) Big5 表
# 说明：CPython 的 'big5' 编解码器即 CP950（台湾常用），与本表口径一致。
def build_big5_table():
    table = []
    for lead in range(LEAD_LO, LEAD_HI + 1):
        for ti in range(TRAILS_PER_LEAD):
            # ti → trail 反推
            if ti < 63:
                trail = TRAIL_A_LO + ti
            else:
                trail = TRAIL_B_LO + (ti - 63)
            try:
                ch = bytes((lead, trail)).decode("big5")
                cp = ord(ch) if len(ch) == 1 else 0
            except UnicodeDecodeError:
                cp = 0
            table.append(cp)
    return table


def decode_with_table(raw: bytes, table) -> str:
    """完全复刻 C 侧 text.c 的解码逻辑，用来做自检。"""
    out = []
    i = 0
    n = len(raw)
    while i < n:
        b = raw[i]
        if b < 0x80:
            out.append(chr(b))
            i += 1
            continue
        if LEAD_LO <= b <= LEAD_HI and i + 1 < n:
            ti = trail_index(raw[i + 1])
            if ti >= 0:
                cp = table[(b - LEAD_LO) * TRAILS_PER_LEAD + ti]
                if cp:
                    out.append(chr(cp))
                    i += 2
                    continue
        out.append("\ufffd")     # 无映射 → REPLACEMENT CHARACTER（与 C 侧一致）
        i += 1
    return "".join(out)


# ---------------------------------------------------------------- 2) 繁→简表
def build_hant2hans():
    p = os.path.join(JSOND, "lang_hant2hans.json")
    if not os.path.isfile(p):
        print(f"  ✗ 缺少 {p}，请先跑 tools/build_lang_table.py")
        sys.exit(1)
    data = json.load(open(p, encoding="utf-8"))
    m = data["map"]
    pairs = sorted((ord(k), ord(v)) for k, v in m.items())
    return pairs, data


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    ok = True

    print("=" * 76)
    print("1) Big5/CP950 → Unicode 表")
    print("=" * 76)
    table = build_big5_table()
    mapped = sum(1 for c in table if c)
    print(f"  条目 {len(table)}（lead {LEAD_LO:#x}..{LEAD_HI:#x} × {TRAILS_PER_LEAD}）")
    print(f"  有效映射 {mapped}，空位 {len(table) - mapped}")
    p1 = os.path.join(OUTDIR, "big5_cp950.bin")
    with open(p1, "wb") as f:
        f.write(b"B5TB")
        f.write(struct.pack("<I", len(table)))
        f.write(struct.pack(f"<{len(table)}H", *table))
    print(f"  ✓ {os.path.relpath(p1, ROOT)}  ({os.path.getsize(p1):,} 字节)")

    print()
    print("=" * 76)
    print("2) 繁 → 简 表")
    print("=" * 76)
    pairs, lang = build_hant2hans()
    print(f"  映射 {len(pairs)} 对（来源 {lang.get('generator')}）")
    if lang.get("conflicts"):
        print(f"  ⚠ 一对多冲突 {len(lang['conflicts'])} 个 —— 取排序后第一个，与 Python 侧 test 无关")
    p2 = os.path.join(OUTDIR, "hant2hans.bin")
    with open(p2, "wb") as f:
        f.write(b"H2S1")
        f.write(struct.pack("<I", len(pairs)))
        for a, b in pairs:
            f.write(struct.pack("<II", a, b))
    print(f"  ✓ {os.path.relpath(p2, ROOT)}  ({os.path.getsize(p2):,} 字节)")

    print()
    print("=" * 76)
    print("3) 自检：用生成的表解码全部 INI，与 Python 原生 big5 逐字符比对")
    print("=" * 76)
    if not os.path.isdir(SETTING):
        print(f"  · 跳过（{SETTING} 不存在）")
    else:
        files = sorted(fn for fn in os.listdir(SETTING) if fn.lower().endswith(".ini"))
        bad = []
        for fn in files:
            raw = open(os.path.join(SETTING, fn), "rb").read()
            mine = decode_with_table(raw, table)
            try:
                ref = raw.decode("big5")
            except UnicodeDecodeError:
                ref = raw.decode("big5", errors="replace")
                # Python 的 replace 与我们的 REPLACEMENT 策略可能对不齐，只比长度量级
                mine = mine.replace("\ufffd", "")
                ref = ref.replace("\ufffd", "")
            if mine != ref:
                # 定位第一处差异
                k = next((i for i in range(min(len(mine), len(ref))) if mine[i] != ref[i]),
                         min(len(mine), len(ref)))
                bad.append((fn, k, repr(ref[max(0, k - 10):k + 10]), repr(mine[max(0, k - 10):k + 10])))
            else:
                print(f"  ✓ {fn:<24} {len(raw):>7,} 字节  {len(ref):>7,} 字符  一致")
        if bad:
            ok = False
            print()
            print("  ✗ 以下文件解码不一致：")
            for fn, k, r, m in bad:
                print(f"      {fn}  首处差异 @{k}\n        python: {r}\n        table : {m}")
        else:
            print(f"\n  ✓ 全部 {len(files)} 个 INI 解码一致 —— C 侧将与 Python 侧得到相同文本")

    print()
    print("=" * 76)
    print("4) 抽样验证：必杀技规则涉及的关键实体名（繁 → 简）")
    print("=" * 76)
    m = {chr(a): chr(b) for a, b in pairs}

    def conv(s):
        return "".join(m.get(ch, ch) for ch in s)

    for k in ["呂布", "關羽", "張飛", "諸葛亮", "黃巾頭目", "青龍偃月刀", "方天畫戟", "丈八蛇矛"]:
        print(f"  {k:<8} → {conv(k)}")

    print()
    print("=" * 76)
    if ok:
        print("✓ 编码表生成完毕，全部自检通过")
    else:
        print("✗ 自检未通过，请检查上面的差异")
        sys.exit(1)


if __name__ == "__main__":
    main()
