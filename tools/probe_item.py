# -*- coding: utf-8 -*-
"""勘察：武将攻击力构成 + 装备加成字段 + 必杀技学习判定所需的全部数据锚点。

用户口径（2026-09-13 补充）：
  必杀技学习判定 = 武将【当前攻击力】>= 80（含装备加成后的动态值），
  而不是静态的 General01.ini 基础武力值。
"""
import os
import re
import sys
import json

sys.stdout.reconfigure(encoding="utf-8")

PKG = r"E:\用户\workbuddy\移植PC计划"
RAW = os.path.join(PKG, ".workbuddy", "data", "raw")
if not os.path.isdir(RAW):
    RAW = os.path.join(PKG, ".workbuddy", "ini")

print("=" * 78)
print("0) 可用原始数据表")
print("=" * 78)
found = {}
for base in [os.path.join(PKG, ".workbuddy", "data", "raw"),
             os.path.join(PKG, ".workbuddy", "data"),
             os.path.join(PKG, ".workbuddy", "ini"),
             os.path.join(PKG, ".workbuddy", "dump")]:
    if not os.path.isdir(base):
        continue
    for r, d, fs in os.walk(base):
        for f in fs:
            if f.lower().endswith((".ini", ".txt")) and f not in found:
                found[f] = os.path.join(r, f)
for k in sorted(found):
    print(f"  {os.path.getsize(found[k]):>9,}  {k}")
print(f"  共 {len(found)} 个")

# ---------------------------------------------------------------- 1) 物品表字段
print()
print("=" * 78)
print("1) 物品表（Thing*.ini）字段结构 —— 找装备加成字段")
print("=" * 78)


def decode_big5(path):
    with open(path, "rb") as f:
        raw = f.read()
    for enc in ("big5", "big5hkscs", "cp950", "gbk", "utf-8"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("big5", errors="replace")


thing_files = [v for k, v in found.items() if k.lower().startswith("thing")]
if not thing_files:
    print("  (未找到 Thing*.ini，尝试从 PAK 现取)")
for p in thing_files:
    txt = decode_big5(p)
    lines = txt.splitlines()
    print(f"\n  --- {os.path.basename(p)}  共 {len(lines)} 行 ---")
    # 收集所有 key（去重，保序）
    keys = []
    seen = set()
    for ln in lines:
        m = re.match(r"\s*([A-Za-z_][A-Za-z0-9_]*)\s*=", ln)
        if m:
            k = m.group(1)
            if k not in seen:
                seen.add(k)
                keys.append(k)
    print(f"  字段 {len(keys)} 个：")
    for i in range(0, len(keys), 6):
        print("    " + "  ".join(f"{k:<16}" for k in keys[i:i + 6]))
    print("\n  前 3 个条目原文：")
    shown = 0
    for i, ln in enumerate(lines):
        if re.match(r"\s*\[", ln):
            blk = lines[i:i + 26]
            print("    " + "\n    ".join(x.rstrip() for x in blk))
            print("    " + "-" * 60)
            shown += 1
            if shown >= 3:
                break
    break

# ---------------------------------------------------------------- 2) 武将表字段
print()
print("=" * 78)
print("2) 武将表（General*.ini）字段总览 —— 找攻击力/装备槽字段")
print("=" * 78)
gen_files = [v for k, v in found.items() if k.lower().startswith("general")]
for p in gen_files:
    txt = decode_big5(p)
    lines = txt.splitlines()
    print(f"\n  --- {os.path.basename(p)}  共 {len(lines)} 行 ---")
    keys = []
    seen = set()
    for ln in lines:
        m = re.match(r'\s*"?([A-Za-z_][A-Za-z0-9_]*)"?\s*=', ln)
        if m:
            k = m.group(1)
            if k not in seen:
                seen.add(k)
                keys.append(k)
    print(f"  字段 {len(keys)} 个：")
    for i in range(0, len(keys), 6):
        print("    " + "  ".join(f"{k:<16}" for k in keys[i:i + 6]))
    break

# ---------------------------------------------------------------- 3) 关键字搜索
print()
print("=" * 78)
print("3) 全表关键字搜索（武力/攻击/加成/必杀相关）")
print("=" * 78)
PATTERNS = {
    "攻击力相关": r"(Attack|AttackPower|Power|Atk|Damage|Arms|Weapon)",
    "武力相关": r"(Strength|Force|WuLi|STR)",
    "加成相关": r"(Add|Bonus|Extra|Plus|Grow|Up)",
    "必杀技相关": r"(SuperAttack|SpecialAttack|SA[A-Z]|Magic)",
    "装备相关": r"(Equip|Item|Arms|Thing|Wear)",
}
for p in list(thing_files) + list(gen_files):
    txt = decode_big5(p)
    lines = txt.splitlines()
    print(f"\n  --- {os.path.basename(p)} ---")
    for label, pat in PATTERNS.items():
        hits = {}
        for ln in lines:
            for m in re.finditer(r'"?([A-Za-z_][A-Za-z0-9_]*)"?\s*=', ln):
                k = m.group(1)
                if re.search(pat, k, re.I):
                    hits[k] = hits.get(k, 0) + 1
        if hits:
            s = "  ".join(f"{k}({v})" for k, v in sorted(hits.items(),
                                                          key=lambda x: -x[1]))
            print(f"    {label}: {s}")

# ---------------------------------------------------------------- 4) 现有输出
print()
print("=" * 78)
print("4) 现有 auto_superattack_targets.json（当前口径）")
print("=" * 78)
jp = os.path.join(PKG, ".workbuddy", "data", "json", "auto_superattack_targets.json")
if os.path.isfile(jp):
    d = json.load(open(jp, encoding="utf-8"))
    print(f"  criteria = {d.get('criteria')}")
    print(f"  count    = {d.get('count')}")
    print(f"  excluded = {d.get('excluded_beast_count')}")
    gs = d.get("generals") or []
    print(f"  名单前 12： " + "、".join(
        f'{g["name"]}({g["strength"]})' for g in gs[:12]))
else:
    print("  (不存在)")
