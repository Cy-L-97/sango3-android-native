# -*- coding: utf-8 -*-
"""
verify_ui_c.py —— UI 布局「C ↔ Python」字段级对照（M2）

为什么需要它：
  「C 读出 325 个控件」不等于「C 读出的内容正确」。
  本脚本把 sango3ui 导出的 ui_dump.tsv 与 Python 基准 ui.json **逐字段**比对，
  任何差异都会指出记录与字段名。

列顺序约定（与 engine/src/ui_probe.c write_dump 一一对应）：
  两侧必须**锁在同一处**——C 侧在 write_dump 的 `#W/#I/#C` 头注释里写明列序，
  Python 侧在下面的 W_COLS / I_COLS / C_COLS 里按同名顺序取值。
  任何一侧增减字段而不同步，就会静默错位（M1-a 踩过这个坑）。

用法:
  python tools/verify_ui_c.py
"""
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding="utf-8")
from build_ui import resolve_paks  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSON = os.path.join(ROOT, ".workbuddy", "data", "json", "ui.json")
OUT = os.path.join(ROOT, "build", "pc", "out")
DUMP = os.path.join(OUT, "ui_dump.tsv")
EXE = os.path.join(ROOT, "build", "pc", "bin", "sango3ui.exe")

fails = []
checks = [0]


def eq(ctx, field, got, want):
    checks[0] += 1
    if got != want:
        fails.append(f"{ctx} :: {field}\n     C   = {got!r}\n     py  = {want!r}")


# ------------------------------------------------------------ 列序（与 C 侧锁死）
# 去掉行首 "W" 之后的下标
ID, RX, RY, RW, RH = 0, 1, 2, 3, 4
WX, WY, WW, WH = 5, 6, 7, 8
FX, FY, FW, FH = 9, 10, 11, 12
STYLE, CLS, CLSNAME = 13, 14, 15
ICON, COMMAND, FONT, FCOLOR, BCOLOR = 16, 17, 18, 19, 20
COLS, ROWS, LINES, CHECK = 21, 22, 23, 24
TITLE, COMMENT, NCHILD, CHILDREN = 25, 26, 27, 28
W_NCOL = 29

# 四态块：raw, n_seg, v0+v1+v2+v3
def state_cols(base):
    return base, base + 1, base + 2


def _nul(s):
    return None if s == "-" or s == "" else s


def esc(v):
    """与 ui_probe.c 的 tsv() **同一套** C 风格转义（空/NULL → '-'）。

    为什么不是「把控制字符换成空格」：原版数据里真的有制表符，
    换成空格会让两侧都「看起来对上」但值其实被改写过——那正是要防的事。
    """
    if v is None or v == "":
        return "-"
    out = []
    for ch in v:
        if ch == "\\":
            out.append("\\\\")
        elif ch == "\t":
            out.append("\\t")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ord(ch) < 0x20:
            out.append("\\x%02X" % ord(ch))
        else:
            out.append(ch)
    return "".join(out) or "-"


def _state(cols, base, tag):
    """C 侧三列 → 与 Python 基准同构的 dict"""
    b_raw, b_seg, b_v = state_cols(base)
    raw = _nul(cols[b_raw]) or ""
    return {
        "raw": raw,
        "n_seg": int(cols[b_seg]),
        "v": [int(x) for x in cols[b_v].split("+")],
    }


def run_probe(paks):
    if not os.path.isfile(EXE):
        print(f"✗ 找不到 {EXE}，请先跑 python tools/build_pc.py")
        return False
    os.makedirs(OUT, exist_ok=True)
    cmd = [EXE, OUT] + paks
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True)
    print("  " + (r.stdout.decode("utf-8", "replace").strip() or "(no stdout)"))
    if r.returncode != 0:
        print(f"✗ sango3ui 退出码 {r.returncode}: " + r.stderr.decode("utf-8", "replace"))
        return False
    return True


