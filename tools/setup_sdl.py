# -*- coding: utf-8 -*-
"""
setup_sdl.py —— 把 .workbuddy/downloads 里的 SDL2 包部署到 third_party/

产物（third_party/ 已 gitignore，各机器执行本脚本即可复现）：
  third_party/SDL2/        Windows/VC 开发库（include + lib + dll + cmake 配置）
  third_party/SDL2-src/    SDL2 源码（Android 端用 NDK 构建时使用）

用法: python tools/setup_sdl.py [--clean]
"""
import os
import shutil
import sys
import tarfile
import zipfile

sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DL = os.path.join(ROOT, ".workbuddy", "downloads")
TP = os.path.join(ROOT, "third_party")

DEV_ZIP = os.path.join(DL, "SDL2-devel-2.32.10-VC.zip")
SRC_TAR = os.path.join(DL, "SDL2-2.32.10.tar.gz")

DEV_DIR = os.path.join(TP, "SDL2")
SRC_DIR = os.path.join(TP, "SDL2-src")


def extract_zip_flat(zip_path, dst, want_top):
    """解压 zip，把顶层目录 want_top 的内容直接放到 dst。"""
    os.makedirs(dst, exist_ok=True)
    with zipfile.ZipFile(zip_path) as z:
        for info in z.infolist():
            n = info.filename
            if not n.startswith(want_top + "/"):
                continue
            rel = n[len(want_top) + 1:]
            if not rel:
                continue
            target = os.path.join(dst, rel)
            if info.is_dir():
                os.makedirs(target, exist_ok=True)
            else:
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with z.open(info) as src, open(target, "wb") as out:
                    shutil.copyfileobj(src, out)


def extract_tar_flat(tar_path, dst, want_top):
    os.makedirs(dst, exist_ok=True)
    with tarfile.open(tar_path) as t:
        for m in t.getmembers():
            n = m.name
            if not n.startswith(want_top + "/"):
                continue
            rel = n[len(want_top) + 1:]
            if not rel:
                continue
            target = os.path.join(dst, rel)
            if m.isdir():
                os.makedirs(target, exist_ok=True)
            elif m.isfile():
                os.makedirs(os.path.dirname(target), exist_ok=True)
                f = t.extractfile(m)
                if f:
                    with f, open(target, "wb") as out:
                        shutil.copyfileobj(f, out)


def report(dst, label, probes):
    print(f"- {label}: {dst}")
    for rel in probes:
        p = os.path.join(dst, rel)
        print(f"    {'OK ' if os.path.exists(p) else 'MISS'}  {rel}")
    return all(os.path.exists(os.path.join(dst, r)) for r in probes)


def main():
    if "--clean" in sys.argv:
        for d in (DEV_DIR, SRC_DIR):
            if os.path.isdir(d):
                shutil.rmtree(d)
                print(f"已删除 {d}")
        return 0

    os.makedirs(TP, exist_ok=True)
    if not os.path.isfile(DEV_ZIP):
        print(f"缺少 {DEV_ZIP}，请先运行 tools/fetch_sdl.py")
        return 2
    if not os.path.isfile(SRC_TAR):
        print(f"缺少 {SRC_TAR}，请先运行 tools/fetch_sdl.py")
        return 2

    print("=" * 70)
    print("解压 SDL2 开发库")
    extract_zip_flat(DEV_ZIP, DEV_DIR, "SDL2-2.32.10")
    ok1 = report(DEV_DIR, "SDL2 (VC)",
                 ["include/SDL.h", "include/SDL_version.h",
                  "lib/x64/SDL2.lib", "lib/x64/SDL2main.lib",
                  "lib/x64/SDL2.dll"])

    print()
    print("=" * 70)
    print("解压 SDL2 源码")
    extract_tar_flat(SRC_TAR, SRC_DIR, "SDL2-2.32.10")
    ok2 = report(SRC_DIR, "SDL2-src",
                 ["CMakeLists.txt", "include/SDL.h", "src/SDL.c",
                  "android-project/app/build.gradle"])

    print()
    print("=" * 70)
    print("查找 CMake 配置（便于 CMake 直接 find_package(SDL2)）")
    hits = []
    for r, _d, fs in os.walk(DEV_DIR):
        for f in fs:
            if f.endswith(".cmake") or f.endswith("Config.cmake"):
                hits.append(os.path.relpath(os.path.join(r, f), DEV_DIR))
    for h in sorted(hits):
        print(f"  {h}")
    if not hits:
        print("  （无 cmake 配置，将在 CMakeLists 中手工设置 include/lib 路径）")

    print()
    print("=" * 70)
    print(f"结果：开发库 {'✅' if ok1 else '❌'}  源码 {'✅' if ok2 else '❌'}")
    return 0 if (ok1 and ok2) else 1


if __name__ == "__main__":
    sys.exit(main())
