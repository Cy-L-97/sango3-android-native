# -*- coding: utf-8 -*-
"""
build_ui.py —— UI 布局 Python 基准（M2）

与 C 侧 engine/src/ui.c **独立实现、同语义**：
  同样从 PAK 取 Setting\\Menu.ini，同样做字节级 #include 展开，同样 Big5→UTF-8，
  同样「先解码再切行」，同样处理行内注释 / 重复键 / 未知 Style token / 非数字四态值。
两侧结果必须逐字段一致（由 tools/verify_ui_c.py 把关）。

素材优先级：Update.PAK > Sango3.PAK（原版 Update 包会覆盖同名文件，
这与 .workbuddy/data/Setting 里已提取的 Menu.ini（121105 字节 = Update 版）口径一致）。

用法:
  python tools/build_ui.py [--pak <pak>] [--out <json>]
    --pak  可重复，指定 PAK 路径（默认自动定位游戏目录）
    --out  输出 json（默认 .workbuddy/data/json/ui.json）
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding="utf-8")
from pak import read_index  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_JSON = os.path.join(ROOT, ".workbuddy", "data", "json", "ui.json")

# 游戏安装目录候选（与换机开工清单一致：两机装同一路径最省事）
GAME_DIR_CANDIDATES = [
    os.environ.get("SANGO3_DIR", ""),
    r"E:\Program Files (x86)\steam\steamapps\common\Sango3",
    r"D:\Program Files (x86)\steam\steamapps\common\Sango3",
    r"C:\Program Files (x86)\steam\steamapps\common\Sango3",
    r"F:\leidian\..\Sango3",
]

# 优先级：Update 覆盖 Sango3
PAK_PRIORITY = ["Update.PAK", "Sango3.PAK"]

ENTRY = r"Setting\Menu.ini"

# ---------------------------------------------------------------- 语义常量
CLASSES = [
    "WND_CLASS_BASE", "WND_CLASS_BUTTON", "WND_CLASS_STATIC", "WND_CLASS_LIST",
    "WND_CLASS_SCROLLBAR", "WND_CLASS_MSTATIC", "WND_CLASS_BUTTONREPORT",
    "WND_CLASS_PROGRESS_BFHP", "WND_CLASS_PROGRESS", "WND_CLASS_PROGRESSEX",
    "WND_CLASS_BFRADAR", "WND_CLASS_TIMER", "WND_CLASS_BFMESSAGE",
]

# 位值必须与 engine/src/ui.h 的 S3_WS_* 完全一致（顺序无关，按名字取）
STYLE_BITS = {
    "wsVisible": 0x00000001, "wsIcon": 0x00000002, "wsVCenter": 0x00000004,
    "wsHCenter": 0x00000008, "wsText": 0x00000010, "wsCheck": 0x00000020,
    "wsVScroll": 0x00000040, "wsHScroll": 0x00000080, "wsSCheck": 0x00000100,
    "wsRight": 0x00000200, "wsLeft": 0x00000400, "wsHorizontal": 0x00000800,
    "wsTrans": 0x00001000, "wsReport": 0x00002000,
    "wsForceSelectChange": 0x00004000, "wsRight2Left": 0x00008000,
    "wsLeft2Right": 0x00010000,
}
# 原版笔误：wcIcon 应作 wsIcon，出现十几次，必须一并识别
STYLE_ALIAS = {"wcIcon": "wsIcon"}


# ---------------------------------------------------------------- PAK 访问
class PakSet:
    """多个 PAK 按优先级构成一个虚拟文件系统（后者覆盖前者）。"""

    def __init__(self, paths):
        self.tables = []          # [(path, {lower_name: (off, size)})]
        for p in paths:
            entries, _, _, _ = read_index(p)
            tab = {}
            for name, off, size in entries:
                tab[name.replace("/", "\\").lower()] = (off, size)
            self.tables.append((p, tab))

    def get(self, name):
        key = name.replace("/", "\\").lower()
        # 逆序查找：优先级高的在后面（调用方按 [Sango3, Update] 传入）
        for path, tab in reversed(self.tables):
            if key in tab:
                off, size = tab[key]
                with open(path, "rb") as f:
                    f.seek(off)
                    return f.read(size)
        # 退化为子串包含
        for path, tab in reversed(self.tables):
            for k, (off, size) in tab.items():
                if key in k:
                    with open(path, "rb") as f:
                        f.seek(off)
                        return f.read(size)
        return None


def resolve_paks(explicit):
    if explicit:
        return explicit
    for d in GAME_DIR_CANDIDATES:
        if not d or not os.path.isdir(d):
            continue
        hits = [os.path.join(d, n) for n in PAK_PRIORITY if os.path.isfile(os.path.join(d, n))]
        if hits:
            # 低优先级在前（Sango3 先，Update 后覆盖）
            return sorted(hits, key=lambda p: PAK_PRIORITY.index(os.path.basename(p)) * -1)
    return []


# ---------------------------------------------------------------- #include 展开
def expand_includes(data, getter, base_dir, depth=0):
    """字节级递归展开 #include（与 C 的 expand_includes 同口径）。"""
    if depth >= 8:
        return data
    lines = data.split(b"\n")
    out = bytearray()
    for i, line in enumerate(lines):
        s = line.lstrip(b" \t\r")
        if s.startswith(b"#include") and len(s) > len(b"#include"):
            rest = s[len(b"#include"):].strip(b" \t\r")
            if rest.startswith(b'"'):
                rest = rest[1:]
                if rest.endswith(b'"'):
                    rest = rest[:-1]
            name = rest.decode("ascii", "replace").strip()
            if name:
                path = name if ("\\" in name or "/" in name) else base_dir + name
                sub = getter(path)
                if sub:
                    out += expand_includes(sub, getter, base_dir, depth + 1)
                    if i < len(lines) - 1:
                        out += b"\n"
                    continue
        out += line
        if i < len(lines) - 1:
            out += b"\n"
    return bytes(out)


