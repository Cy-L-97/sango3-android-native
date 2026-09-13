"""M0 素材管线验证：批量导出 SHP -> PNG 并生成 HTML 图库"""
import os, sys, re, html
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pak import read_index
from shp import decode_rgb565, write_png, parse_header

GAME = r"E:\Program Files (x86)\steam\steamapps\common\Sango3"
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
GAL = os.path.join(ROOT, ".workbuddy", "gallery")
PNG = os.path.join(GAL, "png")

CATS = [
    ("武将头像 Portrait", "shape\\portrait\\", 475, "原版武将立绘，100x120 RGB565"),
    ("兵种/战场 BF", "shape\\bf\\", 60, "战场单位精灵"),
    ("界面素材 MM", "shape\\mm\\", 60, "界面底图与控件"),
    ("武将技演出 Magic", "shape\\magic\\", 40, "武将技特效帧"),
    ("人物 AD 动作", "shape\\ad\\", 40, "人物动作精灵"),
]

def main():
    entries, _, _, _ = read_index(os.path.join(GAME, "Sango3.PAK"))
    os.makedirs(PNG, exist_ok=True)
    idx = [(n.lower(), n, o, s) for n, o, s in entries]

    report = []
    sections = []
    with open(os.path.join(GAME, "Sango3.PAK"), "rb") as f:
        for title, prefix, limit, note in CATS:
            picked = []
            for low, name, off, size in idx:
                if low.startswith(prefix):
                    picked.append((name, off, size))
                    if len(picked) >= limit:
                        break
            f.seek(0)
            cards = []
            dims = {}
            ok = 0
            for name, off, size in picked:
                f.seek(off)
                data = f.read(size)
                rel = name.replace("\\", "_").replace("/", "_")
                stem = os.path.splitext(rel)[0]
                dst = os.path.join(PNG, stem + ".png")
                try:
                    rgb, w, h, hdr = decode_rgb565(data, None, None)
                    write_png(dst, w, h, rgb)
                    ok += 1
                    dims[(w, h)] = dims.get((w, h), 0) + 1
                    cards.append((stem + ".png", name, w, h))
                except Exception as e:
                    pass
            sections.append((title, note, cards, dims, ok, len(picked)))
            report.append(f"{title}: 命中 {len(picked)} / 成功解码 {ok}")

    for r in report:
        print("  " + r)

    # 生成 HTML 图库
    parts = ["""<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<title>三国群英传3 · M0 素材管线验证</title>
<style>
:root{--bg:#f6f7f9;--card:#fff;--line:#e3e6ea;--tx:#1f2328;--tx2:#5b6470;--acc:#b5451b;}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);
 font-family:-apple-system,"Segoe UI","Microsoft YaHei",sans-serif;}
header{padding:28px 32px 18px;border-bottom:1px solid var(--line);background:#fff;}
h1{margin:0 0 6px;font-size:22px;letter-spacing:.5px;}
.sub{color:var(--tx2);font-size:13px;line-height:1.7;}
.badge{display:inline-block;background:#eef7ee;color:#256b25;border:1px solid #cbe6cb;
 border-radius:999px;padding:2px 10px;font-size:12px;margin-right:8px;}
section{padding:22px 32px 8px;}
h2{font-size:16px;margin:0 0 4px;display:flex;align-items:center;gap:10px;}
h2 .n{background:var(--acc);color:#fff;border-radius:6px;padding:1px 8px;font-size:12px;}
.note{color:var(--tx2);font-size:12.5px;margin:2px 0 14px;}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(104px,1fr));gap:10px;}
.card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:6px;
 text-align:center;transition:.15s;}
.card:hover{box-shadow:0 4px 14px rgba(0,0,0,.08);transform:translateY(-2px);}
.card img{width:100%;height:auto;image-rendering:pixelated;border-radius:4px;
 background:linear-gradient(45deg,#f0f0f0 25%,transparent 25%,transparent 75%,#f0f0f0 75%),
            linear-gradient(45deg,#f0f0f0 25%,#fff 25%,#fff 75%,#f0f0f0 75%);
 background-size:12px 12px;background-position:0 0,6px 6px;}
.card .fn{font-size:10px;color:var(--tx2);margin-top:4px;word-break:break-all;line-height:1.3;}
footer{padding:20px 32px 40px;color:var(--tx2);font-size:12px;line-height:1.8;}
code{background:#eef0f3;padding:1px 6px;border-radius:4px;font-size:12px;}
</style></head><body>
<header>
<h1>三国群英传3 · M0 素材管线验证</h1>
<div class="sub">
<span class="badge">PAK/PAKSW 已破解</span>
<span class="badge">TLHS SHP 已破解</span>
<span class="badge">RGB565 直色</span>
<span class="badge">Big5 数据表</span><br>
本页所有图片均由自研解码器从 <code>Sango3.PAK</code> 实时解析生成，未使用任何第三方工具。这是原生引擎「素材管线」跑通的可视化证据。
</div></header>
"""]
    total = 0
    for title, note, cards, dims, ok, hit in sections:
        if not cards:
            continue
        total += len(cards)
        dstr = ", ".join(f"{w}×{h}" for (w, h), _ in sorted(dims.items(), key=lambda x: -x[1])[:4])
        parts.append(f'<section><h2>{html.escape(title)}<span class="n">{len(cards)} 张</span></h2>')
        parts.append(f'<div class="note">{html.escape(note)} · 尺寸分布 {dstr}</div><div class="grid">')
        for fn, name, w, h in cards:
            parts.append(f'<div class="card"><img loading="lazy" src=".workbuddy/gallery/png/{html.escape(fn)}" '
                         f'alt="{html.escape(name)}"><div class="fn">{html.escape(os.path.basename(name))}</div></div>')
        parts.append('</div></section>')
    parts.append(f"""<footer>
共渲染 <b>{total}</b> 张素材。<br>
解码链路：<code>Sango3.PAK</code> → PAKSW 索引 → 条目定位 → TLHS 帧表 → RGB565 → PNG。<br>
下一步：批量导出全量 24,477 条素材并接入 AI 超分管线（第 5 项需求）。
</footer></body></html>""")

    out = os.path.join(ROOT, "三国群英传3_M0素材管线验证.html")
    with open(out, "w", encoding="utf-8") as f:
        f.write("".join(parts))
    print(f"\n  图库已生成: {out}  (共 {total} 张)")

if __name__ == "__main__":
    main()
