# Sango3 Android Native Port — Long-term Notes

## 新会话 / 换机续接（先读）
1. 本文件（MEMORY.md，长期账本）→ 2. 最近一篇日志 `memory/YYYY-MM-DD.md` → 3. **`docs/项目约定.md`（规则与口径，用户要求"必须遵守"）**。
   再按任务查：`docs/分辨率与高清化架构.md` / `docs/必杀技学习规则.md` / `docs/UI布局与Menu.ini.md` / `docs/战略地图格式.md` / `docs/Android构建.md`。
   上下文丢失不影响续接；**事实以文档为准**，不依赖聊天记录。

## 路线
A→B 递进：A 档（2~4 周 demo 级原生原型）→ B 档（4~6 月忠实还原）。
**当前进度**（2026-09-15，HEAD `3bb76f4`）：**M2 收口 + M4 跑通（含字体/EXTEND）+ M3-lite 开局流程 + 战略层地图**
—— 已可从主菜单一路点到战略地图：選擇時期 → 選擇君主 → 開局 → 地图（70 城，可拖动）。
仓库历史已净化（38 提交 → 现 45 提交，`main`）—— 误入库的两张版权截图已从历史彻底移除。
**下一步（明日起点）**：城池信息面板（内政/军事入口）· 势力着色（`Nation.ini` 旗号）·
城名文字层（`CitiesName.shp`）· blk 逻辑层（通行判定）。A 档 M5（高清包）仍放最后。

## A 档里程碑
- **M0**（已完）格式破解 + 分辨率无关渲染骨架
- **M1-a**（已完）数据层 + 规则引擎
  - 421 武将 / 103 物品 / 9 兵种 / 125 武将技，**9024 项字段级 C 与 Python 完全一致**
  - 见 `tools/verify_data_c.py`（任何改动后必跑）
- **M1-b**（已完）GPU 化渲染 + SDL_ttf 集成显示层
  - **GPU 路径 A 已完成**：`engine/src/presenter.c/.h`（逻辑画布作 SDL 静态纹理上传，GPU 按宽高比/滤镜缩放，窗口可拖动缩放）；`sango3view show` 默认开 2K 窗口，支持 `--out/--aspect/--filter`；dummy 驱动下验证通过（窗口物理尺寸/内容矩形/帧数均符合公式）
  - 顺带修正旧 `sango3view_sdl.c` 的纹理格式 bug（误用 `ABGR8888`，应为 `RGBA8888`）
  - SDL_ttf 字体层已完成：`font.c/.h`（像素/高清双模式 + 回退链），中文自检通过
- **M2**（✅ 09-14 收口）UI 布局系统 + 控件树运行时 + 交互态 + 场景切换
  - M2-1~M2-4：`ini.c`（#include 递归）→ `ui.c/.h`（Menu.ini）→ `ui_probe.c` + `tools/build_ui.py`
    + `tools/verify_ui_c.py`（**11530 项 C↔Python 完全一致**）
  - 数据口径：**必须取 Update.PAK 的 Menu.ini**（两包都开，Update 覆盖）；详见 `docs/UI布局与Menu.ini.md`
  - M2-5/6/7：`menu_scene.c/.h`（控件树运行时 + 四态 + 命中测试 + 解码缓存）、
    `menu_probe.c`（`sango3menu` 离线出图）、`sango3app.c`（真实窗口交互主程序）
  - **场景 = 一组 root**（叠加显示）；映射：1→選擇時期 / 2→存檔 / 3→登錄武將 /
    5→設定選項 / 6→退出；剧本按钮 cmd=**11..17** → `City01~07.ini`
- **M3-lite**（✅ 09-15）自定义武将 + 开局流程 + 战略层地图
  - `editor_scene.c/.h`：创建自定义武将表单（姓名**真文本输入**（安卓中文 IME 实测可用）/
    性别 / 头像（原版自创脸谱 `Shape\Portrait\{mFace001-030,wFace001-020}.SHP`）/
    四维随机 / 保存 JSONL）；**「登錄武將」= 创建自创武将**（用户纠正，勿当浏览列表）
  - `kingdom_scene.c/.h`：選択君主（剧本数据 `Setting\City01~07.ini` 的 Lord 字段）
  - `strategy_scene.c/.h`：战略层地图（整图 `Shape\AD\Base\Map.shp` 1024×768 + 70 城 +
    视口拖动）；**详见 `docs/战略地图格式.md`**
  - 开局状态写 `start_state.json`（Android 私有目录 / PC `tmp/`）
