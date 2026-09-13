# -*- coding: utf-8 -*-
"""把游戏 Big5 数据表解析为结构化 JSON 数据字典（引擎数据层地基）

【必杀技学习判定口径 —— 用户 2026-09-13 最终确认】
    判定依据是武将的【当前攻击力】，而不是 General01.ini 里的基础武力。
    当前攻击力 = 基础武力 + 武器加成（Thing.ini Type=2 的 Increment 值，0~20）。
    因此一名基础武力 75 的武将，装上 +6 的武器后攻击力 81 >= 80，同样要学会必杀技。
    → 该判定【必须在运行时进行】：武器可随时更换，攻击力随之变化。

    这条口径改变了实现方式：
      · 旧（错误）：M0 阶段生成一份静态名单，开局一次性补数据；
      · 新（正确）：引擎的武将属性系统 + 装备系统 + 事件驱动的学习检查。
    本脚本仍会输出名单，但用途变为【验证用基准】，而非"补丁数据"。
"""
import os, sys, re, json
sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DATA = os.path.join(ROOT, ".workbuddy", "data", "Setting")
OUT = os.path.join(ROOT, ".workbuddy", "data", "json")

# 物品 Type -> 槽位语义（依据 Thing.ini 的 Statement 逐件校准）
SLOT_OF = {1: "soldier_token", 2: "weapon", 3: "book", 4: "horse",
           5: "formation", 6: "treasure", 7: "herb", 8: "fungus"}
# 只有武器(Type=2)的 Increment1 加【武力】。书加智力/技力上限/体力上限，马加速度，均不影响攻击力。


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

# ---- 物品（含装备加成）----
th = read_ini(os.path.join(DATA, "Thing.ini"))
trecs = []
item_by_name = {}
for t in th:
    if t.get("_section") != "ITEM" or "Name" not in t:
        continue
    ty = to_int(t.get("Type"))
    inc = [to_int(t.get("Increment")), to_int(t.get("Increment2")), to_int(t.get("Increment3"))]
    rec = {
        "name": t.get("Name", "").strip(),
        "type": ty,
        "slot": SLOT_OF.get(ty, "other"),
        "count": str(t.get("Count", "")),
        "level": to_int(t.get("Level")),
        "icon": t.get("Icon", ""),
        "statement": t.get("Statement", ""),
        "increment": inc,
        # 武力加成：只有武器(Type=2)计入；其余槽位不影响攻击力
        "strength_bonus": inc[0] if ty == 2 else 0,
    }
    trecs.append(rec)
    item_by_name.setdefault(rec["name"], rec)
json.dump(trecs, open(os.path.join(OUT, "things.json"), "w", encoding="utf-8"),
          ensure_ascii=False, indent=1)
summary.append(f"things.json      {len(trecs)} 件物品（含 increment 加成语义）")

# ---- 武将 ----
gens = read_ini(os.path.join(DATA, "General01.ini"))
recs = []
for g in gens:
    if g.get("_section") != "GENERAL" or "No" not in g:
        continue
    recs.append({
        "no": to_int(g.get("No")),
        "name": g.get("Name", "").strip(),
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
        # 装备槽（原版存的是物品名字符串，不是索引）
        "weapon": (g.get("Weapon") or "").strip(),
        "book": (g.get("Book") or "").strip(),
        "horse": (g.get("Horse") or "").strip(),
    })

# 野兽判定（结构化规则，与 C 侧 S3_BEAST_INTELLIGENCE_MAX / S3_BEAST_HP_MIN 完全一致）
BEAST_INT_MAX = 20
BEAST_HP_MIN = 150

def is_beast(r):
    return r["intelligence"] <= BEAST_INT_MAX and r["hp"] >= BEAST_HP_MIN

# 计算当前攻击力（基础武力 + 武器加成）+ 野兽标记
for r in recs:
    it = item_by_name.get(r["weapon"])
    r["weapon_bonus"] = it["strength_bonus"] if it else 0
    r["current_attack"] = r["strength"] + r["weapon_bonus"]
    r["is_beast"] = is_beast(r)

json.dump(recs, open(os.path.join(OUT, "generals.json"), "w", encoding="utf-8"),
          ensure_ascii=False, indent=1)
summary.append(f"generals.json    {len(recs)} 名武将（含装备槽与 current_attack）")

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

# =================================================================
# 第 4 项需求：必杀技学习 —— 运行时判定规则 + 验证用基准名单
# =================================================================
# 野兽判定（结构化规则，不用名字黑名单）：
#   (智力 <= 20 且 HP >= 150) 恰好命中 4 只，零误伤；黃巾頭目/黃巾將軍 落在人形一侧。
# 野兽判定函数与常量在文件前部（generals 字段填充之后）定义，
# 此处直接复用，避免在 Python 源里出现两份语义相同、容易失同步的常量。

