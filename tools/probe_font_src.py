# -*- coding: utf-8 -*-
"""探测字体下载源连通性：为"内置可自由分发开源中文字体"选定可用源。

候选字体（均为 OFL-1.1 / 可自由分发）：
  - Ark Pixel Font（方舟像素字体）  12px  —— 像素风，与老游戏最匹配
  - Fusion Pixel Font（缝合像素字体）12px  —— 像素风，中日韩全覆盖
  - LXGW WenKai（霞鹜文楷）                —— 矢量，楷体观感
  - Sarasa Gothic（更纱黑体）              —— 矢量，等宽/黑体
"""
import json
import os
import ssl
import sys
import urllib.request

sys.stdout.reconfigure(encoding="utf-8")
ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE
UA = {"User-Agent": "Mozilla/5.0"}

GH = [
    ("github.com 直连", "https://github.com/"),
    ("api.github.com", "https://api.github.com/"),
    ("gh-proxy.com 镜像", "https://gh-proxy.com/"),
    ("ghproxy.net 镜像", "https://ghproxy.net/"),
    ("gitee.com", "https://gitee.com/"),
    ("mirror.ghproxy.com", "https://mirror.ghproxy.com/"),
    ("hub.gitmirror.com", "https://hub.gitmirror.com/"),
    ("cdn.jsdelivr.net", "https://cdn.jsdelivr.net/"),
    ("github.io", "https://takwolf.github.io/"),
]

print("=" * 76)
print("1) 站点连通性")
print("=" * 76)
ok = {}
for name, url in GH:
    try:
        req = urllib.request.Request(url, headers=UA)
        with urllib.request.urlopen(req, timeout=12, context=ctx) as r:
            ok[name] = r.status
            print(f"  ✓ {name:<24} {r.status}")
    except Exception as e:  # noqa: BLE001
        ok[name] = None
        print(f"  ✗ {name:<24} {type(e).__name__}: {str(e)[:60]}")

print()
print("=" * 76)
print("2) 查询各字体仓库的 release 资产（走可用的 API 通道）")
print("=" * 76)

REPOS = [
    ("ArkPixel", "TakWolf/ark-pixel-font"),
    ("FusionPixel", "TakWolf/fusion-pixel-font"),
    ("LXGWWenKai", "lxgw/LxgwWenKai"),
    ("Sarasa", "be5invis/Sarasa-Gothic"),
    ("NotoSansSC", "notofonts/noto-cjk"),
]

api = "https://api.github.com"
for label, repo in REPOS:
    try:
        req = urllib.request.Request(f"{api}/repos/{repo}/releases/latest", headers=UA)
        with urllib.request.urlopen(req, timeout=15, context=ctx) as r:
            d = json.load(r)
        tag = d.get("tag_name")
        print(f"\n  [{label}] {repo}  最新 = {tag}")
        assets = d.get("assets") or []
        for a in assets[:14]:
            mb = a["size"] / 1048576
            print(f"      {mb:>8.2f} MB  {a['name']}")
            print(f"                 {a['browser_download_url']}")
        if len(assets) > 14:
            print(f"      ... 其余 {len(assets)-14} 个资产略")
    except Exception as e:  # noqa: BLE001
        print(f"\n  [{label}] {repo}  ✗ {type(e).__name__}: {str(e)[:70]}")

print()
print("=" * 76)
print("3) 已存在的字体目录")
print("=" * 76)
for p in [r"E:\用户\workbuddy\移植PC计划\engine\assets\fonts",
          r"E:\用户\workbuddy\移植PC计划\third_party"]:
    print(f"  {p}  ->  {'存在' if os.path.isdir(p) else '不存在'}")
    if os.path.isdir(p):
        for r, d, fs in os.walk(p):
            for f in fs:
                print(f"      {os.path.getsize(os.path.join(r,f)):>10,}  {f}")
