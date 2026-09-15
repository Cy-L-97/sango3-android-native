# Sango3 Android Native Port — Long-term Notes

## 新会话 / 换机续接（先读）
1. 本文件（MEMORY.md，长期账本）→ 2. 最近一篇日志 `memory/YYYY-MM-DD.md` → 3. **`docs/项目约定.md`（规则与口径，用户要求"必须遵守"）**。
   再按任务查：`docs/分辨率与高清化架构.md` / `docs/必杀技学习规则.md` / `docs/UI布局与Menu.ini.md`。
   上下文丢失不影响续接；**事实以文档为准**，不依赖聊天记录。

## 路线
A→B 递进：A 档（2~4 周 demo 级原生原型）→ B 档（4~6 月忠实还原）。
**当前进度**（2026-09-15）：**M2 收口** + **M4 字体打通** —— 主菜单 + 文本（V2.2C）已在
雷电模拟器正常运行（FreeType 直连后端）。
仓库历史已净化（38 提交，`main`）—— 误入库的两张版权截图已从历史彻底移除。
M2 遗留：地图地形层（BlkData + `Shape\SF\Map` 瓦片拼接）；M3 部分就绪；
M4 遗留：EXTEND 宽高比（字体已于 09-15 用 FreeType 直连解决）。

## A 档里程碑
- **M0**（已完）格式破解 + 分辨率无关渲染骨架
- **M1-a**（已完）数据层 + 规则引擎
  - 421 武将 / 103 物品 / 9 兵种 / 125 武将技，**9024 项字段级 C 与 Python 完全一致**
  - 见 `tools/verify_data_c.py`（任何改动后必跑）
- **M1-b**（已完）GPU 化渲染 + SDL_ttf 集成显示层
  - **GPU 路径 A 已完成**：`engine/src/presenter.c/.h`（逻辑画布作 SDL 静态纹理上传，GPU 按宽高比/滤镜缩放，窗口可拖动缩放）；`sango3view show` 默认开 2K 窗口，支持 `--out/--aspect/--filter`；dummy 驱动下验证通过（窗口物理尺寸/内容矩形/帧数均符合公式）
  - 顺带修正旧 `sango3view_sdl.c` 的纹理格式 bug（误用 `ABGR8888`，应为 `RGBA8888`）
  - SDL_ttf 字体层已完成：`font.c/.h`（像素/高清双模式 + 回退链），中文自检通过
