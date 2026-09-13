# -*- coding: utf-8 -*-
"""
probe_res2.py —— 验证两件事：
  A) 用结构化规则（智力/HP）判定"野兽"，看是否会误伤真武将 → 决定 build_data.py 的规则
  B) 确认逻辑坐标空间：把 Menu.ini 的坐标与已知素材物理尺寸（433x34 / 100x120）对上，
     并列出根窗口与字号相关声明。
"""
import os, re, sys
sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SET = os.path.join(ROOT, ".workbuddy", "data", "Setting")


def read_text(path):
    with open(path, "rb") as f:
        raw = f.read()
    for enc in ("big5", "gbk", "utf-8"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("latin1")


def sections(txt):
    out, cur, d = [], None, None
    for line in txt.splitlines():
        s = line.strip()
        if not s or s.startswith(";") or s.startswith("#"):
            continue
        m = re.match(r"^\[(.+?)\]\s*$", s)
        if m:
            if cur is not None:
                out.append((cur, d))
            cur, d = m.group(1), {}
            continue
        if "=" in s and d is not None:
            k, v = s.split("=", 1)
            d[k.strip()] = v.strip()
    if cur is not None:
        out.append((cur, d))
    return out


def i(v, dflt=0):
    try:
        return int(str(v).split(",")[0].strip())
    except Exception:
        return dflt


# ============================================================ A
print("=" * 78)
print("A) 野兽判定规则验证")
print("=" * 78)
gens = sections(read_text(os.path.join(SET, "General01.ini")))
recs = []
for name, d in gens:
    if name != "GENERAL" or "No" not in d:
        continue
    recs.append({"no": i(d.get("No")), "name": d.get("Name", ""),
                 "str": i(d.get("Strength")), "int": i(d.get("Intelligence")),
                 "hp": i(d.get("HP")), "rank": i(d.get("Rank")),
                 "weapontype": i(d.get("WeaponType")), "soldier": d.get("SoldierType", "")})
print(f"武将总数 = {len(recs)}\n")

rules = {
    "智力<=20": lambda r: r["int"] <= 20,
    "HP>=150": lambda r: r["hp"] >= 150,
    "智力<=20 且 HP>=150": lambda r: r["int"] <= 20 and r["hp"] >= 150,
    "Rank==5": lambda r: r["rank"] == 5,
    "智力<=20 且 HP>=150 且 Rank==5": lambda r: r["int"] <= 20 and r["hp"] >= 150 and r["rank"] == 5,
}
for label, fn in rules.items():
    hit = [r for r in recs if fn(r)]
    print(f"规则 [{label}] 命中 {len(hit)} 条:")
    print("   " + "、".join(f'{r["name"]}(智{r["int"]}/HP{r["hp"]}/R{r["rank"]})' for r in hit))
    print()

print("--- 智力最低的 15 名（看有没有真武将混入低智力区）---")
for r in sorted(recs, key=lambda x: x["int"])[:15]:
    print(f'   {r["name"]:10s} 智{r["int"]:3d} HP{r["hp"]:4d} Rank{r["rank"]} 武力{r["str"]:3d}')
print()

print("--- HP 最高的 15 名 ---")
for r in sorted(recs, key=lambda x: -x["hp"])[:15]:
    print(f'   {r["name"]:10s} HP{r["hp"]:4d} 智{r["int"]:3d} Rank{r["rank"]} 武力{r["str"]:3d}')
print()

# ============================================================ B
print("=" * 78)
print("B) 逻辑坐标空间验证")
print("=" * 78)
txt = read_text(os.path.join(SET, "Menu.ini"))
lines = txt.splitlines()

# 1) 根窗口声明
print("--- 含 640 or 480 的行（前 25）---")
n = 0
for idx, line in enumerate(lines, 1):
    if re.search(r"\b(640|480)\b", line):
        n += 1
        if n <= 25:
            print(f"   {idx:5d}: {line.strip()[:100]}")
print(f"   合计 {n} 行\n")

# 2) 已知素材物理尺寸 433x34 是否出现在坐标里
print("--- 坐标中出现 433 / 34 / 100 / 120 的行（找素材尺寸与逻辑坐标的对应）---")
for target in ("433", "464", "120"):
    hits = [(idx, l.strip()) for idx, l in enumerate(lines, 1)
            if re.search(rf"\b{target}\b", l) and "=" in l]
    print(f"   [{target}] {len(hits)} 行" + (f"  例: {hits[0][1][:90]}" if hits else ""))
print()

# 3) 全部 4 元组的 x2 值分布（界面宽度谱）
w4 = []
for line in lines:
    if "=" not in line:
        continue
    m = re.search(r"=\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*$", line.strip())
    if m:
        w4.append((int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)), line.strip()))
from collections import Counter
cw = Counter(x[2] for x in w4)
print(f"--- 4元组 x2 值出现频次（共 {len(w4)} 条）---")
for v, c in cw.most_common(18):
    print(f"   x2={v:5d}  ×{c}")
print()

print("--- 接近界面满宽的条目（x2>=600）---")
for x1, y1, x2, y2, raw in w4:
    if x2 >= 600:
        print(f"   {raw[:100]}")

# 4) 字号 / 字体声明
print()
print("--- Menu.ini 中的 Font 相关行 ---")
for idx, line in enumerate(lines, 1):
    if re.search(r"font|size|\.fnt", line, re.I):
        print(f"   {idx:5d}: {line.strip()[:100]}")

print()
print("--- Setting/Font001.ini 全文 ---")
p = os.path.join(SET, "Font001.ini")
if os.path.isfile(p):
    for line in read_text(p).splitlines()[:40]:
        print("   " + line[:110])
