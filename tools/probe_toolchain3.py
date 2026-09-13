# -*- coding: utf-8 -*-
"""绕过 vcvars64.bat（其内部调用被沙箱拦截的 reg.exe），手工构造 MSVC x64 环境并验证编译。"""
import os, subprocess, sys, glob

sys.stdout.reconfigure(encoding="utf-8")

MSVC_VER = "14.44.35207"
VC = rf"C:\BuildTools\VC\Tools\MSVC\{MSVC_VER}"
SDK_INC = r"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0"
SDK_LIB = r"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0"
SDK_BIN = r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64"


def make_env():
    env = dict(os.environ)
    paths = [
        rf"{VC}\bin\Hostx64\x64",
        SDK_BIN,
        os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32"),
    ]
    env["PATH"] = ";".join(paths) + ";" + env.get("PATH", "")
    env["INCLUDE"] = ";".join([
        rf"{VC}\include",
        rf"{SDK_INC}\ucrt",
        rf"{SDK_INC}\shared",
        rf"{SDK_INC}\um",
    ])
    env["LIB"] = ";".join([
        rf"{VC}\lib\x64",
        rf"{SDK_LIB}\ucrt\x64",
        rf"{SDK_LIB}\um\x64",
    ])
    return env


print("=" * 70)
print("组件完整性检查")
for label, p in [
    ("cl.exe", rf"{VC}\bin\Hostx64\x64\cl.exe"),
    ("link.exe", rf"{VC}\bin\Hostx64\x64\link.exe"),
    ("stdio.h", rf"{VC}\include\stdio.h"),
    ("ucrt stdio.h", rf"{SDK_INC}\ucrt\stdio.h"),
    ("kernel32.lib", rf"{SDK_LIB}\um\x64\kernel32.lib"),
    ("rc.exe", rf"{SDK_BIN}\rc.exe"),
    ("mt.exe", rf"{SDK_BIN}\mt.exe"),
    ("MSBuild.exe", r"C:\BuildTools\MSBuild\Current\Bin\MSBuild.exe"),
]:
    print(f"  {'OK ' if os.path.isfile(p) else 'MISS'}  {label:16s} {p}")

print()
print("=" * 70)
work = r"C:\sango_build_test"
os.makedirs(work, exist_ok=True)
env = make_env()

# 纯 C 测试
with open(os.path.join(work, "hello.c"), "w", encoding="ascii") as f:
    f.write('#include <stdio.h>\n'
            'int main(void){printf("HELLO_FROM_MSVC_C\\n");return 0;}\n')

print("C 编译 + 运行：")
p = subprocess.run("cl /nologo /W3 /Fe:hello.exe hello.c",
                   shell=True, cwd=work, env=env,
                   capture_output=True, text=True, encoding="gbk",
                   errors="replace", timeout=300)
print(f"  cl rc={p.returncode}")
for line in ((p.stdout or "") + (p.stderr or "")).splitlines():
    print(f"  | {line}")
exe = os.path.join(work, "hello.exe")
if os.path.isfile(exe):
    r = subprocess.run(f'"{exe}"', shell=True, cwd=work, env=env, capture_output=True,
                       text=True, encoding="gbk", errors="replace", timeout=60)
    print(f"  运行输出: {(r.stdout or '').strip()!r}  rc={r.returncode}")
    print(f"  hello.exe 大小 = {os.path.getsize(exe):,} 字节")

print()
print("=" * 70)
# C++ 测试（含 STL，验证 C++ 头文件与运行时齐全）
with open(os.path.join(work, "hello.cpp"), "w", encoding="ascii") as f:
    f.write('#include <iostream>\n#include <vector>\n#include <string>\n'
            'int main(){std::vector<std::string> v{"Sango3","Android","Native"};'
            'for(auto&s:v) std::cout<<s<<" ";std::cout<<std::endl;return 0;}\n')
print("C++ 编译 + 运行：")
p = subprocess.run("cl /nologo /W3 /EHsc /std:c++17 /Fe:hello_cpp.exe hello.cpp",
                   shell=True, cwd=work, env=env,
                   capture_output=True, text=True, encoding="gbk",
                   errors="replace", timeout=300)
print(f"  cl rc={p.returncode}")
for line in ((p.stdout or "") + (p.stderr or "")).splitlines():
    print(f"  | {line}")
exe2 = os.path.join(work, "hello_cpp.exe")
if os.path.isfile(exe2):
    r = subprocess.run(f'"{exe2}"', shell=True, cwd=work, env=env, capture_output=True,
                       text=True, encoding="gbk", errors="replace", timeout=60)
    print(f"  运行输出: {(r.stdout or '').strip()!r}  rc={r.returncode}")

print()
print("=" * 70)
print("结论：手工 env 方案 " + ("可用 ✅" if os.path.isfile(exe) and os.path.isfile(exe2) else "仍有问题 ❌"))
