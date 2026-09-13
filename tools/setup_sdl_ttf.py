# -*- coding: utf-8 -*-
"""
setup_sdl_ttf.py —— 把 SDL2_ttf 开发库部署到 third_party/SDL2_ttf

SDL2_ttf 是 SDL2 的独立扩展库（不像 SDL3 已并入核心），需单独部署。
third_party/ 已 gitignore，各机器执行本脚本即可复现，不入库。

产物:
  third_party/SDL2_ttf/include/SDL_ttf.h
  third_party/SDL2_ttf/lib/x64/SDL2_ttf.lib
  third_party/SDL2_ttf/lib/x64/SDL2_ttf.dll

用法:
  python tools/setup_sdl_ttf.py            # 若 zip 已存在则直接解压；否则先下载
  python tools/setup_sdl_ttf.py --clean    # 删除 third_party/SDL2_ttf
  python tools/setup_sdl_ttf.py --no-fetch # 仅解压，不下载（zip 需已存在于 downloads）
"""
import os
import shutil
import sys
import zipfile

try:
    import urllib.request as urllib_req
except Exception:  # pragma: no cover
    urllib_req = None

sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DL = os.path.join(ROOT, ".workbuddy", "downloads")
TP = os.path.join(ROOT, "third_party")
DEST = os.path.join(TP, "SDL2_ttf")

# 与 SDL2 2.32.x 配套的 SDL2_ttf 稳定版（2025 年发布）
ZIP_NAME = "SDL2_ttf-devel-2.24.0-VC.zip"
ZIP_URL = ("https://github.com/libsdl-org/SDL_ttf/releases/download/"
           "release-2.24.0/" + ZIP_NAME)
TOP = "SDL2_ttf-2.24.0"  # zip 内顶层目录名


def extract_zip_flat(zip_path, dst, want_top):
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


def fetch():
    if not urllib_req:
        print("无 urllib，无法下载；请手动把 %s 放到 %s" % (ZIP_NAME, DL))
        return False
    os.makedirs(DL, exist_ok=True)
    dst = os.path.join(DL, ZIP_NAME)
    if os.path.isfile(dst):
        print("已存在，跳过下载: %s" % dst)
        return True
    print("下载 %s ..." % ZIP_URL)
    req = urllib_req.Request(ZIP_URL, headers={"User-Agent": "Mozilla/5.0"})
    try:
        with urllib_req.urlopen(req, timeout=180) as resp:
            data = resp.read()
        with open(dst, "wb") as f:
            f.write(data)
    except Exception as e:  # noqa: BLE001
        print("下载失败: %s" % e)
        return False
    print("  已保存 %s (%d 字节)" % (dst, os.path.getsize(dst)))
    return True


def report():
    print("- SDL2_ttf: %s" % DEST)
    probes = ["include/SDL_ttf.h", "lib/x64/SDL2_ttf.lib", "lib/x64/SDL2_ttf.dll"]
    ok = True
    for rel in probes:
        p = os.path.join(DEST, rel)
        ex = os.path.exists(p)
        ok = ok and ex
        print("    %s  %s" % ("OK " if ex else "MISS", rel))
    return ok


def main():
    if "--clean" in sys.argv:
        if os.path.isdir(DEST):
            shutil.rmtree(DEST)
            print("已删除 %s" % DEST)
        return 0

    no_fetch = "--no-fetch" in sys.argv
    zip_path = os.path.join(DL, ZIP_NAME)
    if not os.path.isfile(zip_path):
        if no_fetch:
            print("缺少 %s 且 --no-fetch，退出" % zip_path)
            return 2
        if not fetch():
            return 2

    print("=" * 70)
    print("解压 SDL2_ttf 开发库")
    extract_zip_flat(zip_path, DEST, TOP)
    ok = report()
    print()
    print("结果：SDL2_ttf %s" % ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
