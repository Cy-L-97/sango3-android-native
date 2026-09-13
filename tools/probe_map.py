"""地图 Map\\*.blk 格式勘察"""
import os, sys, struct
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pak import read_index

GAME = r"E:\Program Files (x86)\steam\steamapps\common\Sango3"
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OUT = os.path.join(ROOT, ".workbuddy", "data", "Map")

def u16(d, o): return struct.unpack_from("<H", d, o)[0]
def u32(d, o): return struct.unpack_from("<I", d, o)[0]

def hexdump(data, base=0, n=112, width=16):
    out = []
    for i in range(0, min(n, len(data)), width):
        c = data[i:i+width]
        hx = " ".join(f"{b:02X}" for b in c)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in c)
        out.append(f"  {base+i:06X}  {hx:<{width*3}}  {asc}")
    return "\n".join(out)

entries, _, _, _ = read_index(os.path.join(GAME, "Sango3.PAK"))
maps = [(n, o, s) for n, o, s in entries if n.lower().startswith("map\\")]
print(f"=== Map 条目 {len(maps)} 个 ===")
os.makedirs(OUT, exist_ok=True)
sizes = {}
with open(os.path.join(GAME, "Sango3.PAK"), "rb") as f:
    for n, off, size in maps:
        f.seek(off)
        data = f.read(size)
        with open(os.path.join(OUT, os.path.basename(n)), "wb") as g:
            g.write(data)
        sizes[n] = size
for n, s in sorted(sizes.items()):
    print(f"  {s:>10,}  {n}")

print()
print("=== 大小分布 ===")
from collections import Counter
c = Counter(sizes.values())
for k, v in c.most_common(12):
    print(f"  {k:>10,} B × {v} 个")

print()
print("=== 前 3 个文件的头部 dump ===")
samples = sorted(sizes.items(), key=lambda x: x[0])[:3]
for n, s in samples:
    p = os.path.join(OUT, os.path.basename(n))
    with open(p, "rb") as f:
        d = f.read()
    print(f"\n--- {n}  ({len(d):,} B) ---")
    print(hexdump(d, 0, 112))
    print("  头部 u32: " + "  ".join(f"[{i:02X}]={u32(d,i):,}" for i in range(0, 32, 4)))

# 尺寸推断
print()
print("=== 尺寸反推 ===")
for n, s in sorted(sizes.items()):
    p = os.path.join(OUT, os.path.basename(n))
    with open(p, "rb") as f:
        d = f.read()
    cands = []
    for w in (16, 20, 24, 32, 40, 48, 64, 80, 100, 128, 160, 200, 256, 320, 640):
        for bpp in (1, 2, 4):
            if s % (w * bpp) == 0:
                h = s // (w * bpp)
                if 1 <= h <= 600:
                    cands.append(f"{w}×{h}@{bpp}B")
    print(f"  {os.path.basename(n):<14} {s:>9,}  ->  {', '.join(cands[:5])}")
