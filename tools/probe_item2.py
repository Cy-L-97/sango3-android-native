# -*- coding: utf-8 -*-
"""深挖装备加成：Increment 语义 + 装备槽引用方式 + 当前攻击力计算。

目标：验证"必杀技学习判定 = 当前攻击力(= 武力 + 装备加成) >= 80"能否在数据层精确表达。
"""
import os
import re
import sys
from collections import Counter, defaultdict

sys.stdout.reconfigure(encoding="utf-8")

PKG = r"E:\用户\workbuddy\移植PC计划"
DATA = os.path.join(PKG, ".workbuddy", "data", "Setting")


def read_blocks(path):
    """返回 [{'section':..., 'kv':[(k,v)]}] 保序。"""
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
    blocks = []
    cur = None
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


# ---------------------------------------------------------------- Thing.ini
things = read_blocks(os.path.join(DATA, "Thing.ini"))
print("=" * 78)
print(f"1) Thing.ini 共 {len(things)} 个条目（按出现顺序编号 0 起）")
print("=" * 78)

TYPES = defaultdict(list)
for idx, t in enumerate(things):
    d = dict(t["kv"])
    TYPES[to_int(d.get("Type"))].append((idx, d))

for ty in sorted(TYPES):
    lst = TYPES[ty]
    print(f"\n  Type={ty}  共 {len(lst)} 个")
    names = [d.get("Name", "?") for _, d in lst]
    print("    样例: " + "、".join(names[:14]) + (" ..." if len(names) > 14 else ""))

print()
print("=" * 78)
print("2) Increment / Increment2 / Increment3 分布（非零才有意义）")
print("=" * 78)
nz = []
for idx, t in enumerate(things):
    d = dict(t["kv"])
    i1, i2, i3 = to_int(d.get("Increment")), to_int(d.get("Increment2")), to_int(d.get("Increment3"))
    if i1 or i2 or i3:
        nz.append((idx, d, i1, i2, i3))
print(f"  含非零加成的条目：{len(nz)} / {len(things)}")
print()
print("  --- 全部非零加成条目（最多 60 条）---")
for idx, d, i1, i2, i3 in nz[:60]:
    print(f"   #{idx:<5} Type={to_int(d.get('Type')):<2} {d.get('Name',''):<12} "
          f"Inc=({i1:>3},{i2:>3},{i3:>3})  {d.get('Statement','')[:34]}")

print()
print("  --- 非零加成的数值三元组频次 ---")
c = Counter((i1, i2, i3) for _, _, i1, i2, i3 in nz)
for k, v in c.most_common(20):
    print(f"    {str(k):<20} x{v}")

print()
print("=" * 78)
print("3) 含'武/力/攻'字样的 Statement（推断 Increment 语义）")
print("=" * 78)
KW = ("武力", "智力", "體力", "体力", "攻擊", "攻击", "防禦", "防御", "武力值")
seen = 0
for idx, t in enumerate(things):
    d = dict(t["kv"])
    st = d.get("Statement", "")
    if any(k in st for k in KW):
        i1, i2, i3 = to_int(d.get("Increment")), to_int(d.get("Increment2")), to_int(d.get("Increment3"))
        print(f"   #{idx:<5} {d.get('Name',''):<12} Inc=({i1:>3},{i2:>3},{i3:>3})  {st[:44]}")
        seen += 1
        if seen >= 40:
            break
if not seen:
    print("  （无）")

# ---------------------------------------------------------------- General01
gens = read_blocks(os.path.join(DATA, "General01.ini"))
print()
print("=" * 78)
print("4) General01.ini：Weapon / Book / Horse 装备槽取值")
print("=" * 78)
gre = []
for g in gens:
    d = dict(g["kv"])
    if g["section"] != "GENERAL" or "No" not in d:
        continue
    gre.append(d)
print(f"  武将 {len(gre)} 名")
for f in ("Weapon", "Book", "Horse", "WeaponType"):
    vals = [to_int(d.get(f)) for d in gre]
    cv = Counter(vals)
    top = "、".join(f"{k}({v})" for k, v in cv.most_common(10))
    print(f"  {f:<11} 非零 {sum(1 for v in vals if v)} 个；取值 Top10: {top}")

print()
print("  --- 样例武将（曹操 / 吕布 / 丁奉 / 呂蒙）---")
for want in ("曹操", "呂布", "吕布", "丁奉", "呂蒙", "關羽", "关羽"):
    for d in gre:
        if d.get("Name", "").strip() == want:
            print(f"    {d.get('Name'):<6} No={d.get('No'):<5} 武力={d.get('Strength'):<4} "
                  f"Weapon={d.get('Weapon'):<5} Book={d.get('Book'):<5} Horse={d.get('Horse'):<5} "
                  f"WeaponType={d.get('WeaponType'):<3} SuperAttack={d.get('SuperAttack','')!r}")
            break

print()
print("=" * 78)
print("5) 验证映射假设：General01.Weapon 值 -> Thing.ini 第 N 个条目")
print("=" * 78)
for probe in ("曹操", "呂布", "丁奉"):
    for d in gre:
        if d.get("Name", "").strip() == probe:
            for slot, base in (("Weapon", None), ("Book", None), ("Horse", None)):
                v = to_int(d.get(slot))
                if v <= 0:
                    continue
                for off in (0, 1):
                    k = v - off
                    if 0 <= k < len(things):
                        td = dict(things[k]["kv"])
                        print(f"    {probe} {slot}={v} -> 索引{k}(假设-{off}): "
                              f"Type={to_int(td.get('Type'))} {td.get('Name','')} "
                              f"Inc=({to_int(td.get('Increment'))},{to_int(td.get('Increment2'))},{to_int(td.get('Increment3'))})")
            break

# ---------------------------------------------------------------- 攻击力重算
print()
print("=" * 78)
print("6) 修正后的判定：当前攻击力 = 武力 + 装备加成  （按索引-1 假设）")
print("=" * 78)


def equip_inc(gv):
    tot = [0, 0, 0]
    detail = []
    for slot in ("Weapon", "Book", "Horse"):
        v = to_int(gv.get(slot))
        if v <= 0:
            continue
        for off in (1, 0):
            k = v - off
            if 0 <= k < len(things):
                td = dict(things[k]["kv"])
                inc = (to_int(td.get("Increment")), to_int(td.get("Increment2")), to_int(td.get("Increment3")))
                if any(inc):
                    detail.append((slot, v, td.get("Name", ""), inc))
                    for i in range(3):
                        tot[i] += inc[i]
                break
    return tot, detail


cnt_with_inc = 0
for d in gre:
    tot, det = equip_inc(d)
    if det:
        cnt_with_inc += 1
print(f"  装备带加成的武将：{cnt_with_inc} / {len(gre)}")
shown = 0
for d in gre:
    tot, det = equip_inc(d)
    if det and shown < 12:
        s = "; ".join(f"{sl}={v}:{nm}{inc}" for sl, v, nm, inc in det)
        print(f"    {d.get('Name'):<6} 武力={to_int(d.get('Strength')):<4} 加成={tot} | {s}")
        shown += 1
