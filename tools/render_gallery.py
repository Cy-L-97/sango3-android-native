# -*- coding: utf-8 -*-
"""
render_gallery.py —— 调 sango3view 出图并转 PNG

链路：C 引擎（读 PAK → 解 SHP → 合成画布 → 写 raw RGBA）→ Python 转 PNG。
这样验证的是**引擎自己的渲染结果**，而不是 Python 解码器的结果。

用法:
    python tools/render_gallery.py                 # 出默认几组图
    python tools/render_gallery.py <pak> <kw> <cols> <limit> <out.png>
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
EXE = os.path.join(ROOT, "build", "pc", "bin", "sango3view.exe")
GAME = r"E:\Program Files (x86)\steam\steamapps\common\Sango3"
OUTDIR = os.path.join(ROOT, ".workbuddy", "render")
os.makedirs(OUTDIR, exist_ok=True)


def run_dump(pak, kw, cols, limit):
    raw = os.path.join(OUTDIR, "_tmp.raw")
    if os.path.isfile(raw):
        os.remove(raw)
    r = subprocess.run([EXE, "dump", pak, kw, raw, "--cols", str(cols), "--limit", str(limit)],
                       capture_output=True, text=True, encoding="ascii", errors="replace",
                       timeout=600)
    out = (r.stdout or "") + (r.stderr or "")
    if not os.path.isfile(raw):
        return None, out
    meta = {}
    for key in ("canvas_w", "canvas_h", "matched", "decoded", "hash_fnv1a", "pak_entries"):
        m = re.search(rf"^{key}\s*=\s*(\S+)\s*$", out, re.M)
        if m:
            meta[key] = m.group(1)
    with open(raw, "rb") as f:
        blob = f.read()
    os.remove(raw)
    return (blob, meta), out


def rgba_to_rgb(blob, w, h):
    n = w * h
    if len(blob) != n * 4:
        raise ValueError(f"raw 大小不符: {len(blob)} != {n*4}")
    rgb = bytearray(n * 3)
    for i in range(n):
        rgb[i * 3] = blob[i * 4]
        rgb[i * 3 + 1] = blob[i * 4 + 1]
        rgb[i * 3 + 2] = blob[i * 4 + 2]
    return bytes(rgb)


def render(pak, kw, cols, limit, out_png, quiet=False):
    res, out = run_dump(pak, kw, cols, limit)
    if not quiet:
        for line in out.splitlines():
            s = line.strip()
            if s and not s.startswith("ENTRY"):
                print(f"      {s}")
    if res is None:
        print(f"  ✗ 未产出 raw：{os.path.basename(pak)} / {kw}")
        return False
    blob, meta = res
    w = int(meta.get("canvas_w", 0))
    h = int(meta.get("canvas_h", 0))
    if w == 0 or h == 0:
        print(f"  ✗ 画布尺寸未知")
        return False
    rgb = rgba_to_rgb(blob, w, h)
    write_png(out_png, w, h, rgb)
    print(f"  ✓ {os.path.basename(out_png)}  {w}x{h}  "
          f"matched={meta.get('matched')} decoded={meta.get('decoded')} "
          f"hash={meta.get('hash_fnv1a')}")
    return True


JOBS = [
    ("Sango3.PAK", "Shape\\Portrait\\mFace",  12,  72, "portrait_mface_72.png"),
    ("Sango3.PAK", "Shape\\Portrait\\Portrait", 12, 72, "portrait_generals_72.png"),
    ("Sango3.PAK", "Shape\\Portrait\\Emperor",  4,   4, "portrait_emperors.png"),
    ("Sango3.PAK", "Shape\\MM\\Base",           8,  32, "ui_mm_base.png"),
]


def main():
    if not os.path.isfile(EXE):
        print(f"缺少 {EXE}，请先构建")
        return 2
    if len(sys.argv) >= 6:
        pak = sys.argv[1]
        kw = sys.argv[2]
        cols = int(sys.argv[3])
        limit = int(sys.argv[4])
        out = sys.argv[5]
        ok = render(pak, kw, cols, limit, out)
        return 0 if ok else 1

    print(f"引擎渲染验证 → {OUTDIR}")
    okn = 0
    for pakname, kw, cols, limit, outname in JOBS:
        pak = os.path.join(GAME, pakname)
        if not os.path.isfile(pak):
            print(f"  跳过（缺游戏文件）{pakname}")
            continue
        print(f"- {pakname} :: {kw}  cols={cols} limit={limit}")
        if render(pak, kw, cols, limit, os.path.join(OUTDIR, outname)):
            okn += 1
    print()
    print(f"完成 {okn}/{len(JOBS)} 组")
    return 0 if okn else 1


if __name__ == "__main__":
    sys.exit(main())
