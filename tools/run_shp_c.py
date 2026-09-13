# -*- coding: utf-8 -*-
"""run_shp_c.py —— 对所有样本 SHP 运行 C 解码工具，汇总输出供 verify_shp_c.py 比对。"""
import os
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAMPLE = os.path.join(ROOT, ".workbuddy", "sample")
EXE = os.path.join(ROOT, "build", "pc", "bin", "sango3shp.exe")
OUT = os.path.join(ROOT, ".workbuddy", "probe_shp_c.txt")

if not os.path.isfile(EXE):
    print(f"缺少可执行文件：{EXE}")
    sys.exit(2)

targets = []
for r, _d, fs in os.walk(SAMPLE):
    for f in sorted(fs):
        if f.lower().endswith(".shp"):
            targets.append(os.path.join(r, f))
targets.sort()

print(f"C 工具: {EXE}")
print(f"样本数: {len(targets)}")

lines = []
for p in targets:
    # 路径由 Python 侧书写（C 工具只输出 ASCII 数据），避免 GBK/UTF-8 混淆
    lines.append(f"##### {p} #####")
    try:
        r = subprocess.run([EXE, p], capture_output=True, text=True,
                           encoding="ascii", errors="replace", timeout=300)
        out = (r.stdout or "") + (r.stderr or "")
        lines.append(out.rstrip())
    except Exception as e:  # noqa: BLE001
        lines.append(f"FAIL : <runner error {type(e).__name__}>")

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")

print(f"已写入: {OUT}")
print("-" * 66)
for l in lines:
    for sub in l.splitlines():
        print("  " + sub)
    print()