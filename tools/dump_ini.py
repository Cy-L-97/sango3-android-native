# -*- coding: utf-8 -*-
"""
dump_ini.py —— 从 PAK 提取单个 INI 并按原版编码（Big5/cp950）转 UTF-8 输出

为什么需要：
  原版 `Setting\\*.ini`（含 Menu.ini / Menu2.ini / MenuMap.ini）是 **Big5 繁体**，
  直接 cat 会乱码。本工具负责「取条目 + 解码 + 截取前 N 行」三件事，
  供 M2 的 UI 布局分析（以及人工核对 C 侧解析结果）使用。

用法:
  python tools/dump_ini.py <pak> <条目名> [--head N] [--out 文件] [--raw]
    <条目名> : 如 "Setting\\Menu.ini"（也接受 "Setting/Menu.ini"）
    --head N : 只输出前 N 行（默认 200；0 表示全部）
    --out    : 写入文件（默认打印到 stdout）
    --raw    : 不做编码转换，按 cp950 解码但保留原字节（调试用）
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding="utf-8")
from pak import read_index  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pak")
    ap.add_argument("entry")
    ap.add_argument("--head", type=int, default=200)
    ap.add_argument("--out", default=None)
    ap.add_argument("--raw", action="store_true")
    a = ap.parse_args()

    entries, _, _, _ = read_index(a.pak)
    want = a.entry.replace("/", "\\")
    hit = None
    for name, off, size in entries:
        if name.lower() == want.lower():
            hit = (name, off, size)
            break
    if not hit:
        # 退化为包含匹配，方便记不全路径
        for name, off, size in entries:
            if want.lower() in name.lower():
                hit = (name, off, size)
                break
    if not hit:
        print(f"未找到条目: {want}")
        return 2

    name, off, size = hit
    with open(a.pak, "rb") as f:
        f.seek(off)
        data = f.read(size)

    # 原版 INI 为 Big5（cp950 超集）；不可解码字节用 U+FFFD，与 C 侧口径一致
    txt = data.decode("cp950", "replace") if not a.raw else data.decode("cp950", "replace")
    lines = txt.splitlines()
    head = lines[: a.head] if a.head > 0 else lines
    body = "\n".join(head)

    meta = f"# entry = {name}\n# offset = {off:,}  size = {size:,}  total_lines = {len(lines)}\n"
    out = meta + body
    if a.out:
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        with open(a.out, "w", encoding="utf-8") as g:
            g.write(out)
        print(f"已写出 {a.out}（{len(head)} 行 / 共 {len(lines)} 行）")
    else:
        print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