# 规则常量（引擎按此实现）
RULE = {
    "rule_id": "super_attack_learn",
    "trigger": "on_attack_changed",  # 攻击力变化时（装备变更 / 入队 / 其他加成来源）
    "condition": {
        "current_attack_gte": 80,
        "exclude_beast": True,
        "not_already_known": True,
    },
    "formula": "current_attack = base_strength + weapon.strength_bonus",
    "notes": [
        "武器可随时更换，因此判定必须运行时进行，不能用开局静态名单。",
        "已学会的必杀技在攻击力回落后【不收回】（学习是历史事件）——待用户确认。",
        "书(智力/技力上限/体力上限)与马(速度)均不影响攻击力，不参与判定。",
    ],
}

# 名单分三类（仅用于验证与回归测试）
A = [r for r in recs if r["strength"] >= 80 and not r["super_attack"] and not is_beast(r)]
B = [r for r in recs if r["strength"] < 80 and r["current_attack"] >= 80
     and not r["super_attack"] and not is_beast(r)]
MAX_WEAPON_BONUS = max((i["strength_bonus"] for i in trecs), default=0)
C = [r for r in recs if r["strength"] < 80 and r["current_attack"] < 80
     and r["strength"] + MAX_WEAPON_BONUS >= 80 and not r["super_attack"] and not is_beast(r)]
beasts = [r for r in recs if is_beast(r)]
with_sa = [r for r in recs if r["super_attack"]]

json.dump({
    "rule": RULE,
    "beast_rule": f"intelligence <= {BEAST_INT_MAX} and hp >= {BEAST_HP_MIN}",
    "max_weapon_strength_bonus": MAX_WEAPON_BONUS,
    "counts": {
        "total_generals": len(recs),
        "already_have_super_attack": len(with_sa),
        "class_A_base_ge80_initial_ok": len(A),
        "class_B_initial_weapon_reaches_80": len(B),
        "class_C_potential_with_better_weapon": len(C),
        "beasts_excluded": len(beasts),
    },
    "class_A_base_ge80_initial_ok": [
        {"no": r["no"], "name": r["name"], "strength": r["strength"],
         "current_attack": r["current_attack"]} for r in A],
    "class_B_initial_weapon_reaches_80": [
        {"no": r["no"], "name": r["name"], "base": r["strength"],
         "weapon": r["weapon"], "bonus": r["weapon_bonus"],
         "current_attack": r["current_attack"]} for r in B],
    "class_C_potential_with_better_weapon": [
        {"no": r["no"], "name": r["name"], "strength": r["strength"]} for r in C],
    "excluded_beasts": [
        {"no": r["no"], "name": r["name"], "strength": r["strength"],
         "intelligence": r["intelligence"], "hp": r["hp"]} for r in beasts],
}, open(os.path.join(OUT, "auto_superattack_targets.json"), "w", encoding="utf-8"),
    ensure_ascii=False, indent=1)
summary.append(
    f"auto_superattack_targets.json  规则(当前攻击力>=80) + 基准名单 A{len(A)}/B{len(B)}/C{len(C)} + 野兽{len(beasts)}只")

# 数据一致性自检：是否存在"武力 < 80 却有必杀技"的武将（用来校准规则是否还有别的门槛）
weird = [r for r in with_sa if r["strength"] < 80]
print("=== 数据字典导出完成 ===")
for s in summary:
    print("  " + s)
print()
print(f"输出目录: {OUT}")

print()
print("--- 校验：已有必杀技但基础武力 < 80 的武将（说明判定不止看武力）---")
if weird:
    for r in weird[:40]:
        print(f"    {r['name']:<8} 武力={r['strength']:<4} 当前攻击={r['current_attack']:<4} "
              f"必杀技={r['super_attack']}  (开局预设，非自动习得)")
    print(f"    共 {len(weird)} 名")
else:
    print("    （无）")

print()
print(f"--- A 类：基础武力>=80 且无必杀技，开局即达标（{len(A)} 名）---")
print("  " + "、".join(f'{r["name"]}({r["strength"]})' for r in A))
print()
print(f"--- B 类：基础<80，靠【初始自带武器】顶到>=80（{len(B)} 名）---")
print("  " + ("（无）" if not B else
              "、".join(f'{r["name"]}({r["strength"]}+{r["weapon_bonus"]}={r["current_attack"]})'
                        for r in B)))
print()
print(f"--- C 类：换更强武器后可跨过 80 —— 证明判定必须运行时进行（{len(C)} 名）---")
print("  " + "、".join(f'{r["name"]}({r["strength"]})' for r in C[:50]) + " ...")
print()
print(f"--- 武器武力加成上限 = +{MAX_WEAPON_BONUS}（因此基础武力 >= "
      f"{80 - MAX_WEAPON_BONUS} 的武将理论上都能跨过 80）---")
print()
print(f"--- 野兽（智<={BEAST_INT_MAX} 且 HP>={BEAST_HP_MIN}，排除）（{len(beasts)} 只）---")
print("  " + "、".join(f'{r["name"]}(智{r["intelligence"]}/HP{r["hp"]})' for r in beasts))
print()
print("--- 8 种必杀技使用统计 ---")
from collections import Counter
c = Counter(a for r in recs for a in r["super_attack"])
for k in sorted(c):
    print(f"   {k}: {c[k]} 人")