- **M4**（✅ 09-14 跑通；09-15 补齐字体与 EXTEND）
  - Android 手工打包链路（不依赖 gradle）→ 主菜单在雷电模拟器运行；
    **详见 `docs/Android构建.md`**（9 个坑 + 两条非报错型坑）
  - **字体：FreeType 直连后端**（`SANGO3_HAVE_FREETYPE`；不用 SDL2_ttf，避免 harfbuzz 链路）
  - **EXTEND**：长屏不留黑边（内容按长边撑满 + 边缘条带镜像延展）；
    **COVER**：内容类场景（地图）铺满裁切、**顶对齐**
  - 遗留：无（2K 压测可后补）
- **M5**（未启动，按约定放最后）素材高清化（纯资源替换）+ 素材层简繁统一

## 5 项需求技术落点
| # | 需求 | 实现位置 |
|---|---|---|
| ① | 自动升级（我方将领）| C 引擎事件函数（参考 `docs/必杀技学习规则.md` 同模式） |
| ② | 士兵上限 400→1000 | `gamedata.c` 的 `soldier_limit` 字段（已设为 1000） |
| ③ | 必杀技携带无限制 | `gamedata.c` 的 `S3_MAX_SA=8` + 运行时数组 |
| ④ | 武力≥80 自动学必杀技 | `gamedata.c` 的 `s3_general_on_attack_changed` |
| ⑤ | 高清化 | 渲染层 `render.c`（素材分级 tier）+ `docs/分辨率与高清化架构.md` |

## 必杀技规则定稿
- 判定：**当前攻击力 ≥ 80**（基础武力 + 武器加成，**实时计算不缓存**）
- 排除：**野兽**（智力 ≤ 20 且 HP ≥ 150，恰好 4 只：猛虎/白额虎/南蛮象/印度神象）
- 学习策略：可注入 picker，默认随机（原版 79 名自带武将的 SA 编号与 WeaponType/武力档位无单调关系，待逆向）
- 回落：**不收回**（monotonic 语义）

## 工程红线
- **C 与 Python 基准必须 100% 一致**：数据层改动后跑 `tools/verify_data_c.py`（9024 项）；
  UI 布局改动后跑 `tools/verify_ui_c.py`（11530 项）。两者都要绿。
- **推送策略：B —— GitHub 私有仓库 + SSH**（2026-09-14 用户确认，沿用 9-13 净化成果）；本机 `github.com:22` 被拒 → 走 `ssh.github.com:443`（已配 `~/.ssh/config`）
- **pre-commit 钩子必装**：`python tools/install_hooks.py`（钩子随 .git 但不随仓库分发，跨机后要重装）
- **本机路径一律用 `%USERPROFILE%` / `os.path.expanduser("~")`**（不入个人信息，跨机可移植）
- **零散临时文件统一放项目根 `tmp/`**（已 gitignore，仅 `.gitkeep` 占位跨机携带）：本机生成、可重建的产物优先进 `tmp/`，不散落项目各处、不落 C 盘。
- ⛔ **本机禁止执行 `git gc` 和 `git filter-repo`**（2026-09-14 实测：二者都会摧毁 `.git` ——
  `fatal: not a git repository`，随后 `HEAD`/`config`/`refs`/`index`/`hooks` 全丢，只剩 `info/`+`objects/`；
  filter-repo 因收尾固定调 `gc` 而同样不可用）。
  需要改写历史时用**手工重建**（`read-tree`+`rm --cached`+`write-tree`+`commit-tree`+`update-ref`+`reset --hard`），
  步骤见 `memory/2026-09-14.md`「收尾：仓库历史净化」。
  `git reflog expire --expire=now --all` 是安全的，但**不要跟 `gc`**。
  不做 gc 也能让远端干净：**`push` 只传可达对象**，refs 干净即可。
- **改历史前必须全量备份到仓库外**（本次 `E:/sango3-backup-20260914/`）；
  操作在**纯 ASCII 路径副本**上做，绕开中文路径附加坑。
- 本机 shell 缺 `grep`/`ls`/`tail`/`sleep`/`dirname`，PowerShell 工具偶发无输出 → **一律改用 Python 调 subprocess**；
  `robocopy` 退出码 1 是「成功」（0-7 均成功），别误判。

## 双机开发
- 周末台式机（本环境，主开发机）
- 雷电模拟器在本机：`F:\leidian\LDPlayer9`（含 dnplayer.exe / ldconsole.exe / 自带 adb.exe），2026-09-13 已实机核实。
- 早前记忆误把雷电归到"工作日笔记本"，以此条为准。

