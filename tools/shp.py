"""SHP 精灵解码器（M0 核心组件）
格式（逆向结论）:
  头部 (0x00~0x23):
    +0x00  'TLHS' 魔数
    +0x04  u32   类型/版本 (0=索引精灵, 1=...)
    +0x08  u32   2
    +0x0C  u32   ?
    +0x10  u32   透明键色/特殊值 (Portrait=0xFFFF)
    +0x14  u32   宽
    +0x18  u32   高（Portrait 情形下 = 帧数）
    +0x1C  u32   0
    +0x20  u32   0
    +0x24  偏移表起始（u32 × N，指向各帧数据）
  帧数据:
    +0x00  u16   flags
    +0x02  u16   本行宽度（像素）
    +0x04  像素  RGB565 小端, 每像素 2 字节
"""
import os, sys, struct, zlib

MAGIC = b"TLHS"

def u16(d, o): return struct.unpack_from("<H", d, o)[0]
def u32(d, o): return struct.unpack_from("<I", d, o)[0]

def rgb565(v):
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)

def parse_header(d):
    if d[:4] != MAGIC:
        raise ValueError(f"不是 TLHS 精灵: {d[:8]!r}")
    return {
        "type": u32(d, 0x04),
        "f2": u32(d, 0x08),
        "f3": u32(d, 0x0C),
        "key": u32(d, 0x10),
        "w": u32(d, 0x14),
        "h": u32(d, 0x18),
        "f7": u32(d, 0x1C),
        "f8": u32(d, 0x20),
        "data_off": u32(d, 0x24),
    }

def frame_offsets(d):
    """从 0x24 起读偏移表，直到第一个数据块位置。"""
    hdr = parse_header(d)
    first = hdr["data_off"]
    n = (first - 0x24) // 4
    return [u32(d, 0x24 + i * 4) for i in range(n)], hdr

def decode_rgb565(d, w, h, frame_bytes=None):
    """按『每帧一行』假设解码：帧 i 的像素填到第 i 行。
    每帧: 4 字节头 + 200 字节像素(100px * 2B) + 2 字节余量 = 206
    """
    offs, hdr = frame_offsets(d)
    W = hdr["w"]
    H = len(offs)
    px = bytearray(W * H * 3)
    key = hdr["key"]
    for y, off in enumerate(offs):
        end = offs[y + 1] if y + 1 < len(offs) else len(d)
        blk = d[off:end]
        fw = u16(blk, 0x02)          # 本行宽度
        body = blk[4:]               # 跳过 4 字节头
        npx = min(fw, W)
        for x in range(npx):
            v = body[x * 2] | (body[x * 2 + 1] << 8)
            r, g, b = rgb565(v)
            o = (y * W + x) * 3
            px[o] = r; px[o + 1] = g; px[o + 2] = b
    return bytes(px), W, H, hdr

def write_png(path, w, h, rgb, alpha=None):
    """零依赖 PNG 写出（RGB 或 RGBA）。"""
    if alpha is None:
        bpp, ctype = 3, 2
        raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    else:
        bpp, ctype = 4, 6
        rows = []
        for y in range(h):
            row = bytearray()
            for x in range(w):
                i = y * w + x
                row += bytes((rgb[i*3], rgb[i*3+1], rgb[i*3+2], alpha[i]))
            rows.append(b"\x00" + bytes(row))
        raw = b"".join(rows)
    def chunk(t, data):
        return (struct.pack(">I", len(data)) + t + data
                + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)

if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    src = sys.argv[1]
    dst = sys.argv[2]
    with open(src, "rb") as f:
        d = f.read()
    rgb, w, h, hdr = decode_rgb565(d, None, None)
    print(f"{os.path.basename(src)}: 头部={hdr}")
    print(f"  解码 {w}x{h} -> {dst}")
    write_png(dst, w, h, rgb)
