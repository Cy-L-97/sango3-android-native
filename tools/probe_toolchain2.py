# -*- coding: utf-8 -*-
"""VS Build Tools 安装后：验证 MSVC 工具链与 CMake 联合可用性"""
import os, subprocess, sys, glob

sys.stdout.reconfigure(encoding="utf-8")

def sh(cmd, cwd=None, env=None):
    try:
        p = subprocess.run(cmd, shell=True, cwd=cwd, env=env,
                           capture_output=True, text=True, encoding="utf-8",
                           errors="replace", timeout=300)
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    except Exception as e:
        return -1, f"<EXC {e}>"

print("=" * 70)
print("1) VS Build Tools 安装目录检查")
for d in [r"C:\BuildTools", r"C:\BuildTools\VC\Tools\MSVC"]:
    print(f"  {d}  exists={os.path.isdir(d)}")
    if os.path.isdir(d):
        for n in sorted(os.listdir(d))[:10]:
            print(f"      {n}")

print()
print("=" * 70)
print("2) 定位 vcvars64.bat / cl.exe / link.exe")
vcvars = glob.glob(r"C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat")
print(f"  vcvars64.bat: {vcvars}")
cls = glob.glob(r"C:\BuildTools\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe")
print(f"  cl.exe: {cls}")
links = glob.glob(r"C:\BuildTools\VC\Tools\MSVC\*\bin\Hostx64\x64\link.exe")
print(f"  link.exe: {links}")

print()
print("=" * 70)
print("3) Windows SDK 检查")
sdks = glob.glob(r"C:\Program Files (x86)\Windows Kits\10\Include\*")
print(f"  SDK Include 版本: {[os.path.basename(s) for s in sdks]}")
libs = glob.glob(r"C:\Program Files (x86)\Windows Kits\10\Lib\*\um\x64\kernel32.lib")
print(f"  kernel32.lib: {libs[:3]}")

print()
print("=" * 70)
print("4) CMake 检查")
cm = os.path.expanduser(r"~\.workbuddy\binaries\cmake\cmake-4.4.3-windows-x86_64\bin\cmake.exe")
print(f"  exists={os.path.isfile(cm)}")
rc, out = sh(f'"{cm}" --version')
print(f"  rc={rc}  {(out.strip().splitlines() or [""])[0]}")

print()
print("=" * 70)
print("5) 联合编译验证：cl.exe 编出 hello.exe")
if vcvars and cls:
    work = os.path.join(os.environ.get("TEMP", r"C:\Temp"), "sango_build_test")
    os.makedirs(work, exist_ok=True)
    print(f"  work dir: {work}")
    csrc = os.path.join(work, "hello.c")
    with open(csrc, "w", encoding="ascii") as f:
        f.write('#include <stdio.h>\nint main(void){printf("HELLO_FROM_MSVC\\n");return 0;}\n')
    bat = os.path.join(work, "build.bat")
    with open(bat, "w", encoding="gbk", errors="replace") as f:
        f.write('@echo off\r\n')
        f.write(f'call "{vcvars[0]}" >nul 2>&1\r\n')
        f.write(f'cd /d "{work}"\r\n')
        f.write('cl /nologo /Fe:hello.exe hello.c\r\n')
        f.write('echo CL_EXIT=%errorlevel%\r\n')
        f.write('hello.exe\r\n')
        f.write('echo RUN_EXIT=%errorlevel%\r\n')
    rc, out = sh(f'cmd /c "{bat}"')
    print(f"  rc={rc}")
    print("  --- output ---")
    for line in out.splitlines():
        print(f"  | {line}")
    exe = os.path.join(work, "hello.exe")
    print(f"  hello.exe exists={os.path.isfile(exe)}  size={os.path.getsize(exe) if os.path.isfile(exe) else 0}")
else:
    print("  跳过：vcvars64.bat 或 cl.exe 未找到")
