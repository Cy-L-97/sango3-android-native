# -*- coding: utf-8 -*-
"""勘察第二轮：确认装备加成语义与"当前攻击力"计算，并算出三类名单。

背景（用户 2026-09-13 补充口径）：
    必杀技学习判定 = 武将【当前攻击力】>= 80（含装备加成后的动态值）。
    即：基础武力 < 80 的武将，靠装备把攻击力顶到 >= 80，同样要学会必杀技。

第一轮已确认：
    - General01.ini 的 Weapon / Book / Horse 三个装备槽存的是【物品名字符串】（不是索引）
    - Thing.ini 的 Increment/2/3 是装备加成，语义由 Statement 说明：
        武器(Type=2) -> 武力上升
        书(Type=3)   -> 智力 / 技力上限 / 体力上限
        马(Type=4)   -> 速度上升（Increment2 另有特性位）
本脚本据此计算并输出三类名单。
"""
import json
import os
import re
import sys
from collections import Counter

sys.stdout.reconfigure(encoding="utf-8")

PKG = r"E:\用户\workbuddy\移植PC计划"
DATA = os.path.join(PKG, ".workbuddy", "data", "Setting")
OUT = os.path.join(PKG, ".workbuddy", "data", "json")


def read_blocks(path):
    with open(path, "rb") as f:
        raw = f.read()
    for enc in ("big5", "big5hkscs", "gbk", "utf-8"):
        try:
            txt = raw.decode(enc)
            break
        except UnicodeDecodeError:
            continue
    else:
        txt = raw.decode("big5", errors="replace")
    blocks, cur = [], None
    for line in txt.splitlines():
        s = line.strip()
        if not s or s.startswith(";"):
            continue
        m = re.match(r"^\[(.+?)\]\s*$", s)
        if m:
            cur = {"section": m.group(1), "kv": []}
            blocks.append(cur)
            continue
        if "=" in s and cur is not None:
            k, v = s.split("=", 1)
            cur["kv"].append((k.strip(), v.strip()))
    return blocks


def to_int(v, d=0):
    try:
        return int(str(v).split(",")[0].strip() or d)
    except Exception:
        return d


# ---------------------------------------------------------------- 物品索引
things = read_blocks(os.path.join(DATA, "Thing.ini"))
by_name = {}
for t in things:
    d = dict(t["kv"])
    nm = (d.get("Name") or "").strip()
    if not nm:
        continue
    by_name.setdefault(nm, {
        "name": nm,
        "type": to_int(d.get("Type")),
        "level": to_int(d.get("Level")),
        "statement": d.get("Statement", ""),
        "increment": [to_int(d.get("Increment")), to_int(d.get("Increment2")), to_int(d.get("Increment3"))],
    })

# 加成语义判定
def slot_of(ty):
    return {2: "weapon", 3: "book", 4: "horse", 5: "formation",
            6: "treasure", 7: "herb", 8: "fungus"}.get(ty, "other")


def strength_inc(item):
    """武器(Type=2) 的 Increment1 = 武力加成；其他槽位不加武力。"""
    if item["type"] == 2:
        return item["increment"][0]
    return 0


print("=" * 78)
print("1) 各槽位物品的加成语义（按 Statement 校准）")
print("=" * 78)
buckets = {}
for nm, it in by_name.items():
    buckets.setdefault(it["type"], []).append(it)
for ty in sorted(buckets):
    lst = buckets[ty]
    incs = Counter(i["increment"][0] for i in lst)
    print(f"  Type={ty}({slot_of(ty):<10}) {len(lst):>3} 件  Increment1 分布: "
          + "、".join(f"{k}({v})" for k, v in sorted(incs.items())))

# ---------------------------------------------------------------- 武将
gens = []
for g in read_blocks(os.path.join(DATA, "General01.ini")):
    d = dict(g["kv"])
    if g["section"] != "GENERAL" or "No" not in d:
        continue
    sa = [int(x) for x in str(d.get("SuperAttack", "")).split(",") if x.strip().isdigit()]
    gens.append({
        "no": to_int(d.get("No")),
        "name": (d.get("Name") or "").strip(),
        "strength": to_int(d.get("Strength")),
        "intelligence": to_int(d.get("Intelligence")),
        "hp": to_int(d.get("HP")),
        "weapon": (d.get("Weapon") or "").strip(),
        "book": (d.get("Book") or "").strip(),
        "horse": (d.get("Horse") or "").strip(),
        "super_attack": sa,
        "rank": to_int(d.get("Rank")),
    })

print()
print("=" * 78)
print("2) 初始装备自带武器的武将（原版数据）")
print("=" * 78)
with_eq = [g for g in gens if g["weapon"]]
print(f"  带武器 {len(with_eq)} / {len(gens)} 名")
shown = 0
for g in with_eq[:25]:
    it = by_name.get(g["weapon"])
    inc = strength_inc(it) if it else 0
    print(f"    {g['name']:<6} 基础武力={g['strength']:<4} 武器={g['weapon']:<8} "
          f"武力+{inc:<3} -> 当前={g['strength']+inc}")
    shown += 1
