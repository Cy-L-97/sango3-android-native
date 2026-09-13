#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
敏感信息入库检查器

用途：推送/提交前挡住个人信息、凭据、密钥进入版本库。

用法：
    python tools/check_secrets.py              # 扫描当前被跟踪的文件
    python tools/check_secrets.py --staged     # 只扫描暂存区（pre-commit 钩子用）
    python tools/check_secrets.py --history    # 连 Git 全部历史对象一起扫（较慢）

退出码：0 = 通过；1 = 发现疑似敏感信息

本地词表（默认不入库，放在 .workbuddy/secret-denylist.txt）：
    每行一个词，# 开头为注释。个人姓名、公司名、内网域名等放在这里，
    不要写进本脚本——否则词表本身就成了泄露源。
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DENYLIST = os.path.join(ROOT, ".workbuddy", "secret-denylist.txt")

SKIP_EXT = {
    ".pak", ".pck", ".sav", ".shp", ".res", ".dat", ".blk",
    ".exe", ".dll", ".lib", ".obj", ".pdb", ".so", ".dylib",
    ".ttf", ".otf", ".woff", ".woff2", ".png", ".jpg", ".jpeg",
    ".gif", ".bmp", ".ico", ".webp", ".zip", ".7z", ".rar", ".gz",
    ".pyc", ".pyo", ".whl", ".mp4", ".mp3", ".wav", ".jks", ".keystore",
}
MAX_SIZE = 8 * 1024 * 1024

