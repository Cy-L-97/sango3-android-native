# -*- coding: utf-8 -*-
"""把 C 引擎的解析结果与 Python 基准（.workbuddy/data/json）逐字段比对。

为什么需要它：
  「C 能读出 421 个武将」不等于「C 读出的内容正确」。
  本脚本做**字段级**对照，任何一处差异都会指出记录与字段名。

依赖：
  先构建并运行 sango3data（见 tools/verify_data_c.py 的 main 会自动执行）：
    build/pc/bin/sango3data.exe .workbuddy/data/Setting build/pc/out engine/assets/encoding
"""
import json
import os
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
PY = sys.executable
JSOND = os.path.join(ROOT, ".workbuddy", "data", "json")
OUT = os.path.join(ROOT, "build", "pc", "out")
DUMP = os.path.join(OUT, "gamedata_dump.txt")
SUMMARY = os.path.join(OUT, "gamedata_summary.txt")
EXE = os.path.join(ROOT, "build", "pc", "bin", "sango3data.exe")

SLOT_NAMES = ["other", "soldier_token", "weapon", "book", "horse",
              "formation", "treasure", "herb", "fungus"]

# C 侧缓冲区上限（见 engine/src/gamedata.h），用于检测截断风险
CAP_NAME, CAP_TEXT = 64, 256

fails = []
checks = [0]


def eq(ctx, field, got, want):
    checks[0] += 1
    if got != want:
        fails.append(f"{ctx} :: {field}\n     C   = {got!r}\n     py  = {want!r}")


def load_json(name):
    with open(os.path.join(JSOND, name), encoding="utf-8") as f:
        return json.load(f)


def run_probe():
    if not os.path.isfile(EXE):
        print(f"✗ 找不到 {EXE}，请先跑 python tools/build_pc.py")
        return False
    os.makedirs(OUT, exist_ok=True)
    r = subprocess.run([EXE, ".workbuddy/data/Setting", "build/pc/out", "engine/assets/encoding"],
                       cwd=ROOT, capture_output=True)
    print("  " + (r.stdout.decode("utf-8", "replace").strip() or "(no stdout)"))
    if r.returncode != 0:
        print(f"✗ sango3data 退出码 {r.returncode}: " + r.stderr.decode("utf-8", "replace"))
        return False
    return True


