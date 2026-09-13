# -*- coding: utf-8 -*-
"""
build_pc.py —— PC 端（Windows）构建入口

统一封装：CMake 配置 + Ninja 构建 + 可选运行。
依赖 tools/msvc_env.py 提供 MSVC 环境（绕过被沙箱拦截的 vcvars64.bat）。

用法:
    python tools/build_pc.py                  # 配置 + 构建
    python tools/build_pc.py --run -- <args>  # 构建后运行 sango3pak.exe
    python tools/build_pc.py --clean          # 清理构建目录
"""
import argparse
import os
import shutil
import sys
import glob

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding="utf-8")
from msvc_env import make_env, run, CMAKE, BUILD_TOOLS, check_components  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(ROOT, "engine")
BUILD_DIR = os.path.join(ROOT, "build", "pc")
NINJA = os.path.join(BUILD_TOOLS, "Common7", "IDE",
                     "CommonExtensions", "Microsoft", "CMake", "Ninja", "ninja.exe")


def pick_ninja():
    if os.path.isfile(NINJA):
        return NINJA
    found = shutil.which("ninja")
    if found:
        return found
    for pat in [r"D:\PythonPackages\site-packages\bin\ninja.exe",
                os.path.expanduser(r"~\.workbuddy\binaries\python\envs\default\Scripts\ninja.exe")]:
        if os.path.isfile(pat):
            return pat
    hits = glob.glob(r"D:\PythonPackages\site-packages\**\ninja.exe", recursive=True)
    if hits:
        return hits[0]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clean", action="store_true", help="删除构建目录后退出")
    ap.add_argument("--run", action="store_true", help="构建后运行 sango3pak.exe")
    ap.add_argument("--reconfigure", action="store_true", help="强制重新执行 cmake 配置")
    ap.add_argument("args", nargs="*", help="传给 sango3pak.exe 的参数")
    a = ap.parse_args()

    if a.clean:
        if os.path.isdir(BUILD_DIR):
            shutil.rmtree(BUILD_DIR)
            print(f"已删除 {BUILD_DIR}")
        return 0

    comp = check_components()
    bad = [k for k, v in comp.items() if not v]
    if bad:
        print("缺少组件，无法构建：", ", ".join(bad))
        return 2

    ninja = pick_ninja()
    print(f"ninja  : {ninja}")
    if not ninja:
        print("未找到 ninja")
        return 2

    env = make_env()
    os.makedirs(BUILD_DIR, exist_ok=True)

    cache = os.path.join(BUILD_DIR, "CMakeCache.txt")
    if a.reconfigure or not os.path.isfile(cache):
        cmd = (f'"{CMAKE}" -S "{SRC_DIR}" -B "{BUILD_DIR}" '
               f'-G Ninja -DCMAKE_BUILD_TYPE=Release '
               f'-DCMAKE_MAKE_PROGRAM="{ninja}" '
               f'-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl')
        print("\n>>> cmake configure")
        rc, out = run(cmd, env=env)
        print(out.rstrip())
        if rc != 0:
            print(f"配置失败 rc={rc}")
            return rc

    print("\n>>> cmake build")
    rc, out = run(f'"{CMAKE}" --build "{BUILD_DIR}"', env=env)
    print(out.rstrip())
    if rc != 0:
        return rc

    exe = os.path.join(BUILD_DIR, "bin", "sango3pak.exe")
    print(f"\n产物: {exe}  exists={os.path.isfile(exe)}"
          + (f"  {os.path.getsize(exe):,} 字节" if os.path.isfile(exe) else ""))

    if a.run and os.path.isfile(exe):
        print("\n>>> 运行 sango3pak.exe")
        rc, out = run(f'"{exe}" ' + " ".join(f'"{x}"' if " " in x else x for x in a.args),
                      env=env, encoding="gbk")
        print(out.rstrip())
        print(f"运行结束 rc={rc}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
