"""三国群英传3 PAK 资源包解析器（M0 核心组件）

格式（逆向得出，待更多样本验证）:
  头部: 'PAKSW' + 27 字节零 + 子包名表(8字节×N) + ...
        u32@0x08 = 索引起始偏移
        u32@0x0C = 待确认
  索引: 从索引起始偏移到文件末尾，每 64 字节一条记录:
        +0x00 u32  数据大小(压缩后?)
        +0x04 u32  数据在文件中的偏移
        +0x08 16B  未知/保留
        +0x18 40B  文件路径(反斜杠, 以 0 结尾)

用法:
  python pak.py list   <pak> [关键字]
  python pak.py extract <pak> <输出目录> [关键字]
"""
import sys, os, struct

def read_index(path: str):
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        head = f.read(64)
        if not head[:4] == b"PAKS":
            raise ValueError(f"不是 PAKS 资源包: {path}  头部={head[:8]!r}")
        count = struct.unpack_from("<I", head, 0x04)[0]
        index_off = struct.unpack_from("<I", head, 0x08)[0]
        hint = struct.unpack_from("<I", head, 0x0C)[0]
        f.seek(index_off)
        blob = f.read(size - index_off)
    if len(blob) % 64 != 0:
        # 容错：丢掉末尾不足一条记录的零头
        blob = blob[: len(blob) - (len(blob) % 64)]
    entries = []
    for i in range(0, len(blob), 64):
        rec = blob[i : i + 64]
        dsize, doff = struct.unpack_from("<II", rec, 0)
        raw = rec[0x18:0x18 + 40]
        name = raw.split(b"\x00")[0].decode("gbk", "replace")
        if not name:
            continue
        entries.append((name, doff, dsize))
    return entries, index_off, hint, count

def cmd_list(pak, kw=None, limit=40):
    entries, index_off, hint, count = read_index(pak)
    print(f"# {os.path.basename(pak)}  索引偏移={index_off:,}  头部声明条目数={count:,}  实际解析={len(entries):,}  hint={hint}")
    shown = 0
    for name, off, size in entries:
        if kw and kw.lower() not in name.lower():
            continue
        shown += 1
        if shown > limit:
            break
        print(f"  {off:>12,}  {size:>10,}  {name}")
    if kw:
        print(f"# 匹配 '{kw}' 的条目共 {sum(1 for n,_,_ in entries if kw.lower() in n.lower())} 条，显示前 {min(shown,limit)} 条")

def cmd_extract(pak, outdir, kw=None):
    entries, _, _, _ = read_index(pak)
    with open(pak, "rb") as f:
        n = 0
        for name, off, size in entries:
            if kw and kw.lower() not in name.lower():
                continue
            rel = name.replace("\\", "/")
            dst = os.path.join(outdir, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            f.seek(off)
            data = f.read(size)
            with open(dst, "wb") as g:
                g.write(data)
            n += 1
        print(f"# 已导出 {n} 个文件 -> {outdir}")

if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    mode = sys.argv[1]
    if mode == "list":
        kw = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] else None
        lim = int(sys.argv[4]) if len(sys.argv) > 4 else 40
        cmd_list(sys.argv[2], kw, lim)
    elif mode == "extract":
        cmd_extract(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else None)
    else:
        print(__doc__)