# 通用规则（不含任何个人标识）
RULES = [
    ("手机号",       re.compile(r"(?<!\d)1[3-9]\d{9}(?!\d)")),
    ("座机号",       re.compile(r"(?<!\d)0\d{2,3}[-\s]\d{7,8}(?!\d)")),
    ("身份证号",     re.compile(r"(?<![0-9A-Za-z])\d{17}[\dXx](?![0-9A-Za-z])")),
    ("银行卡号",     re.compile(r"(?<!\d)\d{16,19}(?!\d)")),
    ("私钥内容",     re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")),
    ("URL内嵌凭据",  re.compile(r"https?://[^/\s:]+:[^/\s@]+@\S+")),
    ("凭据赋值",     re.compile(
        r"""(?ix)\b(?:password|passwd|pwd|secret|api[_-]?key|access[_-]?key|auth[_-]?token)\b"""
        r"""\s*[:=]\s*["']?[^\s"']{6,}""")),
    ("平台令牌",     re.compile(
        r"gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{20,}"
        r"|AKIA[0-9A-Z]{16}|sk-[A-Za-z0-9]{24,}|AIza[0-9A-Za-z\-_]{35}"
        r"|xox[baprs]-[A-Za-z0-9\-]{10,}")),
    ("JWT",          re.compile(r"eyJ[A-Za-z0-9_\-]{10,}\.eyJ[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,}")),
    ("本机用户路径", re.compile(r"[A-Za-z]:[\\/]+Users[\\/]+(?!%USERPROFILE%)(?!\[)[^\\/\s\"'|;,)\]]+")),
]

# 明确无害的值，避免误报
BENIGN = re.compile(
    r"^(git@github\.com|git@gitlab\.com|git@bitbucket\.org"
    r"|[A-Za-z0-9._%+\-]+@users\.noreply\.github\.com"
    r"|[A-Za-z0-9._%+\-]+@(?:example\.(?:com|org)|localhost|test\.invalid))$"
)

# 高风险文件名：这类文件本身就不该入库（按文件名判定，而非正文里提到）
RISKY_FILENAME = re.compile(
    r"""(?ix)^(?:
        id_rsa|id_dsa|id_ecdsa|id_ed25519|id_rsa\.pub|id_ed25519\.pub
        |.*\.(?:pem|key|pfx|p12|ppk|jks|keystore|kdbx)
        |\.env(?:\..*)?|\.netrc|_netrc
        |.*(?:credential|password|passwd|secret|token).*
      )$"""
)


def find_git() -> str:
    g = shutil.which("git")
    if g:
        return g
    home = os.path.expanduser("~")
    for cand in (
        os.path.join(home, ".workbuddy", "binaries", "PortableGit", "versions",
                     "1.2.0", "cmd", "git.exe"),
    ):
        if os.path.exists(cand):
            return cand
    raise SystemExit("找不到 git 可执行文件")


GIT = find_git()


def git(*args: str) -> str:
    r = subprocess.run([GIT, *args], cwd=ROOT, capture_output=True)
    return r.stdout.decode("utf-8", "replace")


def load_denylist() -> list[str]:
    if not os.path.exists(DENYLIST):
        return []
    terms = []
    with open(DENYLIST, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip().lstrip("\ufeff")
            if line and not line.startswith("#"):
                terms.append(line)
    return terms


def decode(b: bytes) -> str:
    for enc in ("utf-8", "gbk", "utf-16"):
        try:
            return b.decode(enc)
        except Exception:
            pass
    return b.decode("utf-8", "replace")


def scan_text(text: str, terms: list[str]) -> list[tuple[str, str, int]]:
    """返回 (规则名, 命中值, 起始偏移)"""
    out = []
    for label, rx in RULES:
        for m in rx.finditer(text):
            val = m.group(0)
            if label == "本机用户路径" and BENIGN.match(val):
                continue
            out.append((label, val, m.start()))
    for t in terms:
        for m in re.finditer(re.escape(t), text):
            out.append(("本地词表", m.group(0), m.start()))
    return out


def count_lines(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def files_staged() -> list[str]:
    names = git("diff", "--cached", "--name-only", "--diff-filter=ACM").splitlines()
    return [n.strip() for n in names if n.strip()]


def files_tracked() -> list[str]:
    return [n.strip() for n in git("ls-files").splitlines() if n.strip()]


def collect_blobs_history() -> dict[str, bytes]:
    objects = git("rev-list", "--objects", "--all")
    sha_to_path: dict[str, str] = {}
    order: list[str] = []
    for line in objects.splitlines():
        line = line.strip()
        if not line:
            continue
        sha, _, path = line.partition(" ")
        if sha not in sha_to_path:
            sha_to_path[sha] = path
            order.append(sha)
    if not order:
        return {}
    proc = subprocess.Popen([GIT, "cat-file", "--batch"], cwd=ROOT,
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    raw, _ = proc.communicate(("\n".join(order) + "\n").encode())
    blobs: dict[str, bytes] = {}
    pos, n = 0, len(raw)
    while pos < n:
        nl = raw.find(b"\n", pos)
        if nl < 0:
            break
        header = raw[pos:nl].decode("utf-8", "replace").split(" ")
        pos = nl + 1
        if len(header) < 3:
            continue
        try:
            size = int(header[2])
        except ValueError:
            continue
        content = raw[pos:pos + size]
        pos += size + 1
        if header[1] != "blob":
            continue
        path = sha_to_path.get(header[0], "?")
        if os.path.splitext(path)[1].lower() in SKIP_EXT:
            continue
        blobs.setdefault(path, content)
    return blobs


def main() -> int:
    ap = argparse.ArgumentParser(description="敏感信息入库检查器")
    ap.add_argument("--staged", action="store_true", help="只扫描暂存区（pre-commit）")
    ap.add_argument("--history", action="store_true", help="同时扫描 Git 全部历史")
    args = ap.parse_args()

    if not git("rev-parse", "--git-dir").strip():
        print("错误：当前目录不是 Git 仓库。")
        return 2

    terms = load_denylist()
    findings: list[tuple[str, str, int, str]] = []

    targets = files_staged() if args.staged else files_tracked()
    self_name = os.path.basename(__file__)
    for rel in targets:
        base = os.path.basename(rel)
        if base == self_name:
            continue
        if RISKY_FILENAME.match(base):
            findings.append(("tracked", rel, 0, "[高风险文件名] %s" % base))
            continue
        p = os.path.join(ROOT, rel.replace("/", os.sep))
        if os.path.splitext(p)[1].lower() in SKIP_EXT:
            continue
        try:
            if args.staged:
                # 读暂存区版本，而非工作区（两者可能不同）
                r = subprocess.run([GIT, "show", ":" + rel], cwd=ROOT, capture_output=True)
                if r.returncode != 0:
                    continue
                text = decode(r.stdout)
            else:
                if not os.path.exists(p) or os.path.getsize(p) > MAX_SIZE:
                    continue
                with open(p, "rb") as f:
                    text = decode(f.read())
        except OSError:
            continue
        for label, val, start in scan_text(text, terms):
            findings.append(("tracked", rel, count_lines(text, start), "[%s] %s" % (label, val)))

    if args.history:
        for path, blob in collect_blobs_history().items():
            if os.path.basename(path) == os.path.basename(__file__):
                continue
            text = decode(blob)
            for label, val, _start in scan_text(text, terms):
                findings.append(("history", path, 0, "[%s] %s" % (label, val)))

    if not terms:
        print("提示：未配置本地词表，个人姓名等专有词不会被检测。")
        print("      建议创建 %s（每行一个词，该文件已被 .gitignore 忽略）。" % DENYLIST)
        print()

    if findings:
        print("发现 %d 处疑似敏感信息：" % len(findings))
        for scope, path, line, detail in findings[:80]:
            loc = "%s:%s" % (path, line) if line else path
            print("  (%s) %s  %s" % (scope, loc, detail[:130]))
        if len(findings) > 80:
            print("  ...（其余 %d 处省略）" % (len(findings) - 80))
        print()
        print("处理建议：删除/改写该内容；若已提交，用 git filter-repo --replace-text 改写历史。")
        return 1

    print("通过：未发现敏感信息。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
