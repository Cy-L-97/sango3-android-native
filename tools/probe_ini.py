"""导出并检视 Update.PAK 中的规则数据表（Setting 目录）"""
import os, sys
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pak import read_index

GAME = r"E:\Program Files (x86)\steam\steamapps\common\Sango3"
OUT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".workbuddy", "data"))

entries, _, _, _ = read_index(os.path.join(GAME, "Update.PAK"))
os.makedirs(OUT, exist_ok=True)
targets = [(n, o, s) for n, o, s in entries if n.lower().startswith("setting")]
print(f"=== Update.PAK 中 Setting 文件: {len(targets)} 个 ===")
with open(os.path.join(GAME, "Update.PAK"), "rb") as f:
    for n, off, size in targets:
        rel = n.replace("\\", "/")
        dst = os.path.join(OUT, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        f.seek(off)
        data = f.read(size)
        with open(dst, "wb") as g:
            g.write(data)
        print(f"  {size:>9,}  {rel}")

def try_read(path):
    with open(path, "rb") as f:
        raw = f.read()
    for enc in ("gbk", "big5", "utf-8", "latin1"):
        try:
            return raw.decode(enc), enc
        except UnicodeDecodeError:
            continue
    return raw.decode("latin1"), "latin1"

print("\n\n" + "=" * 78)
print("=== 关键数据表内容预览 ===")
key_files = ["General01.ini", "Game.ini", "Soldier.ini", "BFMagic.ini", "Thing.ini", "Menu.ini"]
for kf in key_files:
    p = None
    for r, ds, fs in os.walk(OUT):
        for f in fs:
            if f.lower() == kf.lower():
                p = os.path.join(r, f)
                break
        if p:
            break
    if not p:
        print(f"\n--- {kf}: 未找到 ---")
        continue
    txt, enc = try_read(p)
    lines = txt.splitlines()
    print(f"\n{'='*78}")
    print(f"--- {kf}  (编码={enc}, {len(lines)} 行) ---")
    for i, ln in enumerate(lines[:45]):
        print(f"  {i:4d}| {ln}")
    if len(lines) > 45:
        print(f"  ... 共 {len(lines)} 行")