# ---------------------------------------------------------------- INI 解析
def parse_ini(text):
    """返回 [(section_name, [(key, [vals...])])]。语义与 engine/src/ini.c 一致。"""
    sections = []
    cur = None
    for raw in text.split("\n"):
        line = raw.strip()
        if not line or line[0] in ";#":
            continue
        if line.startswith("[") and line.endswith("]"):
            cur = (line[1:-1], [])
            sections.append(cur)
            continue
        if "=" not in line or cur is None:
            continue
        k, v = line.split("=", 1)
        k, v = k.strip(), v.strip()
        if not k:
            continue
        for i, (kk, vals) in enumerate(cur[1]):
            if kk == k:
                vals.append(v)
                break
        else:
            cur[1].append((k, [v]))
    return sections


def strip_inline(v):
    """去掉行内注释（';' / '#' 之后）并去首尾空白——与 ui.c 的 strip_inline 同口径。"""
    if v is None:
        return ""
    for i, ch in enumerate(v):
        if ch in ";#":
            v = v[:i]
            break
    return v.strip(" \t\r")


def _is_digits(s):
    """只认 ASCII 0-9（C 侧同样只认 '0'..'9'；str.isdigit 会把全角/上标数字也算进去）。"""
    return bool(s) and all("0" <= c <= "9" for c in s)


def to_int(s, default):
    """严格整数：去空白后必须整体是十进制整数，否则返回 default。"""
    if s is None:
        return default
    t = s.strip()
    if not t:
        return default
    body = t[1:] if t[0] in "+-" else t
    if not _is_digits(body):
        return default
    try:
        return int(t)
    except ValueError:
        return default


def parse_rect(v):
    """'x, y, w, h' → (x,y,w,h)；非恰好 4 段或尾部有残留 → (0,0,0,0)（与 C 的失败口径一致）。"""
    p = v or ""
    segs = []
    i = 0
    n = len(p)
    while len(segs) < 4:
        while i < n and p[i] in " \t":
            i += 1
        if i >= n:
            break
        sign = 1
        if p[i] in "+-":
            sign = -1 if p[i] == "-" else 1
            i += 1
        j = i
        while j < n and "0" <= p[j] <= "9":
            j += 1
        if j == i:
            return (0, 0, 0, 0)
        segs.append(sign * int(p[i:j]))
        i = j
        while i < n and p[i] in " \t":
            i += 1
        if i < n and p[i] == ",":
            i += 1
            continue
        break
    if len(segs) != 4 or i != n:
        return (0, 0, 0, 0)
    return tuple(segs)


def parse_pos(v):
    """ICON 的 'Pos = x, y'：最多取 2 段，不足补 0（与 ui.c 的内联解析同口径）。

    ⚠ 不能复用 parse_rect：它要求恰好 4 段，而 Pos 只有 2 段，
      用它会把 `Pos = -6, -6` 这类**负坐标**静默变成 (0,0)。
    """
    p = v or ""
    segs, i, n = [], 0, len(p)
    while len(segs) < 2:
        while i < n and p[i] in " \t":
            i += 1
        if i >= n:
            break
        sign = 1
        if p[i] in "+-":
            sign = -1 if p[i] == "-" else 1
            i += 1
        j = i
        while j < n and "0" <= p[j] <= "9":
            j += 1
        if j == i:
            break
        segs.append(sign * int(p[i:j]))
        i = j
        while i < n and p[i] in " \t":
            i += 1
        if i < n and p[i] == ",":
            i += 1
            continue
        break
    return [segs[0] if len(segs) > 0 else 0, segs[1] if len(segs) > 1 else 0]


def parse_state(v):
    """四态通用：全数字则记段数；否则 n_seg=0 只保留原串（如 'Normal'）。"""
    raw = v or ""
    segs = []
    i, n = 0, len(raw)
    while len(segs) < 4:
        while i < n and raw[i] in " \t":
            i += 1
        if i >= n:
            break
        sign = 1
        if raw[i] in "+-":
            sign = -1 if raw[i] == "-" else 1
            i += 1
        j = i
        while j < n and "0" <= raw[j] <= "9":
            j += 1
        if j == i:
            break                       # 非数字 → n_seg=0
        segs.append(sign * int(raw[i:j]))
        i = j
        while i < n and raw[i] in " \t":
            i += 1
        if i < n and raw[i] == ",":
            i += 1
            continue
        break
    if i != n:
        segs = []                       # 尾部有残留 → 视为非数字串
    vals = [segs[k] if k < len(segs) else 0 for k in range(4)]
    return {"raw": raw, "n_seg": len(segs), "v": vals}