def parse_dump():
    recs = {"W": [], "I": [], "C": []}
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
    print("0) 运行 C 验证器（PAK 优先级与 Python 基准一致：Update 覆盖 Sango3）")
    print("=" * 76)
    paks = resolve_paks([])
    if not paks:
        print("✗ 未定位到游戏 PAK，请设置 SANGO3_DIR 或先跑 tools/build_ui.py --pak …")
        return 2
    for p in paks:
        print(f"  pak: {p}")
    if not run_probe(paks):
        return 1

    with open(JSON, encoding="utf-8") as f:
        base = json.load(f)
    recs = parse_dump()

    print()
    print("=" * 76)
    print("1) 数量")
    print("=" * 76)
    eq("counts", "windows", len(recs["W"]), base["counts"]["windows"])
    eq("counts", "icons", len(recs["I"]), base["counts"]["icons"])
    eq("counts", "colors", len(recs["C"]), base["counts"]["colors"])
    print(f"  C : W={len(recs['W'])} I={len(recs['I'])} C={len(recs['C'])}")
    print(f"  py: W={base['counts']['windows']} I={base['counts']['icons']} "
          f"C={base['counts']['colors']}")

    # ------------------------------------------------------------ 控件
    print()
    print("=" * 76)
    print("2) WINDOW 逐字段比对")
    print("=" * 76)
    if len(recs["W"]) != len(base["windows"]):
        print("  ✗ 数量不同，跳过字段比对")
    else:
        for c, p in zip(recs["W"], base["windows"]):
            if len(c) != W_NCOL:
                fails.append(f"列数不符：期望 {W_NCOL} 列，实际 {len(c)} —— 列序可能已不同步")
                break
            ctx = f"WINDOW#{p['id']}"
            eq(ctx, "id", int(c[ID]), p["id"])
            eq(ctx, "range", [int(c[RX]), int(c[RY]), int(c[RW]), int(c[RH])], p["range"])
            eq(ctx, "work_range", [int(c[WX]), int(c[WY]), int(c[WW]), int(c[WH])],
               p["work_range"])
            eq(ctx, "fill_range", [int(c[FX]), int(c[FY]), int(c[FW]), int(c[FH])],
               p["fill_range"])
            eq(ctx, "style", int(c[STYLE]), p["style"])
            eq(ctx, "class", int(c[CLS]), p["cls"])
            eq(ctx, "class_name", c[CLSNAME], p["cls_name"])
            eq(ctx, "icon", int(c[ICON]), p["icon"])
            eq(ctx, "command", int(c[COMMAND]), p["command"])
            eq(ctx, "font", int(c[FONT]), p["font"])
            eq(ctx, "fcolor", int(c[FCOLOR]), p["fcolor"])
            eq(ctx, "bcolor", int(c[BCOLOR]), p["bcolor"])
            eq(ctx, "cols", int(c[COLS]), p["cols"])
            eq(ctx, "rows", int(c[ROWS]), p["rows"])
            eq(ctx, "lines", int(c[LINES]), p["lines"])
            eq(ctx, "check", int(c[CHECK]), p["check"])
            eq(ctx, "title", c[TITLE], esc(p["title"]))
            eq(ctx, "comment", c[COMMENT], esc(p["comment"]))
            eq(ctx, "children",
               [int(x) for x in c[CHILDREN].split(",") if x not in ("", "-")],
               p["children"])
        else:
            print(f"  ✓ 比对 {len(base['windows'])} 个控件")

    # ------------------------------------------------------------ 图标
    print()
    print("=" * 76)
    print("3) ICON 逐字段比对（四态素材名 + 目录）")
    print("=" * 76)
    if len(recs["I"]) != len(base["icons"]):
        eq("counts", "icons", len(recs["I"]), len(base["icons"]))
    else:
        for c, p in zip(recs["I"], base["icons"]):
            ctx = f"ICON#{p['id']}"
            eq(ctx, "id", int(c[0]), p["id"])
            eq(ctx, "pos", [int(c[1]), int(c[2])], p["pos"])
            eq(ctx, "dir", c[3], esc(p["dir"]))
            for i, tag in enumerate(("normal", "focus", "down", "disable")):
                b = 4 + i * 3
                st = _state(c, b, tag)
                eq(ctx, f"{tag}.raw", st["raw"], esc(p[tag]["raw"]))
                eq(ctx, f"{tag}.n_seg", st["n_seg"], p[tag]["n_seg"])
                eq(ctx, f"{tag}.v", st["v"], p[tag]["v"])
            eq(ctx, "comment", c[16], esc(p["comment"]))
        print(f"  ✓ 比对 {len(base['icons'])} 个图标")

    # ------------------------------------------------------------ 配色
    print()
    print("=" * 76)
    print("4) COLOR 逐字段比对（四态可能是 '21,89,210,1'，也可能是 'Normal'）")
    print("=" * 76)
    if len(recs["C"]) != len(base["colors"]):
        eq("counts", "colors", len(recs["C"]), len(base["colors"]))
    else:
        for c, p in zip(recs["C"], base["colors"]):
            ctx = f"COLOR#{p['id']}"
            eq(ctx, "id", int(c[0]), p["id"])
            for i, tag in enumerate(("normal", "focus", "down", "disable")):
                b = 1 + i * 3
                st = _state(c, b, tag)
                eq(ctx, f"{tag}.raw", st["raw"], esc(p[tag]["raw"]))
                eq(ctx, f"{tag}.n_seg", st["n_seg"], p[tag]["n_seg"])
                eq(ctx, f"{tag}.v", st["v"], p[tag]["v"])
            eq(ctx, "comment", c[13], esc(p["comment"]))
        print(f"  ✓ 比对 {len(base['colors'])} 组配色")

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
