"""把游戏 Big5 数据表解析为结构化 JSON 数据字典（引擎数据层地基）"""
import os, sys, re, json
sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DATA = os.path.join(ROOT, ".workbuddy", "data", "Setting")
OUT = os.path.join(ROOT, ".workbuddy", "data", "json")

def read_ini(path):
    with open(path, "rb") as f:
        raw = f.read()
    for enc in ("big5", "gbk", "utf-8"):
        try:
            txt = raw.decode(enc)
            break
        except UnicodeDecodeError:
            continue
    else:
        txt = raw.decode("latin1")
    sections = []
    cur = None
    for line in txt.splitlines():
        s = line.strip()
        if not s or s.startswith(";") or s.startswith("#"):
            continue
        m = re.match(r"^\[(.+?)\]\s*$", s)
        if m:
            cur = {"_section": m.group(1), "_kv": []}
            sections.append(cur)
            continue
        if "=" in s and cur is not None:
            k, v = s.split("=", 1)
            cur["_kv"].append((k.strip(), v.strip()))
    out = []
    for sec in sections:
        d = {"_section": sec["_section"]}
        for k, v in sec["_kv"]:
            if k in d:
                if isinstance(d[k], list):
                    d[k].append(v)
                else:
                    d[k] = [d[k], v]
            else:
                d[k] = v
        out.append(d)
    return out

def to_int(v, default=0):
    try:
        return int(str(v).split(",")[0].strip() or default)
    except Exception:
        return default

os.makedirs(OUT, exist_ok=True)
summary = []

# ---- 武将 ----
gens = read_ini(os.path.join(DATA, "General01.ini"))
recs = []
for g in gens:
    if g.get("_section") != "GENERAL" or "No" not in g:
        continue
    recs.append({
        "no": to_int(g.get("No")),
        "name": g.get("Name", ""),
        "strength": to_int(g.get("Strength")),
        "intelligence": to_int(g.get("Intelligence")),
        "hp": to_int(g.get("HP")),
        "mp": to_int(g.get("MP")),
        "justice": to_int(g.get("Justice")),
        "morale": to_int(g.get("Morale")),
        "personality": to_int(g.get("Personality")),
        "portrait": to_int(g.get("Portrait")),
        "weapon_type": to_int(g.get("WeaponType")),
        "bfai": to_int(g.get("BFAI")),
        "bfshape": to_int(g.get("BFShape")),
        "super_attack": [int(x) for x in str(g.get("SuperAttack", "")).split(",") if x.strip().isdigit()],
        "soldier_type": [to_int(x) for x in str(g.get("SoldierType", "")).split(",") if x.strip()],
        "rank": to_int(g.get("Rank")),
        "sex": to_int(g.get("Sex")),
    })
json.dump(recs, open(os.path.join(OUT, "generals.json"), "w", encoding="utf-8"),
          ensure_ascii=False, indent=1)
summary.append(f"generals.json    {len(recs)} 名武将")

# ---- 兵种 ----
sold = read_ini(os.path.join(DATA, "Soldier.ini"))
srecs = []
for s in sold:
    if s.get("_section") != "ITEM" or "Name" not in s:
        continue
    srecs.append({
        "no": to_int(s.get("No")),
        "name": s.get("Name", ""),
        "name_adv": s.get("NameAdv", ""),
        "res_id": to_int(s.get("ResID")),
        "start_hp": to_int(s.get("StartHP")),
        "add_hp": to_int(s.get("AddHP")),
        "start_power": to_int(s.get("StartPower")),
        "add_power": to_int(s.get("AddPower")),
        "hit_rate": [to_int(s.get(f"HitRate{i:02d}")) for i in range(10)],
    })
json.dump(srecs, open(os.path.join(OUT, "soldiers.json"), "w", encoding="utf-8"),
          ensure_ascii=False, indent=1)
summary.append(f"soldiers.json    {len(srecs)} 个兵种")

# ---- 武将技 ----
bf = read_ini(os.path.join(DATA, "BFMagic.ini"))
brecs = []
for b in bf:
    if b.get("_section") != "BF_MAGIC" or "Name" not in b:
        continue
    brecs.append({
        "no": to_int(b.get("No")),
        "name": b.get("Name", ""),
        "mp": to_int(b.get("MP")),
        "power": to_int(b.get("Power")),
        "level": to_int(b.get("Level")),
        "contribution": to_int(b.get("Contribution")),
        "attribute": to_int(b.get("Attribute")),
        "spec": b.get("Spec", ""),
    })
json.dump(brecs, open(os.path.join(OUT, "bfmagic.json"), "w", encoding="utf-8"),
          ensure_ascii=False, indent=1)
summary.append(f"bfmagic.json     {len(brecs)} 个武将技")

