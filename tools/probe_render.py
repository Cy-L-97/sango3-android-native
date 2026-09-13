"""渲染测试：列出样本并把 SHP 解码成 PNG"""
import os, sys
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shp import decode_rgb565, write_png, parse_header

SAMPLE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".workbuddy", "sample"))
OUT = os.path.join(SAMPLE, "..", "png")

print("=== sample 目录内容 ===")
shps = []
for r, ds, fs in os.walk(SAMPLE):
    for f in fs:
        p = os.path.join(r, f)
        rel = os.path.relpath(p, SAMPLE)
        print(f"  {os.path.getsize(p):>9,}  {rel}")
        if f.lower().endswith(".shp"):
            shps.append(p)

print("\n=== 渲染 ===")
for p in shps:
    rel = os.path.relpath(p, SAMPLE).replace("\\", "_").replace("/", "_")
    dst = os.path.join(OUT, os.path.splitext(rel)[0] + ".png")
    try:
        with open(p, "rb") as f:
            d = f.read()
        hdr = parse_header(d)
        rgb, w, h, _ = decode_rgb565(d, None, None)
        write_png(dst, w, h, rgb)
        print(f"  OK  {rel}  {w}x{h}  type={hdr['type']} key=0x{hdr['key']:04X} -> {dst}")
    except Exception as e:
        print(f"  FAIL {rel}: {type(e).__name__}: {e}")
