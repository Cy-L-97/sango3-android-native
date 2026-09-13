"""下载工具链安装器（VS Build Tools 引导程序 + CMake）"""
import os, sys, urllib.request, json, re
sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DL = os.path.join(ROOT, ".workbuddy", "downloads")
os.makedirs(DL, exist_ok=True)

def fetch(url, name, min_size=1000):
    dst = os.path.join(DL, name)
    if os.path.exists(dst) and os.path.getsize(dst) > min_size:
        print(f"[跳过] {name} 已存在 ({os.path.getsize(dst):,} B)")
        return dst
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=120) as r, open(dst, "wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        done = 0
        while True:
            chunk = r.read(1 << 16)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            if total:
                print(f"  {name} {done*100//total}% ({done:,}/{total:,})")
    print(f"[完成] {name}  {os.path.getsize(dst):,} B  -> {dst}")
    return dst

print("=" * 70)
print("1) VS Build Tools 引导程序")
print("=" * 70)
try:
    fetch("https://aka.ms/vs/17/release/vs_BuildTools.exe", "vs_BuildTools.exe", 1_000_000)
except Exception as e:
    print(f"  失败: {type(e).__name__}: {e}")

print()
print("=" * 70)
print("2) 查询 CMake 最新版")
print("=" * 70)
ver = None
try:
    req = urllib.request.Request("https://api.github.com/repos/Kitware/CMake/releases/latest",
                                 headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=30) as r:
        j = json.loads(r.read().decode("utf-8"))
    tag = j.get("tag_name", "")
    ver = tag.lstrip("v")
    print(f"  最新版: {tag}")
    asset = f"cmake-{ver}-windows-x86_64.zip"
    url = f"https://github.com/Kitware/CMake/releases/download/{tag}/{asset}"
    print(f"  资产: {asset}")
    fetch(url, asset, 5_000_000)
except Exception as e:
    print(f"  查询/下载失败: {type(e).__name__}: {e}")
    ver = "3.31.6"
    print(f"  回退到固定版本 v{ver}")
    try:
        fetch(f"https://github.com/Kitware/CMake/releases/download/v{ver}/cmake-{ver}-windows-x86_64.zip",
              f"cmake-{ver}-windows-x86_64.zip", 5_000_000)
    except Exception as e2:
        print(f"  回退也失败: {type(e2).__name__}: {e2}")

print()
print("下载目录内容:")
for n in sorted(os.listdir(DL)):
    p = os.path.join(DL, n)
    print(f"  {os.path.getsize(p):>12,}  {n}")