# ---- 物品 ----
th = read_ini(os.path.join(DATA, "Thing.ini"))
trecs = []
for t in th:
    if t.get("_section") != "ITEM" or "Name" not in t:
        continue
    trecs.append({
        "name": t.get("Name", ""),
        "type": to_int(t.get("Type")),
        "count": str(t.get("Count", "")),
        "statement": t.get("Statement", ""),
        "icon": t.get("Icon", ""),
        "level": to_int(t.get("Level")),
    })
json.dump(trecs, open(os.path.join(OUT, "things.json"), "w", encoding="utf-8"),
          ensure_ascii=False, indent=1)
summary.append(f"things.json      {len(trecs)} 件物品")

# ---- 全局规则 ----
gm = read_ini(os.path.join(DATA, "Game.ini"))
gmap = {}
for g in gm:
    if g.get("_section") in ("GENERALEXP", "SOLDIEREXP"):
        gmap[g["_section"]] = {k: to_int(v) for k, v in g.items() if not k.startswith("_")}
    elif g.get("_section") == "COST":
        gmap["COST"] = {k: to_int(v) for k, v in g.items() if not k.startswith("_")}
json.dump(gmap, open(os.path.join(OUT, "rules.json"), "w", encoding="utf-8"),
          ensure_ascii=False, indent=1)
summary.append(f"rules.json       升级经验曲线 {len(gmap.get('GENERALEXP', {}))} 级 / {len(gmap.get('SOLDIEREXP', {}))} 级")

# ---- 第 4 项需求：武力>=80 自动学必杀技 的目标名单 ----
# 用户口径（2026-09-13 两次确认）：
#   ① 野兽（虎 / 象）不发必杀技，后续另行设计"特殊攻击方式"；
#   ② 黃巾頭目 / 黃巾將軍 属于人形武将，正常发必杀技。
#
# 判定方式：结构化规则，不用名字黑名单。
# 理由：名字会随繁简转换 / 译名 / 后续 mod 变动，数值特征不会。
# 实测（421 名单位）：
#   (智力 <= 20 且 HP >= 150) 恰好命中 4 只野兽，零误伤；
#   单用"智力 <= 20"会误伤 兀突骨(智18) / 金環三結(智16) 两名真人武将。
#   黃巾頭目(智36/HP85) 与 黃巾將軍(智57/HP90) 自动落在人形一侧 —— 与用户口径一致。
BEAST_INT_MAX = 20
BEAST_HP_MIN = 150

def is_beast(r):
    return r["intelligence"] <= BEAST_INT_MAX and r["hp"] >= BEAST_HP_MIN

pool = [r for r in recs if r["strength"] >= 80 and not r["super_attack"]]
targets = [r for r in pool if not is_beast(r)]
excluded = [r for r in pool if is_beast(r)]
with_sa = [r for r in recs if r["super_attack"]]
json.dump({
    "criteria": "strength >= 80 and super_attack is empty and not a beast",
    "beast_rule": f"intelligence <= {BEAST_INT_MAX} and hp >= {BEAST_HP_MIN}",
    "beast_rule_note": "结构化规则；黃巾頭目/黃巾將軍 属人形武将，正常补必杀技",
    "count": len(targets),
    "excluded_beast_count": len(excluded),
    "generals": [{"no": r["no"], "name": r["name"], "strength": r["strength"]} for r in targets],
    "excluded_beasts": [{"no": r["no"], "name": r["name"], "strength": r["strength"],
                         "intelligence": r["intelligence"], "hp": r["hp"]} for r in excluded],
}, open(os.path.join(OUT, "auto_superattack_targets.json"), "w", encoding="utf-8"),
    ensure_ascii=False, indent=1)
summary.append(f"auto_superattack_targets.json  {len(targets)} 名武将待补 + {len(excluded)} 只野兽已排除")

print("=== 数据字典导出完成 ===")
for s in summary:
    print("  " + s)
print()
print(f"输出目录: {OUT}")
print()
print("--- 武力>=80 且无必杀技的武将（第 4 项需求目标）---")
print("  " + "、".join(f'{r["name"]}({r["strength"]})' for r in targets))
print()
print(f"--- 野生动物（按规则 智力<={BEAST_INT_MAX} 且 HP>={BEAST_HP_MIN} 排除，不发必杀技）---")
print("  " + "、".join(f'{r["name"]}(智{r["intelligence"]}/HP{r["hp"]})' for r in excluded))
print()
print("--- 已有必杀技的武将（前 20）---")
print("  " + "、".join(f'{r["name"]}({"+".join(map(str,r["super_attack"]))})' for r in with_sa[:20]))
print()
print("--- 8 种必杀技 ---")
from collections import Counter
c = Counter(a for r in recs for a in r["super_attack"])
for k in sorted(c):
    print(f"   {k}: {c[k]} 人")
