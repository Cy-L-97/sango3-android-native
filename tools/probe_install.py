"""检查 VS Build Tools 安装进度"""
import os, sys, subprocess
sys.stdout.reconfigure(encoding="utf-8")

print("=" * 70)
print("一、安装目录")
print("=" * 70)
for d in [r"C:\BuildTools", r"C:\Program Files (x86)\Microsoft Visual Studio",
          r"C:\Program Files\Microsoft Visual Studio"]:
    if os.path.exists(d):
        try:
            subs = os.listdir(d)
        except Exception:
            subs = ["(无法列出)"]
        print(f"  存在 {d} -> {subs[:8]}")
    else:
        print(f"  暂无 {d}")

vs = r"C:\BuildTools\VC\Tools\MSVC"
if os.path.isdir(vs):
    print(f"  MSVC 工具集: {os.listdir(vs)}")
else:
    print("  MSVC 工具集: 尚未出现")

print()
print("=" * 70)
print("二、相关进程")
print("=" * 70)
try:
    r = subprocess.run([r"C:\Windows\System32\tasklist.exe", "/FO", "CSV", "/NH"],
                       capture_output=True, text=True, timeout=30, errors="replace")
    targets = ["vs_", "vs_BuildTools", "setup.exe", "vs_installer", "vs_installershell",
               "MSBuild", "ServiceHub", "VSIXInstaller", "vsix"]
    hits = []
    for line in (r.stdout or "").splitlines():
        low = line.lower()
        if any(t.lower() in low for t in targets):
            hits.append(line.strip())
    if hits:
        for h in hits[:20]:
            print("  " + h)
    else:
        print("  未发现安装相关进程")
except Exception as e:
    print(f"  查询失败: {e}")

print()
print("=" * 70)
print("三、引导程序日志")
print("=" * 70)
temp = os.environ.get("TEMP", os.path.expanduser(r"~\AppData\Local\Temp"))
import glob
for pat in ["dd_bootstrapper*.log", "dd_setup*.log", "dd_installer*.log"]:
    for p in glob.glob(os.path.join(temp, pat)):
        print(f"  {p}  ({os.path.getsize(p):,} B, 修改于 {__import__('time').strftime('%H:%M:%S', __import__('time').localtime(os.path.getmtime(p)))})")
# 日志尾部
logs = sorted(glob.glob(os.path.join(temp, "dd_*.log")), key=os.path.getmtime)
if logs:
    p = logs[-1]
    print(f"\n  --- {os.path.basename(p)} 末尾 15 行 ---")
    try:
        with open(p, encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
        for ln in lines[-15:]:
            print("   " + ln.rstrip()[:160])
    except Exception as e:
        print(f"   读取失败: {e}")

print()
print("=" * 70)
print("四、后台任务 stdout")
print("=" * 70)
p = r"E:\用户\workbuddy\移植PC计划\.workbuddy\vs_install_stdout.txt"
if os.path.exists(p):
    print(f"  {os.path.getsize(p)} B")
    with open(p, encoding="utf-8", errors="replace") as f:
        print("  " + f.read()[:800])
else:
    print("  尚未产生")
