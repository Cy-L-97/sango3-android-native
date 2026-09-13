# -*- coding: utf-8 -*-
"""探测：SSH 工具链 / 字体下载包内容 / 繁简转换能力。"""
import glob
import importlib.util
import os
import sys

sys.stdout.reconfigure(encoding="utf-8")

print("=" * 74)
print("1) SSH 工具（ssh-keygen / ssh / git 的 ssh）")
print("=" * 74)
CAND = [
    os.path.expanduser(r"~\.workbuddy\binaries\PortableGit\versions\1.2.0\usr\bin\ssh-keygen.exe"),
    os.path.expanduser(r"~\.workbuddy\binaries\PortableGit\versions\1.2.0\usr\bin\ssh.exe"),
    r"C:\Program Files\Git\usr\bin\ssh-keygen.exe",
    r"C:\Windows\System32\OpenSSH\ssh-keygen.exe",
    r"C:\Windows\System32\OpenSSH\ssh.exe",
]
found = {}
for p in CAND:
    if os.path.isfile(p):
        print(f"  ✓ {p}")
        found[os.path.basename(p)] = p
    else:
        print(f"  · 无 {p}")

# 通配兜底
for pat in [os.path.expanduser(r"~\.workbuddy\binaries\PortableGit\**\ssh-keygen.exe"),
            r"C:\Windows\System32\OpenSSH\ssh*"]:
    for p in glob.glob(pat, recursive=True):
        if p not in found.values():
            print(f"  (glob) {p}")

print()
print("=" * 74)
print("2) 已有 SSH 密钥？")
print("=" * 74)
ssh_dir = os.path.join(os.path.expanduser("~"), ".ssh")
if os.path.isdir(ssh_dir):
    for f in sorted(os.listdir(ssh_dir)):
        print(f"  {os.path.getsize(os.path.join(ssh_dir, f)):>9,}  {f}")
else:
    print("  （~/.ssh 不存在）")

print()
print("=" * 74)
print("3) 字体下载包内容（.workbuddy/downloads/fonts）")
print("=" * 74)
DF = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                  ".workbuddy", "downloads", "fonts")
if os.path.isdir(DF):
    for r, d, fs in os.walk(DF):
        for f in sorted(fs):
            p = os.path.join(r, f)
            print(f"  {os.path.getsize(p):>12,}  {os.path.relpath(p, DF)}")
else:
    print("  （不存在）")

print()
print("=" * 74)
print("4) 已部署字体（engine/assets/fonts）")
print("=" * 74)
EF = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                  "engine", "assets", "fonts")
if os.path.isdir(EF):
    for f in sorted(os.listdir(EF)):
        p = os.path.join(EF, f)
        if os.path.isfile(p):
            print(f"  {os.path.getsize(p):>12,}  {f}")
else:
    print("  （不存在）")

print()
print("=" * 74)
print("5) 繁→简 转换能力（opencc 可用性）")
print("=" * 74)
for mod in ["opencc", "fontTools", "PIL"]:
    spec = importlib.util.find_spec(mod)
    print(f"  {mod:<10} {'✓ 已安装' if spec else '✗ 未安装'}")
