import os, shutil, sys, subprocess, json

out = []
def w(s=""):
    out.append(str(s))

# 1) GitHub 相关工具与凭据探测
w("=== GitHub 工具探测 ===")
for t in ["gh", "git", "ssh"]:
    w(f"{t}: {shutil.which(t)}")
home = os.path.expanduser("~")
w(f"HOME: {home}")
w(f".git-credentials exists: {os.path.exists(os.path.join(home, '.git-credentials'))}")
w(f".ssh dir: {os.listdir(os.path.join(home, '.ssh')) if os.path.exists(os.path.join(home,'.ssh')) else 'NOT FOUND'}")

# 2) 游戏目录盘点
w("")
w("=== 游戏目录盘点 ===")
game = r"E:\Program Files (x86)\steam\steamapps\common\Sango3"
w(f"exists: {os.path.exists(game)}")
if os.path.exists(game):
    entries = sorted(os.listdir(game))
    w(f"顶层条目数: {len(entries)}")
    # 先列顶层：目录在前，文件带大小
    dirs = [e for e in entries if os.path.isdir(os.path.join(game, e))]
    files = [e for e in entries if os.path.isfile(os.path.join(game, e))]
    w(f"--- 目录 ({len(dirs)}) ---")
    for d in dirs:
        try:
            n = sum(len(fs) for _, _, fs in os.walk(os.path.join(game, d)))
        except Exception:
            n = -1
        w(f"  [DIR] {d}  (含文件 {n})")
    w(f"--- 文件 ({len(files)}) ---")
    big = []
    for f in files:
        p = os.path.join(game, f)
        try:
            sz = os.path.getsize(p)
        except Exception:
            sz = -1
        big.append((sz, f))
    for sz, f in sorted(big, reverse=True)[:60]:
        w(f"  {sz:>12,}  {f}")
    if len(big) > 60:
        w(f"  ... 其余 {len(big)-60} 个文件略（总大小 {sum(s for s,_ in big):,} 字节）")

sys.stdout.reconfigure(encoding="utf-8")
print("\n".join(out))
