"""SHP 精灵格式勘察脚本（M0）
目标：dump 头部 + 统计结构，推断出帧表/调色板/像素编码方式。
"""
import os, sys, struct, binascii
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pak import read_index

GAME = r"E:\Program Files (x86)\steam\steamapps\common\Sango3"
SAMPLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".workbuddy", "sample")

def extract(pak, outdir, kw, limit=8):
    entries, _, _, _ = read_index(pak)
    os.makedirs(outdir, exist_ok=True)
    got = []
    with open(pak, "rb") as f:
        for name, off, size in entries:
            if kw.lower() not in name.lower():
                continue
            rel = name.replace("\\", "/")
            dst = os.path.join(outdir, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            f.seek(off)
            data = f.read(size)
            with open(dst, "wb") as g:
                g.write(data)
            got.append((name, size, len(data)))
            if len(got) >= limit:
                break
    return got

def hexdump(data, base=0, n=192, width=16):
    out = []
    for i in range(0, min(n, len(data)), width):
        chunk = data[i:i+width]
        hx = " ".join(f"{b:02X}" for b in chunk)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        out.append(f"  {base+i:08X}  {hx:<{width*3}}  {asc}")
    return "\n".join(out)

def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]

def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]

def analyze(path):
    with open(path, "rb") as f:
        d = f.read()
    name = os.path.basename(path)
    print(f"\n{'='*78}")
    print(f"文件: {name}   大小={len(d):,} 字节")
    print(f"魔数: {d[:8]!r}")
    print(f"--- 头部 hexdump (192B) ---")
    print(hexdump(d, 0, 192))
    # 常见布局猜测
    print("--- 偏移 0x00~0x40 的 u32 值 ---")
    print("  " + "  ".join(f"[{i:02X}]={u32(d,i):,}" for i in range(0, 0x40, 4)))
    return d

def main():
    print("步骤1: 从 Sango3.PAK 导出样本")
    for kw, lim in [("Shape\\Portrait", 6), ("Shape\\Face", 4), ("Shape\\Logo", 2)]:
        got = extract(os.path.join(GAME, "Sango3.PAK"), SAMPLE, kw, lim)
        print(f"  关键字 '{kw}': 导出 {len(got)} 个")
        for n, sz, real in got[:6]:
            print(f"    {sz:>9,}  {n}")
    # 也导出一些小图（图标）
    got = extract(os.path.join(GAME, "Sango3.PAK"), SAMPLE, "Shape\\Item", 3)
    print(f"  关键字 'Shape\\Item': 导出 {len(got)} 个")

    print("\n\n步骤2: 逐个分析结构")
    targets = []
    for r, ds, fs in os.walk(SAMPLE):
        for f in fs:
            p = os.path.join(r, f)
            if os.path.getsize(p) > 0:
                targets.append(p)
    # 按大小排序，取最小的几个和最大的一个，便于对比
    targets.sort(key=os.path.getsize)
    pick = targets[:3] + targets[-2:]
    seen = set()
    for p in pick:
        if p in seen:
            continue
        seen.add(p)
        analyze(p)

if __name__ == "__main__":
    main()