def parse_style(v):
    m = 0
    for tok in strip_inline(v).split(","):
        t = tok.strip()
        if not t:
            continue
        t = STYLE_ALIAS.get(t, t)
        m |= STYLE_BITS.get(t, 0)       # 未知 token 忽略（原版有脏数据）
    return m


def parse_class(v):
    try:
        return CLASSES.index(strip_inline(v))
    except ValueError:
        return 99


# ---------------------------------------------------------------- 取值
def get1(pairs, key):
    for k, vals in pairs:
        if k == key:
            return strip_inline(vals[0])
    return ""


def getall(pairs, key):
    for k, vals in pairs:
        if k == key:
            return [strip_inline(x) for x in vals]
    return []


def geti(pairs, key, default):
    v = get1(pairs, key)
    return default if v == "" else to_int(v, default)


def getopt(pairs, key):
    v = get1(pairs, key)
    return v if v else None


# ---------------------------------------------------------------- 主流程
def build(ps):
    entry_bytes = ps.get(ENTRY)
    if entry_bytes is None:
        raise SystemExit(f"PAK 中找不到条目：{ENTRY}")
    base_dir = ENTRY.rsplit("\\", 1)[0] + "\\"
    raw = expand_includes(entry_bytes, ps.get, base_dir)
    text = raw.decode("cp950", "replace")
    sections = parse_ini(text)

    windows, icons, colors = [], [], []
    for name, pairs in sections:
        if name == "WINDOW":
            w = {
                "id": geti(pairs, "ID", 0),
                "range": list(parse_rect(get1(pairs, "Range"))),
                "work_range": list(parse_rect(get1(pairs, "WorkRange"))),
                "fill_range": list(parse_rect(get1(pairs, "FillRange"))),
                "style": parse_style(get1(pairs, "Style")),
                "cls": parse_class(get1(pairs, "Class")),
                "cls_name": CLASSES[parse_class(get1(pairs, "Class"))]
                if parse_class(get1(pairs, "Class")) < len(CLASSES) else "WND_CLASS_UNKNOWN",
                "icon": geti(pairs, "Icon", -1),
                "command": geti(pairs, "Command", -1),
                "font": geti(pairs, "Font", -1),
                "fcolor": geti(pairs, "FColor", -1),
                "bcolor": geti(pairs, "BColor", -1),
                "cols": geti(pairs, "Cols", -1),
                "rows": geti(pairs, "Rows", -1),
                "lines": geti(pairs, "Lines", -1),
                "check": geti(pairs, "Check", -1),
                "title": getopt(pairs, "Title"),
                "comment": getopt(pairs, "Comment"),
                "children": [to_int(x, -1) for x in getall(pairs, "Child")],
            }
            windows.append(w)
        elif name == "ICON":
            pos = parse_pos(get1(pairs, "Pos"))
            icons.append({
                "id": geti(pairs, "ID", 0),
                "pos": pos,
                "dir": getopt(pairs, "Dir"),
                "normal": parse_state(get1(pairs, "Normal")),
                "focus": parse_state(get1(pairs, "Focus")),
                "down": parse_state(get1(pairs, "Down")),
                "disable": parse_state(get1(pairs, "Disable")),
                "comment": getopt(pairs, "Comment"),
            })
        elif name == "COLOR":
            colors.append({
                "id": geti(pairs, "ID", 0),
                "normal": parse_state(get1(pairs, "Normal")),
                "focus": parse_state(get1(pairs, "Focus")),
                "down": parse_state(get1(pairs, "Down")),
                "disable": parse_state(get1(pairs, "Disable")),
                "comment": getopt(pairs, "Comment"),
            })

    return {
        "source": {"entry": ENTRY, "bytes": len(entry_bytes)},
        "counts": {"windows": len(windows), "icons": len(icons), "colors": len(colors)},
        "windows": windows, "icons": icons, "colors": colors,
    }, len(sections)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pak", action="append", default=[], help="PAK 路径，可重复")
    ap.add_argument("--out", default=OUT_JSON)
    a = ap.parse_args()

    paks = resolve_paks(a.pak)
    if not paks:
        print("✗ 未定位到游戏 PAK。用 --pak 指定，或设置环境变量 SANGO3_DIR。")
        return 2
    print("PAK 优先级（后者覆盖前者）：")
    for p in paks:
        print(f"  {p}")

    ps = PakSet(paks)
    data, n_sec = build(ps)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
    c = data["counts"]
    print(f"✓ 写出 {a.out}")
    print(f"  windows={c['windows']} icons={c['icons']} colors={c['colors']} "
          f"sections={n_sec}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
