# Sango3 Android Native Port — Long-term Notes

## 路线
A→B 递进：A 档（2~4 周 demo 级原生原型）→ B 档（4~6 月忠实还原）。
**当前进度**：M1-b（GPU 化渲染 + SDL_ttf 字体显示层）完成。

## A 档里程碑
- **M0**（已完）格式破解 + 分辨率无关渲染骨架
- **M1-a**（已完）数据层 + 规则引擎
  - 421 武将 / 103 物品 / 9 兵种 / 125 武将技，**9024 项字段级 C 与 Python 完全一致**
  - 见 `tools/verify_data_c.py`（任何改动后必跑）
- **M1-b**（进行中）GPU 化渲染 + SDL_ttf 集成显示层
  - **GPU 路径 A 已完成**：`engine/src/presenter.c/.h`（逻辑画布作 SDL 静态纹理上传，GPU 按宽高比/滤镜缩放，窗口可拖动缩放）；`sango3view show` 默认开 2K 窗口，支持 `--out/--aspect/--filter`；dummy 驱动下验证通过（窗口物理尺寸/内容矩形/帧数均符合公式）
  - 顺带修正旧 `sango3view_sdl.c` 的纹理格式 bug（误用 `ABGR8888`，应为 `RGBA8888`）
  - 待做：SDL_ttf 字体渲染接入（含回退链）
- **M2**（待）场景驱动（地图 + 战场）+ UI 框架

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
- **C 与 Python 基准必须 100% 一致**（任何 INI/字段变更后跑 `tools/verify_data_c.py`）
- **不直接 git push GitHub**（用户决定不推送，本地继续）
- **pre-commit 钩子必装**：`python tools/install_hooks.py`（钩子随 .git 但不随仓库分发，跨机后要重装）
- **本机路径一律用 `%USERPROFILE%` / `os.path.expanduser("~")`**（不入个人信息，跨机可移植）
- **零散临时文件统一放项目根 `tmp/`**（已 gitignore，仅 `.gitkeep` 占位跨机携带）：本机生成、可重建的产物优先进 `tmp/`，不散落项目各处、不落 C 盘。

## 双机开发
- 周末台式机（本环境）
- 工作日笔记本（雷电模拟器路径 `F:\leidian\LDPlayer9`）

## 数据确定性产物清单
- `engine/assets/encoding/big5_cp950.bin` ← `tools/gen_encoding_tables.py`
- `engine/assets/encoding/hant2hans.bin` ← `tools/gen_encoding_tables.py`
- `.workbuddy/data/json/*.json` ← `tools/build_data.py` + `tools/build_lang_table.py`
- 全部可重建；上游变更（PAK/INI/繁简词条）后必须重跑 + 验证。

## 跨语言字段对照的教训
C 与 Python 逐字段对照脚本（`tools/verify_data_c.py`）的列索引约定必须**双侧锁定在同一处**：
- C 侧在 TSV 头注释里写明列序（`gamedata_probe.c` 的 write_dump）
- Python 侧解析时校对列序
否则两边各自增减字段就会**静默错位**（第一轮 verify 漏读 is_beast，导致 rank/sex/wtype/portrait 整体错位一格，1480 项"假阳性"不一致 —— 实际 C 完全正确）。
