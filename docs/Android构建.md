# Android 构建（M4）—— 不依赖 gradle 的手工打包流程

> 状态：**已跑通**（2026-09-14，主菜单已在雷电模拟器正常运行）。
> 定位：本文件是可复用操作手册；工具链一次性部署，之后 `python tools/build_android.py --install` 一条命令出包。

---

## 一、为什么手工打包（而不是 gradle）

gradle 需要联网拉 AGP / gradle 发行包 + 依赖解析，链路长、易受网络与 JDK 版本影响。
本流程只用 **dl.google.com 的官方组件**（NDK / build-tools / platform）+ JDK，可控性好：

```
NDK/CMake 交叉编译 → aapt2（资源）→ javac（Java）→ d8（dex）→ 组装 → zipalign → apksigner
```

代价是要自己处理 9 个坑（见第四节），好处是**任何一步失败都能单独定位与重试**。

---

## 二、工具链（一次性部署，均在 E 盘，不落 C 盘）

| 组件 | 位置 | 说明 |
|---|---|---|
| NDK | `E:/android-ndk/android-ndk-r27c` | r27c，745MB；解压即用 |
| JDK 17 | `E:/android-sdk/jdk17/jdk-17.0.20.1+1` | **必须 11+**：d8 / sdkmanager 需要；系统自带的是 JDK 8 |
| SDK cmdline-tools | `E:/android-sdk/cmdline-tools/latest` | sdkmanager |
| platform-tools | `E:/android-sdk/platform-tools` | adb 等 |
| build-tools | `E:/android-sdk/build-tools/34.0.0` | aapt2 / d8 / zipalign / apksigner |
| platform | `E:/android-sdk/platforms/android-34` | android.jar |
| CMake / Ninja | WorkBuddy 的 cmake + VS 自带 ninja | 见 `tools/build_android.py` 顶部常量 |

> NDK 与 build-tools 用**官方 zip 直解**比 sdkmanager 快得多（sdkmanager 在本机很慢且易中断）；
> platform 的直链命名不规则，可从 `repository2-3.xml` 里查（见第六节）。

---

## 三、项目侧结构

```
android/app/jni/CMakeLists.txt   # engine → libmain.so（SDL2 由源码一并编译）
tools/build_android.py           # 一键构建 + 可选安装
```

要点：
- 与 PC 端**共用同一套 `engine/src`**（一套代码两端编译）；差异只在编译宏与资源路径。
- `SANGO3_ANDROID` 宏让 `sango3app.c` 走 Android 分支（固定资源路径、不解析 argv）。
- **字体走 FreeType 直连**：定义 `SANGO3_HAVE_FREETYPE=1`，FreeType 源码（
  `third_party/freetype-*`，目录带版本号、CMake 里用 glob 取最新版本）由 `add_subdirectory` 一并编译。
  **不用 SDL2_ttf** —— 其 CMake 会拉 harfbuzz / plutosvg，NDK 交叉编译链路长。
  `font.c` 里 SDL2_ttf 与 FreeType 两个后端对外接口一致（PC 用前者，Android 用后者）。
  FreeType 的可选外部依赖全部关掉：`FT_DISABLE_{ZLIB,BZIP2,PNG,HARFBUZZ,BROTLI}=ON`。
- CMake 必须传 `-DCMAKE_MAKE_PROGRAM=<ninja>`，否则报 `unable to find a build program corresponding to "Ninja"`。

---

## 四、九个坑（按踩到的顺序，每条都是"现象 → 原因 → 解法"）

