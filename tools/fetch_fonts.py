# -*- coding: utf-8 -*-
"""获取可自由分发的开源中文字体，部署到 engine/assets/fonts/。

选定（均为 OFL-1.1，允许自由分发与商用嵌入）：
  1. Ark Pixel Font 12px（方舟像素字体）  —— 像素风，充当"原版像素"显示模式
  2. LXGW WenKai（霞鹜文楷）             —— 矢量楷体，充当"高清"显示模式（任意分辨率清晰）

策略：优先 github.com 直连；失败自动改走 gh-proxy.com 镜像。
"""
import json
import os
import ssl
import sys
import urllib.request
import zipfile

sys.stdout.reconfigure(encoding="utf-8")
ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE
UA = {"User-Agent": "Mozilla/5.0"}

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DL = os.path.join(ROOT, ".workbuddy", "downloads", "fonts")
OUT = os.path.join(ROOT, "engine", "assets", "fonts")
os.makedirs(DL, exist_ok=True)
os.makedirs(OUT, exist_ok=True)

MIRRORS = ["", "https://gh-proxy.com/"]


def http_get(url, timeout=90):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=timeout, context=ctx) as r:
        return r.read()


def download(url, dest):
    """依次尝试直连与镜像。返回实际使用的 URL。"""
    last = None
    for pre in MIRRORS:
        u = pre + url if pre else url
        try:
            print(f"    尝试 {u[:100]}")
            data = http_get(u)
            with open(dest, "wb") as f:
                f.write(data)
            print(f"    ✓ {len(data):,} 字节 -> {os.path.basename(dest)}")
            return u
        except Exception as e:  # noqa: BLE001
            last = f"{type(e).__name__}: {str(e)[:60]}"
            print(f"    ✗ {last}")
    raise RuntimeError(f"全部源失败: {url}  最后错误 {last}")


print("=" * 76)
print("1) 查询 Ark Pixel Font 12px 资产名")
print("=" * 76)
ark_zip_url = None
ark_name = None
try:
    d = json.loads(http_get("https://api.github.com/repos/TakWolf/ark-pixel-font/releases/latest"))
    tag = d["tag_name"]
    print(f"  最新 tag = {tag}")
    cands = [a for a in d["assets"]
             if "12px-proportional-ttf-v" in a["name"] and "woff" not in a["name"]]
    for a in cands:
        print(f"    候选 {a['size']/1048576:>7.2f} MB  {a['name']}")
    if cands:
        ark_zip_url = cands[0]["browser_download_url"]
        ark_name = cands[0]["name"]
    if not ark_zip_url:
        raise RuntimeError("未找到 12px-proportional-ttf 资产")
except Exception as e:  # noqa: BLE001
    print(f"  查询失败：{type(e).__name__}: {e}")

print()
print("=" * 76)
print("2) 下载字体包")
print("=" * 76)

LXGW_URL = ("https://github.com/lxgw/LxgwWenKai/releases/download/v1.522/"
            "LXGWWenKai-Regular.ttf")
LXGW_LICENSE = ("https://github.com/lxgw/LxgwWenKai/releases/download/v1.522/"
                "OFL.txt")
ARK_LICENSE = ("https://github.com/TakWolf/ark-pixel-font/raw/main/LICENSE-OFL")

report = {}

if ark_zip_url:
    dst = os.path.join(DL, ark_name)
    print(f"\n  [Ark Pixel 12px] {ark_name}")
    try:
        used = download(ark_zip_url, dst)
        report["ark_url"] = used
        report["ark_zip"] = dst
        with zipfile.ZipFile(dst) as z:
            names = z.namelist()
            print(f"    包内 {len(names)} 个文件：")
            for n in names[:20]:
                print(f"      {z.getinfo(n).file_size:>10,}  {n}")
            z.extractall(os.path.join(DL, "ark"))
        report["ark_ok"] = True
    except Exception as e:  # noqa: BLE001
        print(f"    ✗ 失败 {type(e).__name__}: {e}")
        report["ark_ok"] = False

print(f"\n  [LXGW WenKai] LXGWWenKai-Regular.ttf")
dst2 = os.path.join(DL, "LXGWWenKai-Regular.ttf")
try:
    used = download(LXGW_URL, dst2)
    report["lxgw_url"] = used
    report["lxgw_ttf"] = dst2
    report["lxgw_ok"] = True
except Exception as e:  # noqa: BLE001
    print(f"    ✗ 失败 {type(e).__name__}: {e}")
    report["lxgw_ok"] = False

print("\n  许可证文件：")
for label, url, fn in [("Ark Pixel OFL", ARK_LICENSE, "ArkPixel-OFL.txt"),
                       ("LXGW OFL", LXGW_LICENSE, "LXGWWenKai-OFL.txt")]:
    try:
        data = http_get(url, timeout=40)
        with open(os.path.join(OUT, fn), "wb") as f:
            f.write(data)
        print(f"    ✓ {label}  {len(data):,} 字节 -> {fn}")
        report[f"lic_{fn}"] = True
    except Exception as e:  # noqa: BLE001
        print(f"    ✗ {label}: {type(e).__name__}: {str(e)[:60]}")
        report[f"lic_{fn}"] = False

json.dump(report, open(os.path.join(DL, "font_fetch_report.json"), "w",
                       encoding="utf-8"), ensure_ascii=False, indent=1)
print()
print("=" * 76)
print("3) 下载目录")
print("=" * 76)
for r, d, fs in os.walk(DL):
    for f in sorted(fs):
        p = os.path.join(r, f)
        print(f"  {os.path.getsize(p):>12,}  {os.path.relpath(p, DL)}")
