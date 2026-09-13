"""三国群英传3 资源包格式勘察工具（M0 第一步）

用法: python inspect_pak.py <pak文件路径>
输出: PAK 头部十六进制转储、可打印字符串扫描、索引结构推断
"""
import sys, os, struct, re

def hexdump(data: bytes, base: int = 0, width: int = 16, limit: int = 0):
    lines = []
    n = len(data) if limit <= 0 else min(len(data), limit)
    for off in range(0, n, width):
        chunk = data[off:off + width]
        hexs = " ".join(f"{b:02X}" for b in chunk)
        asci = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"{base+off:08X}  {hexs:<{width*3}}  |{asci}|")
    return lines

def scan_strings(data: bytes, minlen: int = 4, limit: int = 60):
    pat = re.compile(rb"[\x20-\x7E]{%d,}" % minlen)
    res = []
    for m in pat.finditer(data):
        res.append((m.start(), m.group().decode("ascii", "replace")))
        if len(res) >= limit:
            break
    return res

def main():
    path = sys.argv[1]
    size = os.path.getsize(path)
    print(f"### 文件: {path}")
    print(f"### 大小: {size:,} 字节 ({size/1024/1024:.1f} MB)")
    with open(path, "rb") as f:
        head = f.read(1024)
        f.seek(max(0, size - 1024))
        tail = f.read(1024)

    print("\n--- 头部 512 字节 ---")
    for l in hexdump(head, 0, 16, 512):
        print(l)
    print("\n--- 头部 前512字节 中的字符串 ---")
    for off, s in scan_strings(head, 4, 30):
        print(f"  0x{off:04X}  {s!r}")
    print("\n--- 尾部 512 字节 十六进制 ---")
    for l in hexdump(tail, size - len(tail), 16, 512):
        print(l)
    print("\n--- 尾部 中的字符串 ---")
    for off, s in scan_strings(tail, 4, 30):
        print(f"  0x{size-len(tail)+off:08X}  {s!r}")

    # 把前 64 字节按 u32 / u16 解释，帮助判断是否有索引头
    print("\n--- 头部前 64 字节 解释 ---")
    print("u32 LE:", [f"{v:#010x}" for v in struct.unpack("<16I", head[:64])])
    print("u16 LE:", [f"{v:#06x}" for v in struct.unpack("<32H", head[:64])])

if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
