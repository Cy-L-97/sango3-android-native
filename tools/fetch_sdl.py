# -*- coding: utf-8 -*-
"""
fetch_sdl.py —— 获取 SDL2 开发库（Windows/VC 版）与源码包（Android 用）

网络背景（2026-09 实测）：
  api.github.com   -> 200 OK
  github.com       -> 直连被拒(RemoteDisconnected)
  libsdl.org       -> 200 OK
  gh-proxy.com     -> 200 OK（可用镜像）
  dl.google.com    -> 200 OK

因此策略：用 API 查版本 -> 用 libsdl.org 官方 release 目录下载 -> 失败则走 gh-proxy.com 镜像。
产物落在 .workbuddy/downloads/（已 gitignore）。

注意：SDL 官方 releases 的 "latest" 现指向 SDL3。本项目**刻意选用 SDL2**：
成熟稳定、Android 集成资料丰富、与老游戏 2D 渲染需求匹配。故这里按 release-2.x 精确取版。
"""
import json
import os
import ssl
import sys
import time
import urllib.request
import urllib.error
import zipfile
import tarfile

sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DL = os.path.join(ROOT, ".workbuddy", "downloads")
os.makedirs(DL, exist_ok=True)

CTX = ssl.create_default_context()
CTX.check_hostname = False
CTX.verify_mode = ssl.CERT_NONE
UA = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}

MIRRORS = [
    ("official", ""),
    ("gh-proxy.com", "https://gh-proxy.com/"),
]


def fetch(url, timeout=25):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=timeout, context=CTX) as r:
        return r.read()


def get_json(url, timeout=25):
    return json.loads(fetch(url, timeout).decode("utf-8"))


def download(url, dst, timeout=300, chunk=1 << 16):
    t0 = time.time()
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=timeout, context=CTX) as r, open(dst, "wb") as f:
        total = 0
        while True:
            b = r.read(chunk)
            if not b:
                break
            f.write(b)
            total += len(b)
    return total, time.time() - t0


def try_download(name, rel_url_paths, min_size=50_000):
    """rel_url_paths: [(label, url), ...]，逐个镜像尝试。返回本机路径或 None。"""
    dst = os.path.join(DL, name)
    if os.path.isfile(dst) and os.path.getsize(dst) > min_size:
        print(f"  [已存在] {name}  {os.path.getsize(dst):,} 字节")
        return dst
    for label, url in rel_url_paths:
        print(f"  [{label}] {name} ...", end=" ", flush=True)
        try:
            got, dt = download(url, dst)
            kb = got / 1024
            print(f"OK {got:,} 字节 ({kb:,.0f} KB, {dt:.1f}s)")
            if got < min_size:
                print(f"    ! 体积异常偏小，可能不是有效文件")
            return dst
        except Exception as e:  # noqa: BLE001
            print(f"失败 {type(e).__name__}: {str(e)[:70]}")
            if os.path.isfile(dst):
                try:
                    os.remove(dst)
                except OSError:
                    pass
    return None


def mirror_urls(rel_path):
    """rel_path 形如 libsdl-org/SDL/releases/download/release-2.32.10/xxx.zip
    同时给出 libsdl.org 官方 release 目录（SDL 长期在 https://www.libsdl.org/release/ 放包）。"""
    base = "https://github.com/" + rel_path
    out = [("github", base)]
    for label, pre in MIRRORS:
        if pre:
            out.append((label, pre + base))
    return out


print("=" * 72)
print("步骤 1：确定 SDL2 最新版本")
ver = None
try:
    rels = get_json("https://api.github.com/repos/libsdl-org/SDL/releases?per_page=100")
    v2 = []
    for r in rels:
        t = (r.get("tag_name") or "")
        if t.startswith("release-2."):
            v2.append(t.replace("release-", ""))
    print(f"  找到 SDL2 代数 release 共 {len(v2)} 个")
    # 版本号排序
    def key(s):
        parts = []
        for x in s.split("."):
            try:
                parts.append(int(x))
            except ValueError:
                parts.append(0)
        return parts
    v2 = sorted(set(v2), key=key, reverse=True)
    if v2:
        ver = v2[0]
    print(f"  最新 SDL2 = {ver}   近期若干: {v2[:6]}")
except Exception as e:  # noqa: BLE001
    print(f"  API 查询失败: {type(e).__name__}: {e}")

CANDIDATES = [ver] if ver else []
CANDIDATES += ["2.32.10", "2.32.8", "2.30.9", "2.28.5"]
CANDIDATES = [v for i, v in enumerate(CANDIDATES) if v and v not in CANDIDATES[:i]]

print()
print("=" * 72)
print("步骤 2：下载 SDL2 开发库（Windows/VC）")
dev_zip = None
used_ver = None
for v in CANDIDATES:
    fn = f"SDL2-devel-{v}-VC.zip"
    cands = mirror_urls(f"libsdl-org/SDL/releases/download/release-{v}/{fn}")
    # 追加 libsdl.org 官方 release 目录
    cands.append(("libsdl.org", f"https://www.libsdl.org/release/{fn}"))
    print(f"- 版本 {v}")
    got = try_download(fn, cands, min_size=1_000_000)
    if got:
        dev_zip, used_ver = got, v
        break

print()
print("=" * 72)
print("步骤 3：下载 SDL2 源码包（Android 端构建需要）")
src_tar = None
if used_ver:
    for ext, opn in [(".tar.gz", "tar.gz"), (".zip", "zip")]:
        fn = f"SDL2-{used_ver}{ext}"
        cands = mirror_urls(f"libsdl-org/SDL/releases/download/release-{used_ver}/{fn}")
        cands.append(("libsdl.org", f"https://www.libsdl.org/release/{fn}"))
        got = try_download(fn, cands, min_size=1_000_000)
        if got:
            src_tar = got
            break

print()
print("=" * 72)
print("步骤 4：校验内容")
for p, kind in [(dev_zip, "zip"), (src_tar, "src")]:
    if not p:
        print(f"  [{kind}] 未获取")
        continue
    print(f"  [{kind}] {os.path.basename(p)}  {os.path.getsize(p):,} 字节")
    try:
        if p.endswith(".zip"):
            with zipfile.ZipFile(p) as z:
                names = z.namelist()
                tops = sorted({n.split('/')[0] for n in names})
                print(f"      条目 {len(names)}，顶层: {tops[:6]}")
                inc = [n for n in names if n.endswith("SDL.h")]
                libs = [n for n in names if n.endswith(".lib") and "x64" in n]
                print(f"      SDL.h: {inc[:3]}")
                print(f"      x64 lib: {libs[:5]}")
        else:
            with tarfile.open(p) as t:
                names = t.getnames()
                tops = sorted({n.split('/')[0] for n in names})
                print(f"      条目 {len(names)}，顶层: {tops[:6]}")
    except Exception as e:  # noqa: BLE001
        print(f"      校验失败: {type(e).__name__}: {e}")

print()
print("=" * 72)
print(f"总结：SDL2 开发库 {'✅ ' + os.path.basename(dev_zip) if dev_zip else '❌ 未获取'}"
      f" | 源码包 {'✅ ' + os.path.basename(src_tar) if src_tar else '❌ 未获取'}")
print(f"版本: {used_ver}")
