# -*- coding: utf-8 -*-
"""
probe_res.py —— 两件事：
  A) 看 General01.ini 里 No=416..421（黄巾头目/黄巾将军/猛虎/白额虎/南蛮象/印度神象）
     的**全部原始字段**，判断是否有结构化字段可用于区分"人形武将 / 野兽"（比名字黑名单稳）。
  B) 测出原版游戏的**逻辑坐标空间**到底多大：扫描 Menu.ini 等界面数据里的
     Range / Pos / Size 坐标，求最大边界。这是"分辨率无关渲染架构"的锚点。
"""
import os, re, sys, json
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
    """切成 [(name, {k:v})]，值保留原文。"""
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


print("=" * 78)
print("A) No=416..421 的全部原始字段（找'人形/野兽'的结构化区分）")
print("=" * 78)
gens = sections(read_text(os.path.join(SET, "General01.ini")))
want = {416, 417, 418, 419, 420, 421}
found = {}
for name, d in gens:
    if name != "GENERAL":
        continue
    try:
        no = int(d.get("No", "-1"))
    except ValueError:
        continue
    if no in want:
        found[no] = d

# 汇总字段名
allkeys = []
for no, d in found.items():
    for k in d:
        if k not in allkeys:
            allkeys.append(k)

print(f"共取到 {len(found)} 条\n")
for no in sorted(found):
    d = found[no]
    print(f"--- No={no}  {d.get('Name')} (Strength={d.get('Strength')}) ---")
    print("   " + "  ".join(f"{k}={v}" for k, v in d.items()))
    print()

# 对比：拿两个普通武将做参照
print("--- 参照：普通武将（No=1 丁奉 / No=220 曹操）---")
for name, d in gens:
    if name != "GENERAL":
        continue
    if d.get("No") in ("1", "220"):
        print("   " + "  ".join(f"{k}={v}" for k, v in d.items()))
print()

# 找出哪些字段在这 6 条上与普通武将系统性不同
print("--- 字段差异扫描：这 6 条 与 全部武将 的取值分布对比 ---")
normal = [d for name, d in gens if name == "GENERAL" and d.get("No", "").isdigit()
          and int(d["No"]) not in want]
for k in allkeys:
    special_vals = {d.get(k, "<缺>") for d in found.values()}
    normal_vals = {}
    for d in normal:
        v = d.get(k, "<缺>")
        normal_vals[v] = normal_vals.get(v, 0) + 1
    # 只报"这 6 条的取值完全不在普通武将取值集合里"的字段 → 可能是区分字段
    if all(v not in normal_vals for v in special_vals):
        print(f"  ★ {k:16s} 特殊单位={sorted(special_vals)}  普通武将无此取值")
    else:
        # 报取值较集中的
        common = set(sorted(normal_vals, key=lambda x: -normal_vals[x])[:3])
        if special_vals & common:
            pass
print()

print("=" * 78)
print("B) 逻辑坐标空间探测（Menu.ini 等界面数据的坐标范围）")
print("=" * 78)

CAND = ["Menu.ini", "Game.ini", "Battle.ini", "Map.ini", "BFAI.ini"]
for fn in CAND:
    p = os.path.join(SET, fn)
    if not os.path.isfile(p):
        print(f"  ({fn} 不存在，跳过)")
        continue
    txt = read_text(p)
    nums_by_key = {}
    for line in txt.splitlines():
        s = line.strip()
        if "=" not in s or s.startswith(";"):
            continue
        k, v = s.split("=", 1)
        k, v = k.strip(), v.strip()
        if not re.search(r"[-,]", v):
            continue
        nv = [int(x) for x in re.findall(r"\d+", v)]
        if len(nv) in (2, 4):
            nums_by_key.setdefault(k.strip(), []).append(nv)
    if not nums_by_key:
        print(f"  {fn}: 无坐标型字段")
        continue
    print(f"\n  ### {fn} —— 坐标型字段：")
    for k, lists in sorted(nums_by_key.items(), key=lambda x: -len(x[1]))[:14]:
        flat = [n for l in lists for n in l]
        mx = max(flat)
        # 猜测 4 元组为 x1,y1,x2,y2 或 x,y,w,h
        w4 = [l for l in lists if len(l) == 4]
        mx_right = max((l[2] for l in w4), default=0)
        mx_bottom = max((l[3] for l in w4), default=0)
        print(f"    {k:22s} n={len(lists):4d}  max={mx:5d}  "
              f"(4元组的 x2/y2 最大: {mx_right}/{mx_bottom})  样例={lists[:2]}")

print()
print("=" * 78)
print("C) 分辨率相关关键字全文检索（找 800/600/640/480/1024/screen/mode）")
print("=" * 78)
PAT = re.compile(r"(800|600|640|480|1024|768|resolution|screen|width|height|viewport|fullscreen|mode)\s*=\s*(\S+)", re.I)
hits = 0
for fn in sorted(os.listdir(SET)):
    if not fn.lower().endswith(".ini"):
        continue
    txt = read_text(os.path.join(SET, fn))
    for i, line in enumerate(txt.splitlines(), 1):
        m = PAT.search(line)
        if m:
            hits += 1
            if hits <= 40:
                print(f"  {fn}:{i}  {line.strip()[:110]}")
print(f"  命中 {hits} 处")

print()
print("=" * 78)
print("D) PAK 中的字体与光标资源（高清化时的依赖项）")
print("=" * 78)
