# -*- coding: utf-8 -*-
"""
msvc_env.py — 可复用的 MSVC x64 构建环境

背景：本机装了 VS Build Tools 2022（C:\\BuildTools），但其 vcvars64.bat
内部会调用 reg.exe 查询注册表，而 reg.exe 在当前沙箱中被安全策略拦截，
导致 vcvars 无法完成环境配置（cl.exe 在 PATH 里但 INCLUDE/LIB 为空）。

因此这里手工构造等价环境。任何需要 cl.exe/link.exe/rc.exe 的构建脚本
都应 `from msvc_env import make_env, run` 而不是自己去 call vcvars64.bat。

用途：
    from msvc_env import make_env, run
    env = make_env()
    rc, out = run("cl /nologo /Fe:app.exe main.c", env=env)
"""
import os
import subprocess

MSVC_VER = "14.44.35207"
BUILD_TOOLS = r"C:\BuildTools"
VC = rf"{BUILD_TOOLS}\VC\Tools\MSVC\{MSVC_VER}"
SDK_ROOT = r"C:\Program Files (x86)\Windows Kits\10"
SDK_VER = "10.0.26100.0"
SDK_INC = rf"{SDK_ROOT}\Include\{SDK_VER}"
SDK_LIB = rf"{SDK_ROOT}\Lib\{SDK_VER}"
SDK_BIN = rf"{SDK_ROOT}\bin\{SDK_VER}\x64"
MSBUILD = rf"{BUILD_TOOLS}\MSBuild\Current\Bin\MSBuild.exe"
NMAKE = rf"{VC}\bin\Hostx64\x64\nmake.exe"
CMAKE = r"%USERPROFILE%\.workbuddy\binaries\cmake\cmake-4.4.3-windows-x86_64\bin\cmake.exe"
GIT = r"%USERPROFILE%\.workbuddy\binaries\PortableGit\versions\1.2.0\cmd\git.exe"


def make_env(extra_path=None, extra=None):
    """返回配置好的 x64 构建环境变量字典（基于当前 os.environ 副本）。"""
    env = dict(os.environ)
    paths = [rf"{VC}\bin\Hostx64\x64", SDK_BIN]
    if extra_path:
        paths.extend(extra_path)
    paths.append(os.path.join(env.get("SystemRoot", r"C:\Windows"), "System32"))
    env["PATH"] = ";".join(paths) + ";" + env.get("PATH", "")
    env["INCLUDE"] = ";".join([
        rf"{VC}\include",
        rf"{SDK_INC}\ucrt",
        rf"{SDK_INC}\shared",
        rf"{SDK_INC}\um",
        rf"{SDK_INC}\winrt",
        rf"{SDK_INC}\cppwinrt",
    ])
    env["LIB"] = ";".join([
        rf"{VC}\lib\x64",
        rf"{SDK_LIB}\ucrt\x64",
        rf"{SDK_LIB}\um\x64",
    ])
    env["VSCMD_ARG_TGT_ARCH"] = "x64"
    env["Platform"] = "x64"
    if extra:
        env.update(extra)
    return env


def run(cmd, cwd=None, env=None, timeout=900, encoding="gbk"):
    """执行命令并返回 (returncode, 合并后的输出文本)。"""
    if env is None:
        env = make_env()
    try:
        p = subprocess.run(cmd, shell=True, cwd=cwd, env=env,
                           capture_output=True, text=True,
                           encoding=encoding, errors="replace", timeout=timeout)
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    except subprocess.TimeoutExpired:
        return -1, f"<TIMEOUT after {timeout}s>"
    except Exception as e:  # noqa: BLE001
        return -1, f"<EXC {type(e).__name__}: {e}>"


def check_components():
    """返回关键组件存在性字典，便于构建前自检。"""
    items = {
        "cl.exe": rf"{VC}\bin\Hostx64\x64\cl.exe",
        "link.exe": rf"{VC}\bin\Hostx64\x64\link.exe",
        "nmake.exe": NMAKE,
        "rc.exe": rf"{SDK_BIN}\rc.exe",
        "ucrt/stdio.h": rf"{SDK_INC}\ucrt\stdio.h",
        "kernel32.lib": rf"{SDK_LIB}\um\x64\kernel32.lib",
        "MSBuild.exe": MSBUILD,
        "cmake.exe": CMAKE,
    }
    return {k: os.path.isfile(v) for k, v in items.items()}
