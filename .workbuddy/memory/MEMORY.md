# Sango3 安卓原生移植 —— 长期账本（精简索引）

> **权威源分工**：规则/口径/红线 → `docs/项目约定.md`；渲染 → `docs/分辨率与高清化架构.md`；
> UI 逆向与实现落点 → `docs/城池信息面板与行政菜单.md`；
> **玩法/数值口径 → `docs/系统功能设定定稿.md`（唯一权威源，用户复核定稿）**；
> 其余专题 → `docs/{必杀技学习规则,UI布局与Menu.ini,战略地图格式,Android构建}.md`。
> 本文件**只记"进度 + 独有环境事实"**，不再复述文档已有内容（避免两处改一漏一）。
> **续接顺序**：本文件 → `memory/` 最近一篇日志 → `docs/项目约定.md`
> → `docs/系统功能设定定稿.md`（做玩法前必读）→ 按任务查其余 docs/。

## 路线与当前进度（2026-09-16 收工；HEAD `cc08920`，52 提交，`main`）
A→B 递进：A 档（2~4 周 demo 级原型）→ B 档（4~6 月忠实还原）。
已打通：主菜单 → 選擇時期 → 選擇君主 → 開局 → **朝堂（内政阶段）** ⇄ 战略地图（70 城可拖动）
→ 点城弹城池信息面板（原版 7400）+ 行政主選單七组全铺开（原版 8000）。
C↔Python 回归 **9024 + 11530 项全绿**；APK 已多轮装机实测。
> ⚠ 09-16 全部改动（面板 + 行政菜单 + 朝堂 + 长按 + 文档）**尚未提交**（等用户授权）。

**下一步 —— 按 `docs/系统功能设定定稿.md`「实现批次」**：
- 第一批：①执行者选择界面 ②武将月度行动限制（每将每月 1 条，守城支援除外）
  ③訓練/士气（城池粒度、不扣钱）④調查/情報重做（非我方城 + 6 个月有效期）
- 第二批：势力插旗（`Nation.ini` Flag）+ 选君主界面重做为大地图版 + 城名层
- 第三批：存档位系统（10 槽 + 读档历史）+ 设置项
未启动：**M5 高清素材包**（按约定放最后）。
遗留：**远端仍待用户提供私有仓库地址**（策略 B 已就绪，至今未推送）。

## 里程碑状态
| 阶段 | 内容 | 状态 |
|---|---|---|
| M0 | 格式破解 + 分辨率无关渲染骨架 | ✅ |
| M1-a | 数据层 + 规则引擎（421 武将等，9024 项） | ✅ |
| M1-b | GPU 渲染（presenter）+ 显示层 | ✅ |
| M2 | UI 布局 + 控件树运行时 + 场景切换（11530 项） | ✅ 09-14 收口 |
| M3-lite | 自定义武将 / 开局流程 / 战略地图 | ✅ 09-15 |
| M3-lite+ | 城池面板 7400 + 行政主選單 8000 + 朝堂 | ✅ 09-16 |
| M4 | 安卓手工打包链路（不依赖 gradle） | ✅ 09-14；09-15 补字体与 EXTEND |
| M5 | 素材高清化 + 素材层简繁统一 | ⬜ 未启动（最后做） |

## 5 项需求技术落点
| # | 需求 | 落点 |
|---|---|---|
| ① | 我方将领自动升级 | 引擎事件函数（同 `docs/必杀技学习规则.md` 模式） |
| ② | 士兵上限 400→1000 | `gamedata.c` 的 `soldier_limit`（已设 1000） |
| ③ | 必杀技携带无限制 | `gamedata.c` 的 `S3_MAX_SA=8` + 运行时数组 |
| ④ | 武力（攻击力）≥80 自动学 | `gamedata.c` 的 `s3_general_on_attack_changed` |
| ⑤ | 高清化 | `render.c` 素材分级 tier（M5 做） |

> 口径细则（含"排除野兽""不收回"等）以 `docs/项目约定.md` 第二节为准。

## 独有环境事实（文档里没有的）
- **双机**：周末台式机（本环境 = 主开发机，雷电 `F:\leidian\LDPlayer9`）+ 工作日笔记本；
  源码/文档/记忆走 Git（收工 push、开工 pull），素材各机本地生成。
- **工具链**（均 E 盘，一次性部署）：NDK r27c / JDK17 / SDK —— 路径见 `docs/Android构建.md` 第二节。
  构建入口：`python tools/build_android.py --install --push-assets`。
- **本机 shell 缺 `grep/ls/tail/sleep/dirname`** → 一律改用 Python `subprocess`；
  `git` 不在 PATH → 用绝对路径 `%USERPROFILE%/.workbuddy/binaries/PortableGit/versions/*/cmd/git.exe`。
- **模拟器实测窗口**：横屏 2560×1392 / 竖屏 1080×1848；
  `input tap` 注入坐标与 presenter 反算有 ~72px(y) 偏差（未查原因，不影响真人操作）。
  模拟器本环境**无法自动启动**（GUI 被拦截）→ `ldconsole.exe launch --index 0` 或用户手启。
- **确定性产物（可重建；上游 PAK/INI/词条变了必须重跑 + 双重验收）**：
  `engine/assets/encoding/{big5_cp950,hant2hans}.bin` ← `tools/gen_encoding_tables.py`；
  `.workbuddy/data/json/*.json` ← `tools/build_data.py` / `build_lang_table.py` / `build_ui.py`。
- **游戏 PAK 定位**：`tools/build_ui.py` 的 `resolve_paks()`（环境变量 `SANGO3_DIR` 优先）；
  **两包都开，Update 覆盖 Sango3**。
- **玩家运行时数据**：`custom_generals.jsonl`（自创武将，JSONL 追加）+ `start_state.json`；
  Android 落应用私有目录，PC 落 `tmp/`。