## Android 集成（M4）—— ✅ 已跑通（2026-09-14）
- **主菜单已在雷电模拟器正常运行**（截图 `tmp/android_shot2.png`，颜色/布局与 PC 一致）。
- 工具链（均 E 盘，一次性部署）：
  - NDK r27c `E:/android-ndk/android-ndk-r27c`
  - JDK 17 `E:/android-sdk/jdk17/jdk-*`（**必须 11+**：d8/sdkmanager 需要，系统只有 JDK 8）
  - SDK `E:/android-sdk/{cmdline-tools/latest, platform-tools, build-tools/34.0.0, platforms/android-34}`
- 构建入口：**`tools/build_android.py`**（手工打包，**不依赖 gradle**；ASCII 工作区 `E:/sango3-android`
  规避中文路径；`android/app/jni/CMakeLists.txt` 编 engine → `libmain.so`）。
- 资源：adb push 到**应用外部私有目录** `/sdcard/Android/data/org.libsdl.app/files/Sango3/`
  （公共 `/sdcard/` 需运行时存储权限 → 会导致启动即退出）。
- **详见 `docs/Android构建.md`** —— 含 9 个坑点与两条非报错型坑：
  ① ninja 需 `-DCMAKE_MAKE_PROGRAM` ② CMake 相对路径层级（三级）
  ③ 脚本 rmtree 被安全策略拦截 → 改 `dirs_exist_ok` ④ manifest 需 `package` 属性
  ⑤ android.jar 要直下 platform zip ⑥ javac 需 `-encoding utf-8`（SDLActivity.java 含 emoji）
  ⑦ d8 需 JDK17（显式 `JAVA_HOME`）⑧ APK 必须含 `libSDL2.so`（Java `System.loadLibrary("SDL2")`）
  ⑨ 资源路径用私有目录；另：`__android_log_print` 需链接 `log`；
  **GLES 通道顺序相对 SDL 定义是反转的 → Android 用 `SDL_PIXELFORMAT_ABGR8888`**（否则整屏偏色）。
- 已知限制：无（字体与 EXTEND 均已于 2026-09-15 完成）。
- **字体后端选型**：不用 SDL2_ttf（CMake 拉 harfbuzz 链路长）；font.c 内双后端
  （`SANGO3_HAVE_TTF`=PC / `SANGO3_HAVE_FREETYPE`=Android），接口一致。
  PC 侧验证：freetype-pc 静态库 + 探针出图两种模式均正确（`tmp/font_hd24.png`）。
- **安卓实操补充（2026-09-15）**：
  - `adb install -r` 在本机雷电上**会卡死**（300s 无响应）→ 用
    `adb push apk /data/local/tmp/` + `adb shell pm install -r`（<1s）；已固化进 `tools/build_android.py`。
  - 模拟器**本环境无法自动启动**（GUI 进程被拦截）→ 用 `ldconsole.exe launch --index 0`
    或请用户手动启动；`ldconsole list2` 可查实例。
  - 文本输入测试可用 `adb shell input text`（英文）；**中文 IME 用户实测可用**（存档里出现手输中文名）。
  - 窗口尺寸实测：横屏 **2560×1392**，竖屏 1080×1848。
  - `input tap` 注入坐标与 presenter 反算存在 ~72px(y) 偏差（原因未查，不影响真人操作；
    自动化验证时按需补偿）。
  - ⚠ **用户在场操作模拟器时不要用 adb 自动点击**（会点到模拟器桌面/商店，干扰用户）——
    改用"数据侧离线验证 + 交用户实测"。

## 渲染管线要诀（2026-09-15 实战总结，改渲染前必读）
1. **"逻辑分辨率"不是常量**：素材分辨率高于 640×480 时（地图 1024×768），把逻辑画布
   切到素材原生分辨率（`sango3_presenter_set_logical_size`）→ GPU 只放大 2~3×。
   塞进 640×480 再放大 = 先降采样再放大，必然发糊（用户实测：截图 PNG 500KB→4.5MB）。
2. **宽高比三选一**：`PILLARBOX`（留黑边）/ `EXTEND`（UI 类场景：按长边撑满 + 边缘条带
   镜像延展，不留黑边不变形）/ `COVER`（内容类场景：铺满裁切，**顶对齐**——居中裁会把
   顶部信息条裁到屏幕外）。均可运行时切换（`set_aspect`）。
3. **滤镜随场景切**：像素素材 `NEAREST`（锐利）↔ 照片级大图 `BILINEAR`（平滑）。
4. **坐标语义**：INI 里**子窗口 Range 相对父窗口**（渲染/命中都要累计父偏移）；
   **城市按钮 Range 是矩形左上角**，中心 = `+w/2, +h/2`。
