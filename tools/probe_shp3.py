"""SHP 格式深度勘察（第2轮）
目标：确认数据区编码方式、定位调色板、摸清素材分类。
"""
import os, sys, struct
from collections import Counter
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pak import read_index

GAME = r"E:\Program Files (x86)\steam\steamapps\common\Sango3"
SAMPLE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".workbuddy", "sample"))

def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]

def hexdump(data, base=0, n=160, width=16):
    out = []
    for i in range(0, min(n, len(data)), width):
        chunk = data[i:i+width]
        hx = " ".join(f"{b:02X}" for b in chunk)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        out.append(f"  {base+i:08X}  {hx:<{width*3}}  {asc}")
    return "\n".join(out)

print("#" * 78)
print("# A. Sango3.PAK 素材分类统计")
print("#" * 78)
entries, index_off, hint, count = read_index(os.path.join(GAME, "Sango3.PAK"))
lvl1 = Counter()
lvl2 = Counter()
ext = Counter()
for name, _, _ in entries:
    parts = name.split("\\")
    lvl1[parts[0]] += 1
    if len(parts) > 1:
        lvl2[parts[0] + "\\" + parts[1]] += 1
    if "." in parts[-1]:
        ext[os.path.splitext(parts[-1])[1].lower()] += 1
print("-- 顶层目录 --")
for k, v in lvl1.most_common(30):
    print(f"  {v:>7,}  {k}")
print("-- 二级目录 (Top 40) --")
for k, v in lvl2.most_common(40):
    print(f"  {v:>7,}  {k}")
print("-- 扩展名 --")
for k, v in ext.most_common(20):
    print(f"  {v:>7,}  {k or '(无)'}")

print()
print("#" * 78)
print("# B. 查找调色板类文件")
print("#" * 78)
for kw in ["palette", "color", ".pal", "pal.", "rgb", "clut"]:
    hits = [n for n, _, _ in entries if kw in n.lower()]
    print(f"  关键字 '{kw}': {len(hits)} 条  {hits[:5]}")

print()
print("#" * 78)
print("# C. Portrait001.SHP 数据区结构")
print("#" * 78)
p = os.path.join(SAMPLE, "Shape/Portrait/Portrait001.SHP")
with open(p, "rb") as f:
    d = f.read()
print(f"文件大小={len(d):,}")
# 解析偏移表
n_frames = (u32(d, 0x24) - 0x24) // 4
print(f"按 0x24 起表推算帧数 = {n_frames}")
offs = [u32(d, 0x24 + i*4) for i in range(n_frames)]
print(f"前 12 个偏移: {offs[:12]}")
print(f"后 4 个偏移: {offs[-4:]}")
diffs = [offs[i+1]-offs[i] for i in range(len(offs)-1)]
print(f"帧间距: min={min(diffs)} max={max(diffs)} 众数={Counter(diffs).most_common(3)}")
print(f"末帧末尾 = {offs[-1] + diffs[-1]:,}  (文件 {len(d):,})")
print()
print("-- 数据区头部 dump (0x1F0 ~ 0x2A0) --")
print(hexdump(d[0x1F0:0x2A0], 0x1F0))
print()
print("-- 第 1 帧完整数据 (0x204, 206 字节) --")
print(hexdump(d[offs[0]:offs[0]+206], offs[0], width=16))
print()
print("-- 第 2 帧起始处 (对比是否同构) --")
print(hexdump(d[offs[1]:offs[1]+64], offs[1]))
print()
body = d[offs[0]:]
hist = Counter(body[:4000])
print(f"-- 数据区前 4000 字节的取值分布 (Top 20) --")
print("  " + "  ".join(f"{k:02X}:{v}" for k, v in hist.most_common(20)))
zero_runs = body[:4000].count(0)
print(f"-- 前 4000 字节中 0x00 出现 {zero_runs} 次 ({zero_runs/40:.1f}%)")

print()
print("#" * 78)
print("# D. OdinLogo.SHP 结构")
print("#" * 78)
p2 = os.path.join(SAMPLE, "Shape/Portrait/OdinLogo.SHP")
with open(p2, "rb") as f:
    d2 = f.read()
print(f"文件大小={len(d2):,}")
print(f"[0x0C]={u32(d2,0x0C):,}  [0x10]={u32(d2,0x10):,}  [0x14]={u32(d2,0x14):,}  [0x18]={u32(d2,0x18):,}")
print(f"[0x24]={u32(d2,0x24):,}  [0x28]={u32(d2,0x28):,}  [0x2C]={u32(d2,0x2C):,}  [0x30]={u32(d2,0x30):,}")
print("-- 0x2C0 ~ 0x340 --")
print(hexdump(d2[0x2C0:0x340], 0x2C0))
print("-- 0x7C0 ~ 0x840 --")
print(hexdump(d2[0x7C0:0x840], 0x7C0))
print("-- 0x3B0 ~ 0x440 (若数据从960开始) --")
print(hexdump(d2[0x3B0:0x440], 0x3B0))

# 检查 0x50 起是否真是等差表
tbl = [u32(d2, o) for o in range(0x50, 0x50+40*4, 4)]
print(f"-- 0x50 起 40 项: {tbl[:16]}")
diffs2 = [tbl[i+1]-tbl[i] for i in range(len(tbl)-1)]
print(f"-- 相邻差: {Counter(diffs2).most_common(5)}")