| # | 现象 | 原因 | 解法 |
|---|---|---|---|
| 1 | CMake 找不到 Ninja | PATH 里没有 ninja，toolchain 不会自动找 | 传 `-DCMAKE_MAKE_PROGRAM=<绝对路径>` |
| 2 | `Cannot find source file: E:/engine/src/pak.c` | 相对路径层级算错（`android/app/jni` 上溯到根是 **三级**） | `${CMAKE_CURRENT_SOURCE_DIR}/../../../engine` |
| 3 | 脚本报 `SAFE_DELETE_BULK_CONFIRM_REQUIRED` | Shutil `rmtree` 批量删文件被安全策略拦截 | 改 `copytree(..., dirs_exist_ok=True)` 增量覆盖；大目录（SDL2 源码）只复制一次 |
| 4 | `AndroidManifest.xml: must have a 'package' attribute` | SDL2 模板把 package 留给 gradle 的 namespace | 构建时注入 `package="org.libsdl.app"` |
| 5 | `android.jar: No such file or directory` | sdkmanager 装 platform 未完成（目录只有 `.installer`） | 从 `repository2-3.xml` 查真实 zip 名（如 `platform-34-ext7_r03.zip`）直下 |
| 6 | javac 报"编码 GBK 的不可映射字符" | `SDLActivity.java` 含 emoji，javac 默认按平台编码读 | `javac -encoding utf-8` |
| 7 | d8 报 `UnsupportedClassVersionError ... class file version 55.0` | d8 用的是系统 JDK 8（需 11+） | **子进程环境显式设 `JAVA_HOME`=JDK17**（并把其 bin 前置到 PATH） |
| 8 | 启动即报 `couldn't find "libSDL2.so"` | APK 只打了 `libmain.so`；Java 会 `System.loadLibrary("SDL2")` | 把 `build/native/**/lib*.so` **全部**打进 `lib/<abi>/` |
| 9 | 进程秒退（日志 `Finished main function`） | 资源放在公共 `/sdcard/`，Android 6+ 需**运行时存储权限**，读不到就 return | 改用**应用外部私有目录** `/sdcard/Android/data/<pkg>/files/`（无需权限，adb 可写） |

### 另外两个非报错型坑

- **通道错位（整屏偏色）**：SDL 的 **GLES 后端**在打包格式上通道顺序相对 SDL 定义是"反转"的。
  - 现象：`RGBA8888` → alpha 落到 R（**整屏偏红**）；`BGRA8888` → alpha 落到 B（**整屏偏蓝**）。
  - 解法：**Android 用 `SDL_PIXELFORMAT_ABGR8888`**（反转后正好得到内存序 R,G,B,A）；PC(D3D) 继续用 `RGBA8888`。
- **`__android_log_print` 未定义**：需链接 `log`（`target_link_libraries(... log)`）。

---

## 五、常用命令

```bash
# 构建 + 安装到模拟器（含启动与截图）
python tools/build_android.py            # 只构建
python tools/build_android.py --install  # 构建并 adb install
python tools/build_android.py --install --push-assets  # 装 APK + 推 encoding/fonts
python tools/build_android.py --push-assets            # 只推资源（不重建）
python tools/build_android.py --clean    # 清理 ASCII 工作区 E:/sango3-android

# 资源推送（PAK 是版权文件，不入库；用 adb push）
LD=F:/leidian/LDPlayer9/adb.exe
D=/sdcard/Android/data/org.libsdl.app/files/Sango3
$LD -s emulator-5554 shell mkdir -p $D/encoding
$LD -s emulator-5554 push "<游戏目录>/Sango3.PAK" $D/
$LD -s emulator-5554 push "<游戏目录>/Update.PAK" $D/
$LD -s emulator-5554 push "engine/assets/encoding/." $D/encoding/

# 启动 / 截图 / 日志
$LD -s emulator-5554 shell am start -n org.libsdl.app/org.libsdl.app.SDLActivity
$LD -s emulator-5554 exec-out screencap -p > shot.png
$LD -s emulator-5554 logcat -d | grep -E "sango3|SDL|FATAL"
```

> 本机 adb 会同时看到 `emulator-5554` 与 `127.0.0.1:5555`（同一台雷电的两种连接），
> 多设备时报 `more than one device/emulator` → **先 `adb disconnect 127.0.0.1:5555`** 或用 `-s` 指定。

---

## 六、查官方组件直链（sdkmanager 慢时的兜底）

```python
import urllib.request, re
data = urllib.request.urlopen(
    'https://dl.google.com/android/repository/repository2-3.xml', timeout=40
).read().decode('utf-8', 'replace')
i = data.find('path="platforms;android-34"')
print(re.search(r'<url>([^<]+)</url>', data[i:i+4000]).group(1))
```

---

## 七、已知限制 / 下一步（2026-09-16 更新）

- ~~**无字体**~~ **已解决（2026-09-15）**：`font.c` FreeType 直连后端（见第三节），
  安卓中文文本正常 —— 实测主菜单右侧 V2.2C 文本出字（`tmp/android_font_shot.png`）。
