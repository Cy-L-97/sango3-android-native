"""第4轮：Smart Script 格式逆向（以 240 字节的 BFAI.so 为样本）+ 修正 SuperAttack 统计"""
import os, sys, re, struct
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

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
print("# 1. SuperAttack 统计（正则限定为空格/制表符，不跨行）")
print("#" * 78)
p = find("General01.ini")
t, _ = try_read(p)
blocks = re.split(r"(?=\[GENERAL\])", t)
pat = re.compile(r"^[ \t]*(\w+)[ \t]*=[ \t]*(.*?)[ \t]*$", re.M)
total = has = 0
empties_str80 = []
vals = []
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
        vals.append((kv.get("No"), name, st, sa))
    elif st >= 80:
        empties_str80.append((kv.get("No"), name, st))
print(f"  武将总数={total}  有 SuperAttack={has}  空={total-has}")
print(f"  武力>=80 且 SuperAttack 为空 = {len(empties_str80)} 人  <<< 第4项需求直接相关")
for no, name, st in empties_str80[:20]:
    print(f"     No={no:<5} {name:<8} 武力={st}")
print(f"  -- 有 SuperAttack 的 {len(vals)} 人 --")
for no, name, st, sa in vals[:25]:
    print(f"     No={no:<5} {name:<8} 武力={st:<4} SuperAttack='{sa}'")
# 值域分布
from collections import Counter
c = Counter(v for _, _, _, v in vals)
print(f"  -- SuperAttack 取值分布: {c.most_common(20)}")

print()
print("#" * 78)
print("# 2. BFAI.so 全量 hexdump (240 字节)")
print("#" * 78)
p = os.path.join(SCRIPT, "Sango3_PAK", "BFAI.so")
with open(p, "rb") as f:
    d = f.read()
print(f"大小={len(d)}")
for i in range(0, len(d), 16):
    chunk = d[i:i+16]
    hx = " ".join(f"{b:02X}" for b in chunk)
    asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
    print(f"  {i:04X}  {hx:<48}  {asc}")

print()
print("#" * 78)
print("# 3. SuperAttack.so 头部 512 字节")
print("#" * 78)
p = os.path.join(SCRIPT, "Sango3_PAK", "SuperAttack.so")
with open(p, "rb") as f:
    d = f.read()
for i in range(0, 512, 16):
    chunk = d[i:i+16]
    hx = " ".join(f"{b:02X}" for b in chunk)
    asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
    print(f"  {i:04X}  {hx:<48}  {asc}")

print()
print("#" * 78)
print("# 4. 头部字段解析（按 Smart Script 猜测）")
print("#" * 78)
def hdr(d, label):
    print(f"  [{label}] 大小={len(d):,}")
    print(f"    +00  magic   = {d[:12]!r}")
    for off in range(12, 48, 4):
        print(f"    +{off:02X}  u32     = {struct.unpack_from('<I', d, off)[0]:,}  (0x{struct.unpack_from('<I', d, off)[0]:X})")
hdr(d, "SuperAttack.so")
print()
p = os.path.join(SCRIPT, "Sango3_PAK", "BFAI.so")
with open(p, "rb") as f:
    db = f.read()
hdr(db, "BFAI.so")

print()
print("#" * 78)
print("# 5. SuperAttack.so 中符号表定位（字符串带偏移）")
print("#" * 78)
strs = []
for m in re.finditer(rb"[\x20-\x7e]{4,}", d):
    s = m.group().decode("latin1")
    strs.append((m.start(), s))
print(f"  共 {len(strs)} 个串，前 30 个带偏移：")
for off, s in strs[:30]:
    print(f"    0x{off:06X}  {s}")
print(f"  ... 末尾 15 个：")
for off, s in strs[-15:]:
    print(f"    0x{off:06X}  {s}")
