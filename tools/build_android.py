#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Android 构建（M4）：NDK 交叉编译 + 手工打包 APK（不依赖 gradle）。

设计取舍
--------
· 手工打包而非 gradle：gradle 需联网拉 AGP / gradle 发行包，链路长且易受网络影响；
  本流程只用 dl.google.com 的官方组件（NDK / build-tools / platform）+ JDK，可控性好。
· ASCII 工作区：项目路径含中文（E:\\用户\\...），NDK/clang/ninja 对非 ASCII 路径支持不佳，
  故先把 engine 源码与 android 工程同步到纯 ASCII 工作区（E:/sango3-android）再构建。

一次性依赖
----------
  E:/android-ndk/android-ndk-r27c
  E:/android-sdk/{cmdline-tools,platform-tools,build-tools,platforms}
  E:/android-sdk/jdk17/jdk-*(JDK 17)

用法
----
  python tools/build_android.py            # 构建 APK
  python tools/build_android.py --install  # 构建并 adb 安装到模拟器
  python tools/build_android.py --install --push-assets  # 装 APK 并推编码表/字体
  python tools/build_android.py --push-assets            # 只推资源（不重建）
  python tools/build_android.py --clean    # 清理
"""
import os
import sys
import glob
import shutil
import subprocess
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = "E:/sango3-android"                 # 纯 ASCII 工作区
NDK  = "E:/android-ndk/android-ndk-r27c"
SDK  = "E:/android-sdk"
LDP_ADB = "F:/leidian/LDPlayer9/adb.exe"
PKG = "org.libsdl.app"


def first_dir(pat):
    hits = sorted(glob.glob(pat))
    return hits[-1] if hits else None


JDK   = first_dir(os.path.join(SDK, "jdk17", "jdk-*"))
BT    = first_dir(os.path.join(SDK, "build-tools", "*"))
PLAT  = first_dir(os.path.join(SDK, "platforms", "android-*"))
CMAKE = first_dir(os.path.expanduser("~/.workbuddy/binaries/cmake/*/bin/cmake.exe"))
NINJA = "C:/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"


# 子进程环境：d8 / sdkmanager 需要 Java 11+，必须显式指向 JDK 17（系统默认是 JDK 8）
ENV = None


def make_env():
    env = dict(os.environ)
    env["JAVA_HOME"] = JDK
    env["ANDROID_SDK_ROOT"] = SDK
    env["ANDROID_HOME"] = SDK
    env["ANDROID_NDK_HOME"] = NDK
    env["PATH"] = os.path.join(JDK, "bin") + os.pathsep + env.get("PATH", "")
    return env


def run(cmd, env=None, cwd=None, check=True, quiet=False):
    env = env or ENV
    # encoding/errors 必须显式指定：javac/aapt2 在中文 Windows 上会输出 GBK，按 UTF-8 解码会抛异常
    kw = dict(env=env, cwd=cwd, capture_output=True, text=True,
              encoding="utf-8", errors="replace")
    if isinstance(cmd, str):
        print(">>>", cmd[:200])
        r = subprocess.run(cmd, shell=True, **kw)
    else:
        print(">>>", " ".join(cmd)[:200])
        r = subprocess.run(cmd, **kw)
    out = r.stdout or ""
    err = r.stderr or ""
    if not quiet and out.strip():
        print(out[-3000:])
    if r.returncode != 0:
        if err.strip():
            print("STDERR:", err[-3000:])
        if check:
            raise SystemExit("FAILED rc=%d" % r.returncode)
    return r


def check_env():
    miss = []
    for name, val in [("NDK", NDK if os.path.isdir(NDK) else None),
                      ("JDK", JDK), ("build-tools", BT),
                      ("platform", PLAT), ("cmake", CMAKE), ("ninja", NINJA)]:
        if not val or not os.path.exists(val):
            miss.append(name)
    if miss:
        print("缺少组件:", ", ".join(miss))
        return False
    print("NDK :", NDK)
    print("JDK :", JDK)
    print("BT  :", BT)
    print("PLAT:", PLAT)
    return True


def sync_sources():
    """把 engine 源码 / android 工程 / SDL2 源码同步到 ASCII 工作区。"""
    os.makedirs(WORK, exist_ok=True)
    # 第三项 always=False：SDL2 源码体积大，只在首次复制
    pairs = [
        (os.path.join(ROOT, "engine", "src"), os.path.join(WORK, "engine", "src"), True),
        (os.path.join(ROOT, "android"),       os.path.join(WORK, "android"),       True),
        (os.path.join(ROOT, "third_party", "SDL2-src"),
         os.path.join(WORK, "third_party", "SDL2-src"), False),
    ]
    # FreeType 源码（目录带版本号，glob 取版本号最大的）—— 字体后端，只在首次复制
    ft_dirs = sorted(glob.glob(os.path.join(ROOT, "third_party", "freetype-*")))
    if ft_dirs:
        pairs.append((ft_dirs[-1],
                      os.path.join(WORK, "third_party", os.path.basename(ft_dirs[-1])),
                      False))
    for src, dst, always in pairs:
        if not os.path.isdir(src):
            continue
        if not always and os.path.isdir(dst):
            print("kept  :", os.path.relpath(dst, WORK))
            continue
        # 用 dirs_exist_ok 增量覆盖：不删旧目录（批量删除会被安全策略拦截），也更快
        shutil.copytree(src, dst, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns("*.obj", "__pycache__", "build"))
        print("synced:", os.path.relpath(dst, WORK))


def build_native(abi="x86_64", api=24):
    ob = os.path.join(WORK, "build", "native")
    tc = os.path.join(NDK, "build", "cmake", "android.toolchain.cmake")
    env = dict(os.environ)
    env["ANDROID_NDK_HOME"] = NDK
    env["JAVA_HOME"] = JDK
    run([CMAKE, "-G", "Ninja",
         "-DCMAKE_MAKE_PROGRAM=" + NINJA,
         "-DCMAKE_TOOLCHAIN_FILE=" + tc,
         "-DANDROID_ABI=" + abi,
         "-DANDROID_PLATFORM=android-%d" % api,
         "-DANDROID_STL=c++_static",
         "-DCMAKE_BUILD_TYPE=Release",
         "-S", os.path.join(WORK, "android", "app", "jni"),
         "-B", ob], env=env)
    run([NINJA, "-C", ob], env=env)
    sos = glob.glob(os.path.join(ob, "**", "libmain.so"), recursive=True)
    print("libmain.so:", sos)
    return sos[0] if sos else None


def build_apk(lib_so, abi="x86_64", api=24):
    tmpl = os.path.join(WORK, "third_party", "SDL2-src", "android-project", "app", "src", "main")
    resdir = os.path.join(tmpl, "res")
    javadir = os.path.join(tmpl, "java")
    android_jar = os.path.join(PLAT, "android.jar")
    out = os.path.join(WORK, "build", "apk")
    os.makedirs(out, exist_ok=True)

    # aapt2 手工流程要求 manifest 带 package 属性（SDL2 模板把它留给 gradle 的 namespace）
    src_manifest = os.path.join(tmpl, "AndroidManifest.xml")
    manifest = os.path.join(out, "AndroidManifest.xml")
    with open(src_manifest, encoding="utf-8") as f:
        mtxt = f.read()
    if "package=" not in mtxt.split(">", 1)[0]:
        mtxt = mtxt.replace("<manifest", '<manifest package="%s"' % PKG, 1)
    with open(manifest, "w", encoding="utf-8") as f:
        f.write(mtxt)
    res_zip = os.path.join(out, "res.zip")
    base_apk = os.path.join(out, "base.apk")
    gen = os.path.join(out, "gen")
    classes = os.path.join(out, "classes")
    dexdir = os.path.join(out, "dex")
    for d in (gen, classes, dexdir):
        os.makedirs(d, exist_ok=True)

    aapt2 = os.path.join(BT, "aapt2.exe")
    # 1) 编资源
    run([aapt2, "compile", "--dir", resdir, "-o", res_zip])
    # 2) 链接 → base.apk + R.java
    run([aapt2, "link", "-o", base_apk, "-I", android_jar,
         "--manifest", manifest,
         "-R", res_zip,
         "--java", gen,
         "--min-sdk-version", str(api),
         "--target-sdk-version", "34",
         "--auto-add-overlay"])
    # 3) javac
    srcs = []
    for base in (javadir, gen):
        for r, _, fs in os.walk(base):
            for f in fs:
                if f.endswith(".java"):
                    srcs.append(os.path.join(r, f))
    javac = os.path.join(JDK, "bin", "javac.exe")
    with open(os.path.join(out, "srcs.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(srcs))
    # -encoding utf-8 必须显式指定：SDLActivity.java 含 emoji，javac 默认按平台编码(GBK)读会报"不可映射字符"
    run([javac, "-nowarn", "-encoding", "utf-8", "-source", "8", "-target", "8",
         "-classpath", android_jar, "-d", classes, "@" + os.path.join(out, "srcs.txt")])
    # 4) d8 → dex
    classfiles = []
    for r, _, fs in os.walk(classes):
        for f in fs:
            if f.endswith(".class"):
                classfiles.append(os.path.join(r, f))
    d8 = os.path.join(BT, "d8.bat")
    run([d8, "--lib", android_jar, "--min-api", str(api),
         "--output", dexdir] + classfiles)
    # 5) 组装：base.apk + classes.dex + lib/<abi>/libmain.so
    apk = os.path.join(out, "sango3-unsigned.apk")
    shutil.copyfile(base_apk, apk)
    import zipfile
    with zipfile.ZipFile(apk, "a", zipfile.ZIP_DEFLATED) as z:
        z.write(os.path.join(dexdir, "classes.dex"), "classes.dex")
        # 所有 native 共享库都要打进 APK —— SDL2 是独立的 libSDL2.so，
        # Java 侧 SDLActivity 会 System.loadLibrary("SDL2")，缺了会在启动时报
        # couldn't find "libSDL2.so"。
        native = glob.glob(os.path.join(WORK, "build", "native", "**", "lib*.so"),
                           recursive=True)
        if lib_so and lib_so not in native:
            native.append(lib_so)
        for so in native:
            z.write(so, "lib/%s/%s" % (abi, os.path.basename(so)))
            print("  + lib/%s/%s" % (abi, os.path.basename(so)))
    print("assembled:", apk)
    return apk


def sign_apk(apk):
    ks = os.path.join(WORK, "build", "debug.keystore")
    if not os.path.exists(ks):
        keytool = os.path.join(JDK, "bin", "keytool.exe")
        run([keytool, "-genkeypair", "-keystore", ks, "-alias", "androiddebugkey",
             "-storepass", "android", "-keypass", "android",
             "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
             "-dname", "CN=Android Debug,O=Android,C=US"])
    out = os.path.join(WORK, "build", "apk", "sango3-aligned.apk")
    za = os.path.join(BT, "zipalign.exe")
    run([za, "-f", "-p", "4", apk, out])
    signed = os.path.join(WORK, "build", "apk", "sango3.apk")
    signer = os.path.join(BT, "apksigner.bat")
    run([signer, "sign", "--ks", ks, "--ks-pass", "pass:android",
         "--key-pass", "pass:android", "--out", signed, out])
    print("signed:", signed)
    return signed


def adb_path():
    return LDP_ADB if os.path.exists(LDP_ADB) else os.path.join(SDK, "platform-tools", "adb.exe")


def push_assets():
    """把编码表 / 字体推到设备的**应用外部私有目录**。

    公共 /sdcard 需运行时存储权限，未授权时进程会启动即退出（M4 踩过的坑），
    所以资源一律放 /sdcard/Android/data/<pkg>/files/Sango3/。
    """
    adb = adb_path()
    dst = "/sdcard/Android/data/%s/files/Sango3" % PKG
    run([adb, "shell", "mkdir", "-p", dst + "/encoding", dst + "/fonts"])
    for sub in ("encoding", "fonts"):
        src = os.path.join(ROOT, "engine", "assets", sub)
        if not os.path.isdir(src):
            continue
        for name in sorted(os.listdir(src)):
            p = os.path.join(src, name)
            if not os.path.isfile(p):
                continue
            print("push: %s/%s (%.1f MB)" % (sub, name, os.path.getsize(p) / 1048576.0))
            run([adb, "push", p, "%s/%s/%s" % (dst, sub, name)])
    print("assets ->", dst)


def main():
    global ENV
    ENV = make_env()
    if "--clean" in sys.argv:
        shutil.rmtree(WORK, ignore_errors=True)
        print("cleaned", WORK)
        return 0
    if "--push-assets" in sys.argv and len(sys.argv) == 2:
        # 只推资源，不重建（PAK 已推过、字体换版本时用）
        push_assets()
        return 0
    if not check_env():
        return 1
    t0 = time.time()
    sync_sources()
    lib = build_native()
    apk = build_apk(lib)
    signed = sign_apk(apk)
    print("=== done in %.1fs ===" % (time.time() - t0))
    print("APK:", signed)
    if "--install" in sys.argv:
        # ⚠ 雷电模拟器上 `adb install -r` 会卡死（2026-09-15 实测，300s 无响应）；
        # 改用 push + pm install，秒级完成且效果相同。
        adb = adb_path()
        tmp_apk = "/data/local/tmp/sango3.apk"
        run([adb, "push", signed, tmp_apk])
        run([adb, "shell", "pm", "install", "-r", tmp_apk])
    if "--push-assets" in sys.argv:
        push_assets()
    return 0


if __name__ == "__main__":
    sys.exit(main())
