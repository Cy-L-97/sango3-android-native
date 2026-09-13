"""第3轮：修正统计 + 脚本文件格式 + 补丁内容"""
import os, sys, re, struct
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pak import read_index

GAME = r"E:\Program Files (x86)\steam\steamapps\common\Sango3"
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DATA = os.path.join(ROOT, ".workbuddy", "data")
SCRIPT = os.path.join(ROOT, ".workbuddy", "script")

def try_read(path):
    with open(path, "rb") as f:
        raw = f.read()
    for enc in ("gbk", "big5", "utf-8", "latin1"):
        try:
            return raw.decode(enc), enc
        except UnicodeDecodeError:
            continue
    return raw.decode("latin1"), "latin1"

def find(name, root=DATA):
    for r, ds, fs in os.walk(root):
        for f in fs:
            if f.lower() == name.lower():
                return os.path.join(r, f)
    return None

print("#" * 78)
print("# 1. SuperAttack 字段（修正正则，不跨行）")
print("#" * 78)
p = find("General01.ini")
t, _ = try_read(p)
blocks = re.split(r"(?=\[GENERAL\])", t)
total = has = 0
empty_str80 = []
samples = []
pat = re.compile(r"^\s*(\w+)\s*=\s*(.*?)\s*$", re.M)
for b in blocks:
    if "No" not in b:
        continue
    kv = dict(pat.findall(b))
    if "No" not in kv:
        continue
    total += 1
    sa = kv.get("SuperAttack", "").strip()
    name = kv.get("Name", "?")
    st = int(kv.get("Strength", "0") or 0)
    if sa:
        has += 1
        if len(samples) < 20:
            samples.append((kv.get("No"), name, st, sa))
    else:
        if st >= 80:
            empty_str80.append((kv.get("No"), name, st))
print(f"  武将总数={total}  有SuperAttack={has}  空={total-has}")
print(f"  武力>=80 但 SuperAttack 为空 = {len(empty_str80)} 人")
print("  -- 空值样例（武力>=80）--")
for no, name, st in empty_str80[:15]:
    print(f"     No={no:<5} {name:<8} 武力={st}")
print(f"  -- 非空样例 --")
for no, name, st, sa in samples:
    print(f"     No={no:<5} {name:<8} 武力={st:<4} SuperAttack='{sa}'")

print()
print("#" * 78)
print("# 2. Thing.ini 中 Count=400 的上下文")
print("#" * 78)
p = find("Thing.ini")
t, _ = try_read(p)
lines = t.splitlines()
for center in (334, 824, 994):
    lo = max(0, center - 6)
    print(f"  --- 第 {center} 行附近 ---")
    for i in range(lo, min(len(lines), center + 4)):
        mark = ">>" if i == center else "  "
        print(f"   {mark}{i:5d}| {lines[i]}")

print()
print("#" * 78)
print("# 3. Script/*.so 导出与格式分析")
print("#" * 78)
os.makedirs(SCRIPT, exist_ok=True)
for pakname in ("Sango3.PAK", "Update.PAK"):
    entries, _, _, _ = read_index(os.path.join(GAME, pakname))
    with open(os.path.join(GAME, pakname), "rb") as f:
        for n, off, size in entries:
            if not n.lower().endswith(".so"):
                continue
            f.seek(off)
            data = f.read(size)
            dst = os.path.join(SCRIPT, pakname.replace(".", "_"), os.path.basename(n))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as g:
                g.write(data)

for r, ds, fs in os.walk(SCRIPT):
    for f in sorted(fs):
        p = os.path.join(r, f)
        with open(p, "rb") as g:
            head = g.read(32)
        rel = os.path.relpath(p, SCRIPT)
        print(f"\n  --- {rel}  ({os.path.getsize(p):,} 字节) ---")
        print(f"      头部: {' '.join(f'{b:02X}' for b in head[:24])}")
        print(f"      ASCII: {''.join(chr(b) if 32<=b<127 else '.' for b in head[:24])}")
        # 判定文件类型
        if head[:2] == b"MZ":
            print("      => PE/Windows 可执行(可能是 DLL 或 exe 片段)")
        elif head[:4] == b"\x7fELF":
            print("      => ELF")
        elif head[:4] == b"PAKS":
            print("      => 内嵌 PAK 包!")
        else:
            print("      => 自定义格式")

# 提取 SuperAttack.so 中的字符串
print()
print("#" * 78)
print("# 4. SuperAttack.so 字符串提取（找关键字）")
print("#" * 78)
cands = []
for r, ds, fs in os.walk(SCRIPT):
    for f in fs:
        if "superattack" in f.lower():
            cands.append(os.path.join(r, f))
for p in cands:
    with open(p, "rb") as g:
        d = g.read()
    print(f"\n  ### {os.path.relpath(p, SCRIPT)} ({len(d):,} B)")
    # ASCII 串
    asc = re.findall(rb"[\x20-\x7e]{5,}", d)
    print(f"  -- ASCII 串 {len(asc)} 个（前 40）--")
    for s in asc[:40]:
        print(f"     {s.decode('latin1')}")
    # Big5 中文串（用宽松扫描）
    try:
        txt = d.decode("big5", "ignore")
    except Exception:
        txt = ""
    cn = re.findall(r"[\u4e00-\u9fff]{2,}", txt)
    print(f"  -- 中文串 {len(cn)} 个（前 30）--")
    for s in cn[:30]:
        print(f"     {s}")

print()
print("#" * 78)
print("# 5. 第三方补丁说明文件")
print("#" * 78)
for r, ds, fs in os.walk(os.path.join(GAME, "Setting")):
    for f in fs:
        if f.endswith(".txt"):
            p = os.path.join(r, f)
            t, e = try_read(p)
            print(f"\n  ### {os.path.relpath(p, GAME)} (编码={e}) ###")
            for ln in t.splitlines()[:60]:
                print(f"     {ln}")