- **M2**（UI 布局系统已完成；场景驱动进行中）UI 框架（读 `Menu.ini`，M2-1~M2-4 已完成）+ 场景驱动（地图 + 战场，待做）
  - **M3-lite（2026-09-15）自定义武将创建已跑通**：`editor_scene.c/.h` 自绘表单
    （姓名真文本输入/性别/头像/四维随机/保存 JSONL）；presenter 增文本轮询接口；
    **登录武将=创建自创武将**（用户纠正，勿再当浏览列表）；开局注入待做。
  - **M3-lite 开局流程第一步已跑通（2026-09-15）**：選擇時期 → 選擇君主 → 開局。
    **剧本→君主数据在 `Setting\City01~07.ini`**（城池的 Lord 字段），
    势力定义在 `Setting\Nation.ini`（旗号/外交，战略层要用）；
    剧本按钮 cmd=11..17。`kingdom_scene.c/.h` 自绘君主表；
    开局状态写 `start_state.json`。战略层（地图）待接。
  - **M2-1~M2-4 已完成**：`ini.c` 支持 `#include` 递归展开；`ui.c/.h` 解析 Menu.ini；
    `ui_probe.c` 验证器；`tools/build_ui.py` + `tools/verify_ui_c.py`
    **11530 项字段级 C↔Python 完全一致**
  - 数据口径：**必须取 Update.PAK 的 Menu.ini**（WINDOW 325 / ICON 303 / COLOR 36；
    Sango3.PAK 版是 323/302/36）；`define.ini` 只在 Sango3.PAK → 两包都开，Update 覆盖
  - 详见 `docs/UI布局与Menu.ini.md`（含五个「不报错但静默错」的坑）
  - **M2-5 已完成**：`menu_scene.c/.h`（控件树运行时）+ `menu_probe.c`（`sango3menu`）+ `tools/render_menu.py`。
    主菜单出图 = 背景 `Main.shp`(640×480) + 5 按钮 `MM\Button\*.shp`(172×46) + 文本 `V2.2C`；
    `drawn=6 / icon_missing=0 / asset_missing=0 / text=1`。
  - **素材层简繁混杂**：主菜单 / 设定选项等素材是**简体**（「开始游戏」「选项设定」），
    而存档界面素材是**繁体**（「讀取自動存檔」）→ 图片内文字**无法**用查表转换，
    需 M5 高清包重绘时一并统一；数据层 / 文本层仍走繁→简映射表。
  - **`LogoFire01.SHP` 是 SHP 特殊变体**：偏移表值域异常（`frame range invalid`），火焰动画层暂跳过；
    失败已由 summary 的 `decode_fail <路径> <原因>` 可见（不再静默）。
  - **M2-6 交互态**：`menu_scene` 加 `state_of`（逐控件状态回调）+ `hit_test`（后画优先）+ 解码缓存；
    `presenter` 暴露 `S3Pointer`（逻辑坐标 + 左右键电平/边沿）；新增 `sango3app`（真实窗口交互主程序，
    右键返回、场景栈）。
    ⚠ **四态语义**：非 normal 态值为 `Normal` = **沿用 normal 素材（回退）**，不是"无素材"；
    按占位跳过会让悬停时背景整块消失（drawn 6→5）。
  - **M2-7 场景切换**：**场景 = 一组 root**（原版同一时刻叠加显示多个窗口）。
    映射：**1→選擇時期[100] / 2→存檔[220,201~210,240,230] / 3→登錄武將[400,410] / 5→設定選項[300~309] / 6→退出**。
    实测各场景 `drawn` 12~13、`asset_missing=0`。
  - **列表类控件**（BUTTONREPORT / LIST）的内容是**运行时数据**，当前只渲染 Menu.ini 声明的框架。
  - 菜单 command → 界面 id 对照见 `tmp/list_roots.py` 的输出（99 个 root 已枚举）。
  - 待做：其余界面映射；地图 / 战场场景

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
- 已知限制：~~首版无字体~~（**2026-09-15 已解决**：FreeType 2.13.3 直连后端，
  `SANGO3_HAVE_FREETYPE`，实测安卓端文本 V2.2C 正常显示）；EXTEND 宽高比未做。
- **字体后端选型**：不用 SDL2_ttf（CMake 拉 harfbuzz 链路长）；font.c 内双后端
  （`SANGO3_HAVE_TTF`=PC / `SANGO3_HAVE_FREETYPE`=Android），接口一致。
  PC 侧验证：freetype-pc 静态库 + 探针出图两种模式均正确（`tmp/font_hd24.png`）。

## 数据确定性产物清单
- `engine/assets/encoding/big5_cp950.bin` ← `tools/gen_encoding_tables.py`
- `engine/assets/encoding/hant2hans.bin` ← `tools/gen_encoding_tables.py`
- `.workbuddy/data/json/*.json` ← `tools/build_data.py` + `tools/build_lang_table.py`
- `.workbuddy/data/json/ui.json` ← `tools/build_ui.py`（UI 布局基准）
- 全部可重建；上游变更（PAK/INI/繁简词条）后必须重跑 + 验证。
- **游戏 PAK 定位**：`tools/build_ui.py` 的 `resolve_paks()`，候选 = 环境变量 `SANGO3_DIR`
  → `E:\Program Files (x86)\steam\steamapps\common\Sango3` 等。优先级 **Update.PAK 覆盖 Sango3.PAK**。

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
