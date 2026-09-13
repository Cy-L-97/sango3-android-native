"""A 档 M1 前置：本机工具链探测"""
import shutil, os, sys, glob
sys.stdout.reconfigure(encoding="utf-8")

print("=" * 70)
print("一、PATH 中的可执行文件")
print("=" * 70)
for exe in ["cl", "clang", "clang++", "clang-cl", "gcc", "g++", "cmake", "ninja", "make",
            "java", "javac", "gradle", "adb", "sdkmanager", "ndk-build", "dotnet",
            "msbuild", "node", "npm", "py", "python", "git", "ld", "ar", "nasm"]:
    p = shutil.which(exe)
    print(f"  {exe:<12} {'OK  ' + p if p else '--'}")

print()
print("=" * 70)
print("二、常见安装目录")
print("=" * 70)
cands = [
    r"C:\Program Files\Microsoft Visual Studio",
    r"C:\Program Files (x86)\Microsoft Visual Studio",
    r"C:\Program Files (x86)\Windows Kits",
    r"C:\Program Files\CMake",
    r"C:\Program Files\LLVM",
    r"C:\Program Files\Android",
    r"C:\Program Files\Java",
    r"C:\Program Files\Eclipse Adoptium",
    r"C:\Program Files\dotnet",
    os.path.expanduser(r"~\AppData\Local\Android\Sdk"),
    os.path.expanduser(r"~\AppData\Local\Programs"),
    r"D:\Android", r"E:\Android", r"D:\SDK", r"E:\SDK",
    r"C:\LDPlayer", r"C:\LDPlayer9", r"D:\LDPlayer", r"E:\LDPlayer",
    r"C:\Program Files\BlueStacks_nxt",
]
for c in cands:
    if os.path.exists(c):
        try:
            subs = os.listdir(c)[:6]
        except Exception:
            subs = []
        print(f"  存在  {c}   -> {subs}")
    else:
        print(f"  无    {c}")

print()
print("=" * 70)
print("三、搜索 IDE / 工具链（限盘根一层）")
print("=" * 70)
for root in ["C:\\", "D:\\", "E:\\"]:
    try:
        for name in os.listdir(root):
            low = name.lower()
            if any(k in low for k in ("visual studio", "vs2022", "vs2019", "android", "sdk",
                                      "llvm", "cmake", "msys", "mingw", "qt", "sdl", "ldplayer",
                                      "雷电", "unity", "godot", "jdk", "java")):
                print(f"  {os.path.join(root, name)}")
    except Exception as e:
        print(f"  {root} 无法列出: {e}")

print()
print("=" * 70)
print("四、VS 安装详情（vswhere）")
print("=" * 70)
vsw = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if os.path.exists(vsw):
    print(f"  vswhere 存在: {vsw}")
else:
    print("  vswhere 不存在 -> 未安装 Visual Studio")

print()
print("=" * 70)
print("五、Android SDK / NDK 探测")
print("=" * 70)
for sdk in [os.path.expanduser(r"~\AppData\Local\Android\Sdk"), r"D:\Android\Sdk", r"E:\Android\Sdk"]:
    if os.path.isdir(sdk):
        print(f"  SDK: {sdk}")
        for sub in ("platform-tools", "ndk", "build-tools", "platforms", "cmdline-tools"):
            p = os.path.join(sdk, sub)
            if os.path.isdir(p):
                print(f"     {sub}: {os.listdir(p)[:5]}")
            else:
                print(f"     {sub}: (无)")