5. **每帧必须清画布**：场景不一定是全屏覆盖（子场景只占部分屏幕），不清屏会残留上一场景像素。
6. **视口滚动优于整图平移**：画布尺寸 = 屏幕宽高比对应的地图区域，1:1 裁取（清晰度不损）。

## M3/M4 追加的「不报错但静默错」教训（接前文）
5. **快速点按丢失**：按下/抬起落在同一帧时，用"电平判定抬起"（`prev_ldown`）会整次丢点击
   → 用**按下边沿记录 press + 无按压即触发**。
6. **不清画布**（见要诀 5）。
7. **安卓 Back 键**：默认会退出 Activity → 需 `SDL_HINT_ANDROID_TRAP_BACK_BUTTON=1`
   拦下，再把 `SDLK_AC_BACK` 当"右键返回"用。
8. **纯 C 探针的 Windows 编码坑**（`tmp/` 脚本层面）：`argv` 是 ANSI(GBK) → 中文文本要
   `CommandLineToArgvW` + `WideCharToMultiByte(CP_UTF8)` 重转；`fopen` 认不了中文路径 →
   输出走 ASCII 路径（`E:/sango3-android/`）。
9. **`git add` 后 commit 前的 CRLF 警告是正常的**（Windows），不是错误。
10. **本机 `git` 不在 PATH**：用绝对路径
    `%USERPROFILE%/.workbuddy/binaries/PortableGit/versions/*/cmd/git.exe`（Python subprocess 调用）。

## 数据确定性产物清单
- `engine/assets/encoding/big5_cp950.bin` ← `tools/gen_encoding_tables.py`
- `engine/assets/encoding/hant2hans.bin` ← `tools/gen_encoding_tables.py`
- `.workbuddy/data/json/*.json` ← `tools/build_data.py` + `tools/build_lang_table.py`
- `.workbuddy/data/json/ui.json` ← `tools/build_ui.py`（UI 布局基准）
- 全部可重建；上游变更（PAK/INI/繁简词条）后必须重跑 + 验证。
- **游戏 PAK 定位**：`tools/build_ui.py` 的 `resolve_paks()`，候选 = 环境变量 `SANGO3_DIR`
  → `E:\Program Files (x86)\steam\steamapps\common\Sango3` 等。优先级 **Update.PAK 覆盖 Sango3.PAK**。
- **玩家数据（运行时生成，位置随平台）**：
  `custom_generals.jsonl`（自定义武将，JSONL 追加）/ `start_state.json`（开局状态）
  —— Android 落应用私有目录 `/sdcard/Android/data/org.libsdl.org/files/Sango3/`，PC 落 `tmp/`。
- **Android 资源推送**：`python tools/build_android.py --push-assets`（encoding + fonts →
  设备私有目录）；PAK 需手工 push（305MB，`tmp/android_verify.py` 有完整流程参考）。

## 跨语言字段对照的教训
C 与 Python 逐字段对照脚本（`tools/verify_data_c.py`）的列索引约定必须**双侧锁定在同一处**：
- C 侧在 TSV 头注释里写明列序（`gamedata_probe.c` 的 write_dump）
- Python 侧解析时校对列序
否则两边各自增减字段就会**静默错位**（第一轮 verify 漏读 is_beast，导致 rank/sex/wtype/portrait 整体错位一格，1480 项"假阳性"不一致 —— 实际 C 完全正确）。

### M2 追加的三条同类教训（都是「不报错、静默错」）
1. **计数变量别依赖循环自增**：`for (n=0;n<4;++n){...break;}` 走 `break` 时 n 不递增，
   `parse_rect_clean` 因此把**所有**矩形判失败并静默置 0。
   对策：解析成功即 `seg[n++]`；再加一个 sanity 计数（如 `range_wh_zero`）兜底。
2. **解码前必须 `s3_text_init()`**：漏了不报错，只把 Big5 原字节输出 → 数量全对、中文全乱码。
   对策：probe 启动时 init，未 ready 直接退出。
3. **别为了"过验证"改写数据**：TSV 里把制表符替换成空格会让两侧都"对上"但值已被改写。
   对策：两侧用**同一套可逆转义**（`\t`/`\n`/`\r`/`\\`/`\xNN`），不丢信息。
4. **`size_t` 下溢 → 堆越界读（段错误）**：`end - off - FRAME_HDR` 为负时被当成巨大无符号数，
   `span` 因此不被裁剪 → 越界读内存（`root=20000`/Statusbar 素材触发段错误，M2 收口时修复）。
   对策：长度差先做**有符号判断**，不足时归零（容错），绝不直接拿无符号减法当"可用量"。
