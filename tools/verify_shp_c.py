# -*- coding: utf-8 -*-
"""
verify_shp_c.py —— 用 Python 侧解码器复算 C 工具 (sango3shp) 的输出，逐项比对。

两侧必须一致：width/height/frames/key/hash_fnv1a/各采样点 RGBA。
若全部相同，说明 engine/src/shp.c 与 tools/shp.py 逐像素等价。
"""
import os
import sys
import glob

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.stdout.reconfigure(encoding="utf-8")

from shp import parse_header, frame_offsets, decode_rgb565  # noqa: E402

ROOT = os.path.dirname(HERE)
SAMPLE = os.path.join(ROOT, ".workbuddy", "sample")


def fnv1a(data: bytes) -> int:
    h = 2166136261
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def py_probe(path):
    with open(path, "rb") as f:
        d = f.read()
    rgb, w, h, hdr = decode_rgb565(d, None, None)
    # 构造 RGBA：与 C 侧一致，key 命中则 alpha=0
    key = hdr["key"]
    rgba = bytearray(w * h * 4)
    # 需要逐像素重新判定 key，因此重走一遍帧循环
    offs, hdr2 = frame_offsets(d)
    W = hdr2["w"]
    for y, off in enumerate(offs):
        end = offs[y + 1] if y + 1 < len(offs) else len(d)
        blk = d[off:end]
        if len(blk) < 4:
            continue
        span = int.from_bytes(blk[2:4], "little")
        body = blk[4:]
        avail = len(body) // 2
        if span > avail:
            span = avail
        npx = min(span, W)
        for x in range(npx):
            v = body[x * 2] | (body[x * 2 + 1] << 8)
            r = (v >> 11) & 0x1F
            g = (v >> 5) & 0x3F
            b = v & 0x1F
            o = (y * W + x) * 4
            rgba[o] = r * 255 // 31
            rgba[o + 1] = g * 255 // 63
            rgba[o + 2] = b * 255 // 31
            rgba[o + 3] = 0 if v == key else 255
    pts = [
        (0, 0), (10, 10), (50, 60),
        (W // 2, h // 2),
        (W - 1 if W else 0, 0),
        (0, h - 1 if h else 0),
        (W - 1 if W else 0, h - 1 if h else 0),
    ]
    samples = []
    for (x, y) in pts:
        if x < W and y < h:
            o = (y * W + x) * 4
            samples.append((x, y, rgba[o], rgba[o + 1], rgba[o + 2], rgba[o + 3]))
        else:
            samples.append((x, y, 0, 0, 0, 0))
    return {
        "file_bytes": len(d),
        "type": hdr["type"],
        "key": key,
        "width": W,
        "height": h,
        "frames": len(offs),
        "hash": fnv1a(bytes(rgba)),
        "samples": samples,
    }


def parse_c_output(text):
    """解析 sango3shp 的输出。"""
    res = {"samples": []}
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("file_bytes"):
            res["file_bytes"] = int(s.split("=")[1])
        elif s.startswith("type"):
            res["type"] = int(s.split("=")[1])
        elif s.startswith("key"):
            res["key"] = int(s.split("=")[1], 16)
        elif s.startswith("width"):
            res["width"] = int(s.split("=")[1])
        elif s.startswith("height"):
            res["height"] = int(s.split("=")[1])
        elif s.startswith("frames"):
            res["frames"] = int(s.split("=")[1])
        elif s.startswith("hash_fnv1a"):
            res["hash"] = int(s.split("=")[1], 16)
        elif s.startswith("px("):
            body = s.split("=")[1]
            vals = [int(v) for v in body.split(",")]
            coord = s[3:s.index(")")]
            x, y = coord.split(",")
            res["samples"].append((int(x), int(y), *vals))
    return res


def main():
    c_out_path = os.path.join(ROOT, ".workbuddy", "probe_shp_c.txt")
    if not os.path.isfile(c_out_path):
        print(f"缺少 C 侧输出: {c_out_path}")
        return 2
    with open(c_out_path, "r", encoding="utf-8", errors="replace") as f:
        ctext = f.read()

    # 按 "##### <path> #####" 分块（路径由 run_shp_c.py 书写，C 工具只输出 ASCII 数据）
    blocks = []
    cur = None
    for line in ctext.splitlines():
        if line.startswith("#####"):
            if cur is not None:
                blocks.append(cur)
            cur = {"path": line.strip("# \t"), "lines": []}
        elif cur is not None:
            cur["lines"].append(line)
    if cur is not None:
        blocks.append(cur)

    total = 0
    match = 0
    fail = 0
    skip = 0
    for blk in blocks:
        path = blk["path"]
        first = blk["lines"][0] if blk["lines"] else ""
        if first.startswith("FAIL"):
            if "unsupported shp type" in first:
                skip += 1
                print(f"  ~ 跳过（非 0 类型）{os.path.basename(path)}")
            else:
                print(f"  ✗ [C 解码失败] {os.path.basename(path)}: {first}")
                fail += 1
            continue
        total += 1
        c = parse_c_output("\n".join(blk["lines"]))
        try:
            p = py_probe(path)
        except Exception as e:  # noqa: BLE001
            print(f"  [Py异常] {os.path.basename(path)}: {e}")
            fail += 1
            continue

        diffs = []
        for k in ("file_bytes", "type", "key", "width", "height", "frames", "hash"):
            cv, pv = c.get(k), p.get(k)
            if cv != pv:
                diffs.append(f"{k}: C={cv} Py={pv}")
        cs, ps = c["samples"], p["samples"]
        if len(cs) != len(ps):
            diffs.append(f"samples count: C={len(cs)} Py={len(ps)}")
        else:
            for a, b in zip(cs, ps):
                if a != b:
                    diffs.append(f"sample C={a} Py={b}")

        name = os.path.basename(path)
        if diffs:
            print(f"  ✗ {name}  ({len(diffs)} 处不一致)")
            for dline in diffs[:6]:
                print(f"        {dline}")
        else:
            match += 1
            print(f"  ✓ {name}  {p['width']}x{p['height']} key=0x{p['key']:04X} "
                  f"hash=0x{p['hash']:08X}")

    print()
    print(f"合计：比对 {total} 个文件，完全一致 {match}，不一致 {fail}，跳过 {skip}")
    return 0 if (total and match == total and fail == 0) else 1


if __name__ == "__main__":
    sys.exit(main())
