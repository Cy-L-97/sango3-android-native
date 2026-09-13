"""M0 勘察脚本 2：Sango3.PAK 头部变体 + SHP 精灵格式初步分析"""
import os, struct, sys, glob

sys.stdout.reconfigure(encoding="utf-8")

G = r"E:\Program Files (x86)\steam\steamapps\common\Sango3"
SAMPLE = r"E:\用户\workbuddy\移植PC计划\.workbuddy\sample"

def hx(b, base=0, width=16, limit=None):
    n = len(b) if limit is None else min(len(b), limit)
    for off in range(0, n, width):
        c = b[off:off+width]
        h = " ".join(f"{x:02X}" for x in c)
        a = "".join(chr(x) if 32 <= x < 127 else "." for x in c)
        print(f"{base+off:08X}  {h:<{width*3}}  |{a}|")

print("########## 1) Sango3.PAK 头部/尾部 ##########")
p = os.path.join(G, "Sango3.PAK")
sz = os.path.getsize(p)
print(f"大小: {sz:,}")
with open(p, "rb") as f:
    head = f.read(128)
    print("--- 头部 128 字节 ---")
    hx(head)
    f.seek(max(0, sz - 128))
    tail = f.read(128)
    print("--- 尾部 128 字节 ---")
    hx(tail, sz - len(tail))
    # 猜测：末尾索引区，从倒数找第一条记录
    print("--- 头部 u32/u16 ---")
    print("u32:", [f"{v:#010x}" for v in struct.unpack_from("<32I", head, 0)])

print()
print("########## 2) 样本目录内容 ##########")
for root, dirs, files in os.walk(SAMPLE):
    for fn in files:
        fp = os.path.join(root, fn)
        print(f"{os.path.getsize(fp):>10,}  {os.path.relpath(fp, SAMPLE)}")

print()
print("########## 3) SHP 文件头部分析 ##########")
for fp in glob.glob(os.path.join(SAMPLE, "**", "*.SHP"), recursive=True)[:3]:
    data = open(fp, "rb").read()
    print(f"--- {os.path.relpath(fp, SAMPLE)}  大小 {len(data):,} ---")
    hx(data, 0, 16, 160)
    print("  u16[0:16]:", [f"{v:#06x}" for v in struct.unpack_from("<16H", data, 0)])
    print("  u32[0:8] :", [f"{v:#010x}" for v in struct.unpack_from("<8I", data, 0)])
