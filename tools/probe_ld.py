"""确认雷电模拟器路径 + 探测包管理器（为安装工具链做准备）"""
import os, sys, shutil, subprocess
sys.stdout.reconfigure(encoding="utf-8")

print("=" * 70)
print("一、雷电模拟器 F:\\leidian")
print("=" * 70)
LD = r"F:\leidian"
if os.path.isdir(LD):
    for name in sorted(os.listdir(LD)):
        p = os.path.join(LD, name)
        kind = "DIR " if os.path.isdir(p) else "FILE"
        sz = "" if os.path.isdir(p) else f"{os.path.getsize(p):,}"
        print(f"  {kind} {sz:>12}  {name}")
else:
    print(f"  不存在: {LD}")
    if os.path.isdir("F:\\"):
        print("  F:\\ 内容:", os.listdir("F:\\"))

print()
print("=" * 70)
print("二、雷电关键可执行文件")
print("=" * 70)
for pat in ["dnplayer.exe", "ldconsole.exe", "adb.exe", "ld9box.exe"]:
    hits = []
    for root, ds, fs in os.walk(LD):
        depth = root[len(LD):].count(os.sep)
        if depth > 3:
            ds[:] = []
            continue
        for f in fs:
            if f.lower() == pat:
                hits.append(os.path.join(root, f))
    for h in hits[:4]:
        print(f"  {pat:<16} {h}")
    if not hits:
        print(f"  {pat:<16} (未找到)")

print()
print("=" * 70)
print("三、包管理器可用性")
print("=" * 70)
for exe in ["winget", "choco", "scoop", "curl", "tar", "bitsadmin", "certutil"]:
    p = shutil.which(exe)
    print(f"  {exe:<12} {p if p else '--'}")

print()
print("=" * 70)
print("四、网络连通性（安装源）")
print("=" * 70)
import urllib.request, socket
socket.setdefaulttimeout(6)
tests = [
    ("微软 BuildTools 官方直链", "https://aka.ms/vs/17/release/vs_BuildTools.exe"),
    ("CMake 官方", "https://github.com/Kitware/CMake/releases/latest"),
    ("CMake 清华镜像", "https://mirrors.tuna.tsinghua.edu.cn/github-release/Kitware/CMake/"),
    ("winget 源", "https://cdn.winget.microsoft.com/cache"),
]
for label, url in tests:
    try:
        req = urllib.request.Request(url, method="HEAD", headers={"User-Agent": "Mozilla/5.0"})
        r = urllib.request.urlopen(req)
        print(f"  OK   {r.status}  {label}")
    except Exception as e:
        print(f"  FAIL {type(e).__name__}  {label}  ({e})")