print(f"  ...（共 {len(with_eq)} 名）")

print()
print("  武器出现频次 Top12：")
c = Counter(g["weapon"] for g in with_eq)
for k, v in c.most_common(12):
    it = by_name.get(k)
    print(f"     {k:<10} x{v:<4} 武力+{strength_inc(it) if it else '?'}")

# ---------------------------------------------------------------- 野兽规则
BEAST_INT_MAX, BEAST_HP_MIN = 20, 150


def is_beast(g):
    return g["intelligence"] <= BEAST_INT_MAX and g["hp"] >= BEAST_HP_MIN


# ---------------------------------------------------------------- 三类名单
def cur_atk(g):
    it = by_name.get(g["weapon"])
    return g["strength"] + (strength_inc(it) if it else 0)


print()
print("=" * 78)
print("3) 三类名单（判定 = 当前攻击力 >= 80 且无必杀技 且非野兽）")
print("=" * 78)

# A: 基础武力已 >= 80（不依赖装备，初始即达标）
# B: 基础 < 80，但装上【初始自带武器】后 >= 80（装备带来的新增目标）
# C: 基础 < 80 且初始无武器/不足以达标，但换装更强武器后理论上能跨过 80（动态潜在）
A = [g for g in gens if g["strength"] >= 80 and not g["super_attack"] and not is_beast(g)]
B = [g for g in gens if g["strength"] < 80 and cur_atk(g) >= 80
     and not g["super_attack"] and not is_beast(g)]
C = [g for g in gens if g["strength"] < 80 and cur_atk(g) < 80
     and g["strength"] + 20 >= 80          # 奧汀神槍 +20 是游戏内武器上限
     and not g["super_attack"] and not is_beast(g)]
excl = [g for g in gens if is_beast(g) and (g["strength"] >= 80 or cur_atk(g) >= 80)
        and not g["super_attack"]]

print(f"\n  A 类 · 基础武力 >= 80，初始即达标：{len(A)} 名")
print("    " + "、".join(f'{g["name"]}({g["strength"]})' for g in A))

print(f"\n  B 类 · 基础 < 80，靠【初始自带武器】顶到 >= 80（这是之前遗漏的一批）：{len(B)} 名")
if B:
    for g in B:
        it = by_name.get(g["weapon"])
        inc = strength_inc(it) if it else 0
        print(f'    {g["name"]:<6} 基础 {g["strength"]} + 武器[{g["weapon"]} +{inc}] = {g["strength"]+inc}')
else:
    print("    （无 —— 原版数据里没有人靠初始武器跨过 80）")

print(f"\n  C 类 · 基础 < 80，但换更强武器后可变身达标（引擎需运行时动态判定）：{len(C)} 名")
print("    " + "、".join(f'{g["name"]}({g["strength"]})' for g in C[:60])
      + (" ..." if len(C) > 60 else ""))

print(f"\n  排除 · 野兽（智<={BEAST_INT_MAX} 且 HP>={BEAST_HP_MIN}）：{len(excl)} 只")
print("    " + "、".join(f'{g["name"]}(武{g["strength"]}/智{g["intelligence"]}/HP{g["hp"]})' for g in excl))

print()
print("=" * 78)
print("4) 关键结论")
print("=" * 78)
print(f"  · 静态需补名单（A）= {len(A)} 名；装备带来的新增（B）= {len(B)} 名")
print(f"  · 但 C 类 {len(C)} 名证明：判定【必须】运行时做，不能靠静态名单 ——")
print("    同一名武将，换一把武器就从'不达标'变成'达标'。")
print(f"  · 原版 421 名武将里，{sum(1 for g in gens if g['super_attack'])} 名已有必杀技，{len(gens)-sum(1 for g in gens if g['super_attack'])} 名没有。")

json.dump({
    "criteria": "current_attack = strength + weapon_increment(strength slot); "
                "learn super attack when current_attack >= 80 and is a humanoid general",
    "note": "判定必须运行时进行：装备可更换，攻击力随之变化（用户 2026-09-13 口径）",
    "weapon_strength_increment_range": [0, 20],
    "class_A_base_ge80": [{"no": g["no"], "name": g["name"], "strength": g["strength"]} for g in A],
    "class_B_equipped_ge80": [{"no": g["no"], "name": g["name"], "base": g["strength"],
                               "weapon": g["weapon"], "bonus": strength_inc(by_name.get(g["weapon"]))}
                              for g in B],
    "class_C_potential": [{"no": g["no"], "name": g["name"], "strength": g["strength"]} for g in C],
    "excluded_beasts": [{"no": g["no"], "name": g["name"], "strength": g["strength"],
                         "intelligence": g["intelligence"], "hp": g["hp"]} for g in excl],
}, open(os.path.join(OUT, "auto_superattack_targets_v2.json"), "w", encoding="utf-8"),
    ensure_ascii=False, indent=1)
print(f"\n  已写出 {os.path.join(OUT, 'auto_superattack_targets_v2.json')}")
