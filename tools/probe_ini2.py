"""定位 5 项需求的数据锚点"""
import os, sys, re
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pak import read_index

GAME = r"E:\Program Files (x86)\steam\steamapps\common\Sango3"
DATA = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".workbuddy", "data"))

def try_read(path):
    with open(path, "rb") as f:
        raw = f.read()
    for enc in ("gbk", "big5", "utf-8", "latin1"):
        try:
            return raw.decode(enc), enc
        except UnicodeDecodeError:
            continue
    return raw.decode("latin1"), "latin1"

def find(name):
    for r, ds, fs in os.walk(DATA):
        for f in fs:
            if f.lower() == name.lower():
                return os.path.join(r, f)
    return None

print("#" * 78)
print("# 1. Game.ini 全文（全局规则，135 行）")
print("#" * 78)
p = find("Game.ini")
txt, enc = try_read(p)
for i, ln in enumerate(txt.splitlines()):
    print(f"  {i:4d}| {ln}")

print()
print("#" * 78)
print("# 2. 全 Setting 数据表中含『400』的行")
print("#" * 78)
for r, ds, fs in os.walk(DATA):
    for f in fs:
        if not f.lower().endswith(".ini"):
            continue
        p = os.path.join(r, f)
        t, _ = try_read(p)
        hits = [(i, ln) for i, ln in enumerate(t.splitlines()) if "400" in ln]
        if hits:
            print(f"  [{f}] {len(hits)} 处")
            for i, ln in hits[:8]:
                print(f"      {i:5d}| {ln.strip()[:90]}")

print()
print("#" * 78)
print("# 3. SuperAttack 字段全景（必杀技归属）")
print("#" * 78)
p = find("General01.ini")
t, _ = try_read(p)
blocks = t.split("[GENERAL]")
stat = {"total": 0, "has_sa": 0}
samples = []
str80_no_sa = 0
str80_total = 0
for b in blocks:
    if "No =" not in b:
        continue
    stat["total"] += 1
    name = re.search(r"Name\s*=\s*(.*)", b)
    sa = re.search(r"SuperAttack\s*=\s*(.*)", b)
    st = re.search(r"Strength\s*=\s*(\d+)", b)
    no = re.search(r"No\s*=\s*(\d+)", b)
    name = name.group(1).strip() if name else "?"
    sa = sa.group(1).strip() if sa else ""
    st = int(st.group(1)) if st else 0
    no = no.group(1) if no else "?"
    if sa:
        stat["has_sa"] += 1
        if len(samples) < 15:
            samples.append((no, name, st, sa))
    if st >= 80:
        str80_total += 1
        if not sa:
            str80_no_sa += 1
print(f"  武将总数={stat['total']}  有 SuperAttack={stat['has_sa']}  无={stat['total']-stat['has_sa']}")
print(f"  武力>=80 的武将={str80_total}  其中无 SuperAttack 的={str80_no_sa}")
print("  -- 有 SuperAttack 的样例 --")
for no, name, st, sa in samples:
    print(f"     No={no:<5} {name:<8} 武力={st:<4} SuperAttack={sa}")

print()
print("#" * 78)
print("# 4. Update.PAK 非 Setting 条目（找脚本/配置）")
print("#" * 78)
entries, _, _, _ = read_index(os.path.join(GAME, "Update.PAK"))
for n, o, s in entries:
    if not n.lower().startswith("setting"):
        print(f"  {s:>9,}  {n}")

print()
print("#" * 78)
print("# 5. Sango3.PAK 中 Script / *.so 文件")
print("#" * 78)
e2, _, _, _ = read_index(os.path.join(GAME, "Sango3.PAK"))
for n, o, s in e2:
    if n.lower().endswith(".so") or n.lower().startswith("script"):
        print(f"  {s:>9,}  {n}")

print()
print("#" * 78)
print("# 6. 磁盘上的外置 Setting / Shape 目录（第三方补丁）")
print("#" * 78)
for sub in ("Setting", "Shape", "Script"):
    d = os.path.join(GAME, sub)
    if os.path.isdir(d):
        print(f"  [磁盘] {sub}\\ 存在:")
        n = 0
        for r, ds, fs in os.walk(d):
            for f in fs:
                fp = os.path.join(r, f)
                print(f"      {os.path.getsize(fp):>9,}  {os.path.relpath(fp, GAME)}")
                n += 1
                if n > 25:
                    break
            if n > 25:
                break
    else:
        print(f"  [磁盘] {sub}\\ 不存在")

print()
print("#" * 78)
print("# 7. General02.ini 结构预览")
print("#" * 78)
p = find("General02.ini")
if p:
    t, e = try_read(p)
    for i, ln in enumerate(t.splitlines()[:30]):
        print(f"  {i:4d}| {ln}")