def parse_dump():
    recs = {"G": [], "I": [], "S": [], "M": [], "R": []}
    with open(DUMP, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            recs[parts[0]].append(parts[1:])
    return recs


def main():
    print("=" * 76)
    print("0) 运行 C 验证器")
    print("=" * 76)
    if not run_probe():
        return 1
    recs = parse_dump()

    print()
    print("=" * 76)
    print("1) 数量与基础统计")
    print("=" * 76)
    gens = load_json("generals.json")
    things = load_json("things.json")
    solds = load_json("soldiers.json")
    magics = load_json("bfmagic.json")
    base = load_json("auto_superattack_targets.json")
    rules = load_json("rules.json")
    lang = load_json("lang_hant2hans.json")["map"]

    eq("counts", "generals", len(recs["G"]), len(gens))
    eq("counts", "items", len(recs["I"]), len(things))
    eq("counts", "soldiers", len(recs["S"]), len(solds))
    eq("counts", "magics", len(recs["M"]), len(magics))
    print(f"  C: G={len(recs['G'])} I={len(recs['I'])} S={len(recs['S'])} "
          f"M={len(recs['M'])} R={len(recs['R'])}")
    print(f"  py: G={len(gens)} I={len(things)} S={len(solds)} M={len(magics)}")

    # ------------------------------------------------------------ 武将
    print()
    print("=" * 76)
    print("2) 武将逐字段比对（含装备、当前攻击力、必杀技、简中显示名）")
    print("=" * 76)
    if len(recs["G"]) != len(gens):
        print(f"  ✗ 数量不同，跳过字段比对")
    else:
        for c, p in zip(recs["G"], gens):
            # 列顺序与 gamedata_probe.c write_dump 一致（去掉行首 "G" 后）：
            # no name display strength int hp mp weapon wbonus catk sa stype is_beast rank sex wtype portrait
            ctx = f"武将#{p['no']} {p['name']}"
            eq(ctx, "no", int(c[0]), p["no"])
            eq(ctx, "name", c[1], p["name"])
            eq(ctx, "display(简中)", c[2], "".join(lang.get(ch, ch) for ch in p["name"]))
            eq(ctx, "strength", int(c[3]), p["strength"])
            eq(ctx, "intelligence", int(c[4]), p["intelligence"])
            eq(ctx, "hp", int(c[5]), p["hp"])
            eq(ctx, "mp", int(c[6]), p["mp"])
            eq(ctx, "weapon", c[7], p["weapon"])
            eq(ctx, "weapon_bonus", int(c[8]), p["weapon_bonus"])
            eq(ctx, "current_attack", int(c[9]), p["current_attack"])
            eq(ctx, "super_attack", [int(x) for x in c[10].split(",") if x], p["super_attack"])
            eq(ctx, "soldier_type", [int(x) for x in c[11].split(",") if x], p["soldier_type"])
            eq(ctx, "is_beast", int(c[12]), bool(p["is_beast"]))
            eq(ctx, "rank", int(c[13]), p["rank"])
            eq(ctx, "sex", int(c[14]), p["sex"])
            eq(ctx, "weapon_type", int(c[15]), p["weapon_type"])
            eq(ctx, "portrait", int(c[16]), p["portrait"])
        print(f"  ✓ 比对 {len(gens)} 名武将，共 {checks[0]} 项检查")

    # ------------------------------------------------------------ 物品
    print()
    print("=" * 76)
    print("3) 物品逐字段比对（装备加成是需求④的判定依据）")
    print("=" * 76)
    if len(recs["I"]) == len(things):
        for c, p in zip(recs["I"], things):
            ctx = f"物品 {p['name']}"
            eq(ctx, "name", c[0], p["name"])
            eq(ctx, "display(简中)", c[1], "".join(lang.get(ch, ch) for ch in p["name"]))
            eq(ctx, "type", int(c[2]), p["type"])
            eq(ctx, "slot", SLOT_NAMES[int(c[3])], p["slot"])
            eq(ctx, "level", int(c[4]), p["level"])
            eq(ctx, "increment", [int(c[5]), int(c[6]), int(c[7])], p["increment"])
            eq(ctx, "strength_bonus", int(c[8]), p["strength_bonus"])
            eq(ctx, "count", c[9], str(p["count"]))
        print(f"  ✓ 比对 {len(things)} 件物品")
    else:
        eq("counts", "items", len(recs["I"]), len(things))

    # ------------------------------------------------------------ 兵种
    print()
    print("=" * 76)
    print("4) 兵种逐字段比对（需求②的士兵上限挂在兵种体系上）")
    print("=" * 76)
    if len(recs["S"]) == len(solds):
        for c, p in zip(recs["S"], solds):
            ctx = f"兵种#{p['no']} {p['name']}"
            eq(ctx, "no", int(c[0]), p["no"])
            eq(ctx, "name", c[1], p["name"])
            eq(ctx, "name_adv", c[2], p["name_adv"])
            eq(ctx, "res_id", int(c[3]), p["res_id"])
            eq(ctx, "start_hp", int(c[4]), p["start_hp"])
            eq(ctx, "add_hp", int(c[5]), p["add_hp"])
            eq(ctx, "start_power", int(c[6]), p["start_power"])
            eq(ctx, "add_power", int(c[7]), p["add_power"])
            eq(ctx, "hit_rate", [int(x) for x in c[8].split(",") if x], p["hit_rate"])
        print(f"  ✓ 比对 {len(solds)} 个兵种")
    else:
        eq("counts", "soldiers", len(recs["S"]), len(solds))

    # ------------------------------------------------------------ 武将技
    print()
    print("=" * 76)
    print("5) 武将技逐字段比对")
    print("=" * 76)
    if len(recs["M"]) == len(magics):
        for c, p in zip(recs["M"], magics):
            ctx = f"武将技#{p['no']} {p['name']}"
            eq(ctx, "no", int(c[0]), p["no"])
            eq(ctx, "name", c[1], p["name"])
            eq(ctx, "mp", int(c[2]), p["mp"])
            eq(ctx, "power", int(c[3]), p["power"])
            eq(ctx, "level", int(c[4]), p["level"])
            eq(ctx, "contribution", int(c[5]), p["contribution"])
            eq(ctx, "attribute", int(c[6]), p["attribute"])
        print(f"  ✓ 比对 {len(magics)} 个武将技")
    else:
        eq("counts", "magics", len(recs["M"]), len(magics))

    # ------------------------------------------------------------ 规则表
    print()
    print("=" * 76)
    print("6) Game.ini 规则表比对（需求① 升级经验曲线）")
    print("=" * 76)
    c_rules = {}
    for r in recs["R"]:
        c_rules.setdefault(r[0], {})[r[1]] = int(r[2])
    for sec in ("GENERALEXP", "SOLDIEREXP", "COST"):
        if sec not in rules:
            continue
        py_sec = {k: int(v) for k, v in rules[sec].items()}
        eq(f"规则 {sec}", "键数量", len(c_rules.get(sec, {})), len(py_sec))
        for k, v in py_sec.items():
            eq(f"规则 {sec}", k, c_rules.get(sec, {}).get(k), v)
        print(f"  ✓ {sec}: {len(py_sec)} 个键")

    # ------------------------------------------------------- 派生统计与规则自测
    print()
    print("=" * 76)
    print("7) 派生统计 + 需求③④ 规则自测")
    print("=" * 76)
    sm = {}
    with open(SUMMARY, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            k, _, v = line.partition("=")
            sm[k] = v
    cnt = base["counts"]
    eq("统计", "counts.with_super_attack", int(sm["counts.with_super_attack"]),
       cnt["already_have_super_attack"])
    eq("统计", "counts.beasts", int(sm["counts.beasts"]), cnt["beasts_excluded"])
    eq("统计", "class_A", int(sm["class_A_base_ge80"]), cnt["class_A_base_ge80_initial_ok"])
    eq("统计", "class_B", int(sm["class_B_initial_weapon"]), cnt["class_B_initial_weapon_reaches_80"])
    eq("统计", "class_C", int(sm["class_C_potential"]), cnt["class_C_potential_with_better_weapon"])
    eq("统计", "max_weapon_bonus", int(sm["max_weapon_strength_bonus"]),
       base["max_weapon_strength_bonus"])

    print(f"  武将总数 {sm['counts.generals']} / 自带必杀技 {sm['counts.with_super_attack']} "
          f"/ 野兽 {sm['counts.beasts']}")
    print(f"  A(基础≥80)={sm['class_A_base_ge80']}  B(初始武器达标)={sm['class_B_initial_weapon']}  "
          f"C(换武器可达标)={sm['class_C_potential']}")

    # 规则自测断言（这些是"行为"而非"数据"）
    eq("规则④", "初始授予数 == A类人数", int(sm["test.initial_grants"]), int(sm["class_A_base_ge80"]))
    eq("规则④", "授予给野兽的数量", int(sm["test.granted_to_beast"]), 0)
    eq("规则④", "授予给未达标者的数量", int(sm["test.granted_below_threshold"]), 0)
    eq("规则④", "回落撤销数（必须 0 = monotonic）", int(sm["test.monotonic_revoked"]), 0)
    eq("规则④", "C 类换最强武器后学会数", int(sm["test.class_c_best_weapon_learned"]),
       int(sm["test.class_c_best_weapon_total"]))
    eq("需求③", "单将最大携带数（无 3 招上限）", int(sm["test.carry_capacity"]), 8)
    eq("需求②", "士兵上限", int(sm["soldier_limit"]), 1200)   # 2026-09-16 用户复核：1000 → 1200

    # ------------------------------------------------------------ 截断风险
    print()
    print("=" * 76)
    print("8) 缓冲区截断风险（C 侧定长字段）")
    print("=" * 76)
    worst_name = max((len(g["name"].encode()) for g in gens), default=0)
    worst_item = max((len(t["name"].encode()) for t in things), default=0)
    worst_stmt = max((len(t["statement"].encode()) for t in things), default=0)
    worst_spec = max((len(m["spec"].encode()) for m in magics), default=0)
    print(f"  最长武将名 {worst_name}B / 物品名 {worst_item}B（上限 {CAP_NAME}B）")
    print(f"  最长物品说明 {worst_stmt}B / 武将技 Spec {worst_spec}B（上限 {CAP_TEXT}B）")
    for label, val, cap in (("武将名", worst_name, CAP_NAME), ("物品名", worst_item, CAP_NAME),
                            ("物品说明", worst_stmt, CAP_TEXT), ("Spec", worst_spec, CAP_TEXT)):
        checks[0] += 1
        if val + 1 > cap:
            fails.append(f"缓冲区 :: {label} 需 {val}B+1 > 上限 {cap}B —— 会被截断，必须调大")

    # ------------------------------------------------------------ 结论
    print()
    print("=" * 76)
    if fails:
        print(f"✗ 发现 {len(fails)} 处不一致（共 {checks[0]} 项检查）：")
        for x in fails[:40]:
            print("  " + x)
        if len(fails) > 40:
            print(f"  … 另有 {len(fails)-40} 处")
        return 1
    print(f"✓ 全部通过：{checks[0]} 项字段级检查，C 引擎与 Python 基准完全一致")
    return 0


if __name__ == "__main__":
    sys.exit(main())