- ~~**EXTEND 宽高比未实现（当前 pillarbox）**~~ **已解决（2026-09-15）**：
  `SANGO3_ASPECT_EXTEND`（UI 类场景：按长边撑满 + 边缘条带镜像延展，不留黑边不变形）+
  `SANGO3_ASPECT_COVER`（内容类场景如地图：铺满裁切、顶对齐）；`sango3app --aspect` 可运行时切。
- **资源靠 adb push**：正式分发时应把 PAK 打进 APK assets 或做首次启动解包（版权与体积需权衡）。
- **竖屏锁**：manifest 里 `screenOrientation="landscape"`（SDL2 模板自带），实机为横屏显示。
- **实测窗口尺寸**：横屏 `2560×1392` / 竖屏 `1080×1848`（窗口**在状态栏之下**，故 y 有偏移）。
- ✅ **`input tap` 坐标映射已解出（2026-09-23，此前记的"~72px 偏差原因未查"作废）**：
  ⚠ **2026-09-23 起朝堂画布不再是 640 宽**（改成"与屏幕同比例"，见
  `docs/分辨率与高清化架构.md` 第五节的宽高比策略）—— 分三种情况：

  ```
  # ① 菜单/選時期（EXTEND，逻辑画布仍 640×480）
  physical_x = lx * (窗口宽/640)          # 2560/640 = 4.0
  physical_y = 49 + ly * (窗口高/480)     # 49 ≈ 状态栏；1392/480 = 2.9
  # ② 朝堂（画布宽 = 480×窗口宽/窗口高 ≈ 883；UI 层居中偏移 ui_x0 ≈ 121）
  physical_x = (ui_x0 + ux) * 2.899       # ux 是"UI 坐标"（= App 内部 hit test 用的坐标）
  physical_y = 49 + uy * 2.9
  # ③ 地图（原生分辨率视口 1024×556 + COVER）：physical = lx*2.5 , 49+ly*2.5 一带，
  #    实测按 1024→2560 折算（2.5 倍）
  ```
  实测标定（朝堂，2026-09-23）：UI(98,70) → 点 `(635,252)` 命中「軍政」；
  UI(195,128) → `(916,420)` 命中「整備」；UI(100,190) → `(641,600)` 命中名单首行。
  菜单场景标定：点 `(1280,655)` 命中「開始遊戲」、`(1280,499)` 命中「黃巾之亂」。


## 八、用 adb 自查界面（截图 + 点击驱动）—— 2026-09-23

> 用途：**不用等用户反馈**，自己把界面走到目标页并截图肉眼验收（本轮"整备页乱七八糟"就是这样定位的）。

```python
# 循环：tap → screencap → pull → 用 Read 看图 → 决策
ADB='F:/leidian/LDPlayer9/adb.exe'
subprocess.run([ADB,'shell','input','tap',str(px),str(py)])          # 坐标用上面的公式换算
subprocess.run([ADB,'shell','screencap','-p','/sdcard/s.png'])
subprocess.run([ADB,'pull','/sdcard/s.png', r'E://sango3-android//shots//s.png'])   # ⚠ 必须纯 ASCII 路径
```

**四条坑（都踩过）**：
1. **`adb pull` 的目标路径不能带中文**（`E://用户//...` 会报 `cannot create file/directory`）→
   统一拉到 `E:/sango3-android/shots/`（ASCII）再看。
2. **Git Bash 会把 `/sdcard/x.png` 当 Windows 路径转换**（报 `C:/...PortableGit/sdcard/x.png` 不存在）→
   所有 adb 调用**走 Python subprocess**，不要在 bash 里直接写设备路径。
3. **`input tap` 的 y 命中偏差来自窗口偏移**（见第七节公式），不是随机的；要点"某一行"时，
   按公式算出的坐标若偏一行，就按行距（列表行高 × 缩放）微调后再点。
4. **看不清"为什么某块没出来"时，先加诊断日志再截图**：本次给 `officer_ui` 加了
   `s3_oui_set_log()` 通道 → app 接到 `ALOG`，一条 `face: ... ok=0 err=-` 立刻暴露了
   `shp_decode` 判断写反（截图只能看到"肖像没显示"，日志才指出"取到了、没报错、状态是失败"）。

**走位参考（黄巾之乱 / 張角，本轮实测可用）**：
`開始遊戲(1280,655)` → `黃巾之亂(1280,499)` → `張角行(135,555)` → `決定(640,1301)` →
`軍政(391,251)` → `整備(782,422)` → `名单里張角(400,600)` → `武將技页签(732,789)`。
