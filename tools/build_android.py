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


def run(cmd, env=None, cwd=None, check=True, quiet=False):
    if isinstance(cmd, str):
        print(">>>", cmd[:200])
        r = subprocess.run(cmd, shell=True, env=env, cwd=cwd, capture_output=True, text=True)
    else:
        print(">>>", " ".join(cmd)[:200])
        r = subprocess.run(cmd, env=env, cwd=cwd, capture_output=True, text=True)
    if not quiet:
        if r.stdout.strip():
            print(r.stdout[-3000:])
    if r.returncode != 0:
        if r.stderr.strip():
            print("STDERR:", r.stderr[-2500:])
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
    pairs = [
        (os.path.join(ROOT, "engine", "src"), os.path.join(WORK, "engine", "src")),
        (os.path.join(ROOT, "android"),       os.path.join(WORK, "android")),
        (os.path.join(ROOT, "third_party", "SDL2-src"), os.path.join(WORK, "third_party", "SDL2-src")),
    ]
    for src, dst in pairs:
        if not os.path.isdir(src):
            continue
        if os.path.isdir(dst):
            shutil.rmtree(dst, ignore_errors=True)
        shutil.copytree(src, dst, ignore=shutil.ignore_patterns("*.obj", "__pycache__"))
        print("synced:", os.path.relpath(dst, WORK))


def build_native(abi="x86_64", api=24):
    ob = os.path.join(WORK, "build", "native")
    tc = os.path.join(NDK, "build", "cmake", "android.toolchain.cmake")
    env = dict(os.environ)
    env["ANDROID_NDK_HOME"] = NDK
    env["JAVA_HOME"] = JDK
    run([CMAKE, "-G", "Ninja",
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
    manifest = os.path.join(tmpl, "AndroidManifest.xml")
    resdir = os.path.join(tmpl, "res")
    javadir = os.path.join(tmpl, "java")
    android_jar = os.path.join(PLAT, "android.jar")
    out = os.path.join(WORK, "build", "apk")
    os.makedirs(out, exist_ok=True)
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
    run([javac, "-nowarn", "-source", "8", "-target", "8",
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
        if lib_so:
            z.write(lib_so, "lib/%s/libmain.so" % abi)
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


def main():
    if "--clean" in sys.argv:
        shutil.rmtree(WORK, ignore_errors=True)
        print("cleaned", WORK)
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
        adb = LDP_ADB if os.path.exists(LDP_ADB) else os.path.join(SDK, "platform-tools", "adb.exe")
        run("\"%s\" install -r \"%s\"" % (adb, signed))
    return 0


if __name__ == "__main__":
    sys.exit(main())
