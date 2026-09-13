"""检查安装权限/磁盘，并解压部署 CMake"""
import os, sys, ctypes, shutil, zipfile, time
sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DL = os.path.join(ROOT, ".workbuddy", "downloads")

print("=" * 70)
print("一、权限")
print("=" * 70)
try:
    admin = bool(ctypes.windll.shell32.IsUserAnAdmin())
except Exception as e:
    admin = False
print(f"  当前进程管理员权限: {'是' if admin else '否'}")
print(f"  用户: {os.environ.get('USERNAME')}")

print()
print("=" * 70)
print("二、磁盘空间")
print("=" * 70)
for d in ["C:\\", "E:\\", "F:\\"]:
    try:
        u = shutil.disk_usage(d)
        print(f"  {d}  可用 {u.free/1024**3:>8.1f} GB / 共 {u.total/1024**3:>8.1f} GB")
    except Exception as e:
        print(f"  {d}  {e}")

print()
print("=" * 70)
print("三、部署 CMake（免安装）")
print("=" * 70)
zips = [f for f in os.listdir(DL) if f.lower().startswith("cmake") and f.lower().endswith(".zip")]
if not zips:
    print("  未找到 CMake 压缩包")
else:
    zp = os.path.join(DL, zips[0])
    dest_root = r"%USERPROFILE%\.workbuddy\binaries\cmake"
    os.makedirs(dest_root, exist_ok=True)
    t0 = time.time()
    with zipfile.ZipFile(zp) as z:
        names = z.namelist()
        top = names[0].split("/")[0]
        z.extractall(dest_root)
    cmake_exe = os.path.join(dest_root, top, "bin", "cmake.exe")
    print(f"  解压 {zips[0]} -> {dest_root}  ({time.time()-t0:.1f}s, {len(names)} 项)")
    print(f"  cmake.exe 存在: {os.path.exists(cmake_exe)}")
    print(f"  路径: {cmake_exe}")
    if os.path.exists(cmake_exe):
        import subprocess
        try:
            r = subprocess.run([cmake_exe, "--version"], capture_output=True, text=True, timeout=30)
            print("  版本: " + (r.stdout or r.stderr).strip().splitlines()[0])
        except Exception as e:
            print(f"  版本检查失败: {e}")
    ninja = os.path.join(dest_root, top, "bin", "ninja.exe")
    print(f"  附带 ninja: {os.path.exists(ninja)}")
