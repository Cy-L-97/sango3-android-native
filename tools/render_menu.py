#!/usr/bin/env python3
"""主菜单场景 raw → PNG（M2-5）。

用法:
  python tools/render_menu.py [raw] [png]
默认:
  build/pc/out/menu.raw  →  .workbuddy/render/menu.png

零依赖（只用 zlib + struct 写 PNG），与 tools/shp.py 的 write_png 同思路。
画布尺寸优先读同名 *_summary.txt 的 canvas 行。
"""
import os
import sys
import zlib
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def write_png(path, w, h, rgba):
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)                       # 每行过滤器 = None
        raw += rgba[y * stride:(y + 1) * stride]

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    raw_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build/pc/out/menu.raw")
    out_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, ".workbuddy/render/menu.png")

    w, h = 640, 480
    summ = raw_path[:-4] + "_summary.txt"
    if os.path.exists(summ):
        with open(summ, encoding="utf-8") as f:
            for line in f:
                if line.startswith("canvas"):
                    pair = line.split()[1]
                    w, h = (int(x) for x in pair.split("x"))

    data = open(raw_path, "rb").read()
    need = w * h * 4
    if len(data) < need:
        print("raw too small: %d < %d" % (len(data), need))
        return 1
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    write_png(out_path, w, h, data[:need])
    print("wrote %s  %dx%d" % (out_path, w, h))
    return 0


if __name__ == "__main__":
    sys.exit(main())
