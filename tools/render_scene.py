# -*- coding: utf-8 -*-
"""
render_scene.py —— 分辨率无关渲染演示的驱动器

链路：sango3scene.exe（C）在 640×480 逻辑坐标搭场景 → 呈现到 1080p/2K/4K
      → 写 RGBA 原始像素 → Python 转 PNG（可直接肉眼比对滤镜与清晰度）。

用法:
    python tools/render_scene.py            # 跑完整矩阵
    python tools/render_scene.py --only 06_qhd_2560x1440_sharp
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.stdout.reconfigure(encoding="utf-8")

from shp import write_png  # noqa: E402

ROOT = os.path.dirname(HERE)
EXE = os.path.join(ROOT, "build", "pc", "bin", "sango3scene.exe")
GAME = r"E:\Program Files (x86)\steam\steamapps\common\Sango3"
OUTDIR = os.path.join(ROOT, ".workbuddy", "render", "scene")

OUT_RE = re.compile(
    r"^OUT\s+name=(\S+)\s+w=(\d+)\s+h=(\d+)\s+filter=(\w+)\s+aspect=(\w+)\s+scale=([\d.]+)"
    r"\s+tier=(\d+)\s+content=(\d+),(\d+),(\d+),(\d+)\s+probe_l2p=([\d.]+),([\d.]+)\s*$"
)


def rgba_to_rgb(blob, w, h):
    n = w * h
    if len(blob) != n * 4:
        raise ValueError(f"raw 大小不符: {len(blob)} != {n * 4}")
    rgb = bytearray(n * 3)
    rgb[0::3] = blob[0::4]
    rgb[1::3] = blob[1::4]
    rgb[2::3] = blob[2::4]
    return bytes(rgb)


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    if not os.path.isfile(EXE):
        print(f"缺少 {EXE}，请先构建（python tools/build_pc.py）")
        return 2
    pak = os.path.join(GAME, "Sango3.PAK")
    if not os.path.isfile(pak):
        print(f"缺少游戏资源包 {pak}")
        return 2

    cmd = [EXE, pak, OUTDIR] + sys.argv[1:]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="ascii",
                       errors="replace", timeout=1800)
    out = (r.stdout or "") + (r.stderr or "")

    print("=" * 78)
    print("sango3scene 输出")
    print("=" * 78)
    for line in out.splitlines():
        s = line.strip()
        if s and not s.startswith("OUT "):
            print("  " + s)
    print()

    print("=" * 78)
    print("分辨率矩阵")
    print("=" * 78)
    print(f"  {'物理分辨率':>12}  {'滤镜':<9} {'缩放':>6} {'档位':>4}  {'内容矩形':<20} {'逻辑(100,56)→物理'}")
    print("  " + "-" * 74)

    ok = 0
    for line in out.splitlines():
        m = OUT_RE.match(line.strip())
        if not m:
            continue
        (name, w, h, filt, aspect, scale, tier,
         cx, cy, cw, ch, px, py) = m.groups()
        w, h, cw, ch, tier = int(w), int(h), int(cw), int(ch), int(tier)
        raw = os.path.join(OUTDIR, name + ".raw")
        if not os.path.isfile(raw):
            print(f"  ✗ 缺 raw: {name}.raw")
            continue
        with open(raw, "rb") as f:
            blob = f.read()
        png = raw[:-4] + ".png"
        write_png(png, w, h, rgba_to_rgb(blob, w, h))
        os.remove(raw)
        print(f"  {w:>5}x{h:<6}  {filt:<9} {float(scale):>6.3f} {tier:>4}  "
              f"{cx},{cy},{cw},{ch:<7} ({px}, {py})")
        print(f"        → {os.path.basename(png)}  ({aspect}, "
              f"{os.path.getsize(png)/1024:.0f} KB)")
        ok += 1

    print()
    print(f"完成 {ok} 张。输出目录: {OUTDIR}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
