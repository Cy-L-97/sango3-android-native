# -*- coding: utf-8 -*-
"""替换候选：下载覆盖更全的像素字体并量化覆盖率。

背景：ArkPixel12 对游戏 2000 个汉字缺 158 个（92.1%），缺的还都是高频字
（孫/殺/擊/旋/拖/換/應/懷），无法用于本项目。

候选：
  A. Fusion Pixel Font 12px（缝合像素字体）—— 设计目标即"融合多套像素字体以补全覆盖"
  B. Ark Pixel Font 16px —— 更大字号档位，覆盖可能更广
"""
import json
import os
import re
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
SETTING = os.path.join(ROOT, ".workbuddy", "data", "Setting")
JSOND = os.path.join(ROOT, ".workbuddy", "data", "json")
os.makedirs(DL, exist_ok=True)
CJK = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")


def http_get(url, timeout=180):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=timeout, context=ctx) as r:
        return r.read()


def download(url, dest):
    for pre in ("", "https://gh-proxy.com/"):
        u = pre + url if pre else url
        try:
            data = http_get(u)
            with open(dest, "wb") as f:
                f.write(data)
            print(f"    ✓ {len(data):,} 字节（{u.split('/')[2]}）")
            return True
        except Exception as e:  # noqa: BLE001
            print(f"    ✗ {type(e).__name__}: {str(e)[:60]}")
    return False


# ---------------- 字符集 ----------------
def read_text(path):
    with open(path, "rb") as f:
        raw = f.read()
    for enc in ("big5", "big5hkscs", "gbk", "utf-8"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("big5", errors="replace")


chars = set()
for fn in os.listdir(SETTING):
    if fn.lower().endswith(".ini"):
        chars |= set(CJK.findall(read_text(os.path.join(SETTING, fn))))
for fn in os.listdir(JSOND):
    if fn.endswith(".json") and "targets" not in fn:
        chars |= set(CJK.findall(open(os.path.join(JSOND, fn), encoding="utf-8").read()))
print(f"游戏用字：{len(chars)} 个不同汉字\n")

# ---------------- 下载候选 ----------------
TARGETS = [
    ("FusionPixel12", "TakWolf/fusion-pixel-font", "12px-proportional-ttf"),
    ("ArkPixel16", "TakWolf/ark-pixel-font", "16px-proportional-ttf"),
]

zips = {}
for label, repo, pat in TARGETS:
    print(f"[{label}] 查询 {repo}")
    try:
        d = json.loads(http_get(f"https://api.github.com/repos/{repo}/releases/latest"))
        cands = [a for a in d["assets"] if pat in a["name"] and "woff" not in a["name"]]
        if not cands:
            print("    未找到资产")
            continue
        a = cands[0]
        print(f"    {a['size']/1048576:.2f} MB  {a['name']}")
        dst = os.path.join(DL, a["name"])
        if os.path.isfile(dst) and os.path.getsize(dst) == a["size"]:
            print("    已存在，跳过下载")
        elif not download(a["browser_download_url"], dst):
            continue
        outdir = os.path.join(DL, label)
        if not os.path.isdir(outdir):
            with zipfile.ZipFile(dst) as z:
                z.extractall(outdir)
        names = sorted(os.listdir(outdir))
        print(f"    解压后 {len(names)} 个文件：")
        for n in names:
            p = os.path.join(outdir, n)
            if os.path.isfile(p):
                print(f"      {os.path.getsize(p):>11,}  {n}")
        zips[label] = outdir
    except Exception as e:  # noqa: BLE001
        print(f"    ✗ {type(e).__name__}: {str(e)[:70]}")

# ---------------- 覆盖分析 ----------------
print()
print("=" * 76)
print("覆盖率对比")
print("=" * 76)
try:
    from fontTools.ttLib import TTFont
except ImportError:
    print("缺 fonttools")
    raise SystemExit(1)


def cmap_of(path):
    f = TTFont(path, fontNumber=0, lazy=True)
    cm = set()
    for t in f["cmap"].tables:
        cm |= set(t.cmap.keys())
    f.close()
    return cm


def analyze(label, path):
    cm = cmap_of(path)
    miss = sorted(c for c in chars if ord(c) not in cm)
    print(f"\n  {label}  ({os.path.basename(path)}, {os.path.getsize(path):,} 字节)")
    print(f"    内码位 {len(cm):,}   缺失 {len(miss)}   覆盖率 {(len(chars)-len(miss))/len(chars)*100:.2f}%")
    if miss:
        print(f"    缺失（前 70）：{''.join(miss[:70])}")
    return miss


report = {"game_chars": len(chars)}
for label, outdir in zips.items():
    for n in sorted(os.listdir(outdir)):
        if not n.endswith(".ttf"):
            continue
        # 只测繁体与简中变体（游戏是 Big5 繁体）
        if not any(k in n for k in ("zh_tw", "zh_hk", "zh_hant", "zh_cn", "zh_hans", "latin")):
            continue
        miss = analyze(f"{label}/{n}", os.path.join(outdir, n))
        report[f"{label}/{n}"] = {"missing": len(miss), "chars": "".join(miss[:300])}

json.dump(report, open(os.path.join(ROOT, ".workbuddy", "data", "font_candidates.json"),
                       "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print()
print("已写出 .workbuddy/data/font_candidates.json")
