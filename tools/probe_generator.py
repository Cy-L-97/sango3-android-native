# -*- coding: utf-8 -*-
"""探测 nmake / ninja 可用性，并为后续 CMake 构建选定 generator。"""
import os, sys, glob, shutil

sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from msvc_env import make_env, run, NMAKE, CMAKE, check_components  # noqa: E402

env = make_env()

print("=" * 70)
print("组件自检")
for k, v in check_components().items():
    print(f"  {'OK ' if v else 'MISS'}  {k}")

print()
print("=" * 70)
print("nmake 可用性")
print(f"  path={NMAKE} exists={os.path.isfile(NMAKE)}")
rc, out = run(f'"{NMAKE}" /?', env=env)
print(f"  nmake rc={rc}")
for line in out.splitlines()[:4]:
    print(f"  | {line}")

print()
print("=" * 70)
print("ninja 可用性（先看 PATH，再看 pip 是否装了）")
ninja = shutil.which("ninja")
print(f"  shutil.which(ninja) = {ninja}")
cands = [
    r"%USERPROFILE%\.workbuddy\binaries\ninja\ninja.exe",
    r"C:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
]
for c in cands:
    print(f"  {c}  exists={os.path.isfile(c)}")
rc, out = run("ninja --version", env=env)
print(f"  ninja --version rc={rc} out={out.strip()!r}")

print()
print("=" * 70)
print("cmake generator 列表")
rc, out = run(f'"{CMAKE}" --help', env=env)
for line in out.splitlines():
    s = line.strip()
    if any(k in s for k in ("NMake", "Ninja", "Visual Studio", "Unix Makefiles")):
        print(f"  | {s}")
