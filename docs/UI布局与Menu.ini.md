# UI 布局系统（M2）—— Menu.ini 逆向与解析

> 状态：M2-1 ~ M2-4 已完成。C 引擎与 Python 基准**逐字段 100% 一致**（11530 项检查）。
> 相关代码：`engine/src/ui.h` / `ui.c`（解析）、`engine/src/ui_probe.c`（验证器）、
> `engine/src/ini.h` / `ini.c`（`#include` 展开）、
> `tools/build_ui.py`（Python 基准）、`tools/verify_ui_c.py`（对照）。

---

## 一、原版是怎么描述界面的

《三国群英传3》**没有把界面写死在代码里**，而是用一张声明式表 `Setting\Menu.ini` 描述全部门户：

```
[WINDOW]          控件：位置、尺寸、样式、控件类、引用的图标与配色、子控件…
[ICON]            图标：素材目录 + Normal/Focus/Down/Disable 四态素材名
[COLOR]           配色：四态颜色值
```

三个 `[XXX]` 段在文件里**交替出现**（ICON 紧跟它所属的那个 WINDOW），
按出现顺序解析即可；段名相同但语义靠 `ID` 区分。

规模（取 **Update.PAK** 版本，7048→ 实际 665 个有效段）：

| 段 | 数量 |
|---|---|
| WINDOW | **325** |
| ICON   | **303** |
| COLOR  | **36**  |

> ⚠ **必须取 Update.PAK 的 Menu.ini**（121,105 字节），不是 Sango3.PAK 的（120,328 字节）。
> 两者相差 2 个控件 / 1 个图标，用错包会让验证对不上。
> 原版 Update 包**覆盖**基础包同名条目；`define.ini` 只在 Sango3.PAK 里，
> 所以两个包都要打开，按「Sango3 先、Update 后覆盖」的顺序查。

---

## 二、WINDOW 控件

```ini
[WINDOW]
Comment = 主選單          ; 开发者备注（繁体，仅调试用）
ID      = 1
Style   = wsVisible, wsIcon
Class   = WND_CLASS_BASE
Range   = 0, 0, 640, 480   ; x, y, w, h —— 逻辑坐标
Icon    = 1                ; 引用 ICON 的 ID
Child   = 2                ; 引用 WINDOW 的 ID，可写多行
Child   = 3
```

| 字段 | 含义 | 缺省 |
|---|---|---|
| `ID` | 控件 ID（WINDOW 空间内唯一） | 0 |
| `Range` | 逻辑矩形 `x, y, w, h`，**640×480 逻辑分辨率** | 全 0 |
| `WorkRange` / `FillRange` | 内容区 / 填充区（可选，多数控件不写） | 全 0 |
| `Style` | 逗号分隔的位标志集合 | 0 |
| `Class` | 控件类 | UNKNOWN(99) |
| `Icon` | 引用的 ICON ID | -1 |
| `Command` | 点击后发往逻辑层的命令号 | -1 |
| `Font` / `FColor` / `BColor` | 字体号 / 前景色 ID / 背景色 ID（引用 COLOR） | -1 |
| `Cols` / `Rows` / `Lines` / `Check` | 列表与复选相关 | -1 |
| `Title` / `Comment` | 标题 / 备注（Big5 原文解码后的 UTF-8） | NULL |
| `Child` | **可重复键**，子控件 ID 列表 | 空 |

### 控件类（13 种）

| 类 | 数量 | 用途 |
|---|---:|---|
| `WND_CLASS_BASE` | 61 | 容器 / 面板（可含子控件） |
| `WND_CLASS_BUTTON` | 119 | 按钮 |
| `WND_CLASS_STATIC` | 66 | 静态文本 |
| `WND_CLASS_LIST` | 42 | 列表 |
| `WND_CLASS_SCROLLBAR` | 14 | 滚动条 |
| `WND_CLASS_MSTATIC` | 7 | 多行静态文本 |
| `WND_CLASS_BUTTONREPORT` | 5 | 报表型按钮组 |
| `WND_CLASS_PROGRESS_BFHP` | 4 | 战场血量条 |
| `WND_CLASS_PROGRESS` | 2 | 进度条 |
| `WND_CLASS_PROGRESSEX` | 2 | 扩展进度条 |
| `WND_CLASS_BFRADAR` | 1 | 战场雷达 |
| `WND_CLASS_TIMER` | 1 | 计时器 |
| `WND_CLASS_BFMESSAGE` | 1 | 战场消息 |

### Style 位标志

`ui.h` 中 `S3_WS_*`，出现频次（Update 版）：

| 标志 | 位 | 用次 |
|---|---:|---:|
| `wsVisible` | 0x00000001 | 182 |
| `wsIcon` | 0x00000002 | 246 |
| `wsVCenter` | 0x00000004 | 216 |
| `wsHCenter` | 0x00000008 | 197 |
| `wsText` | 0x00000010 | 88 |
| `wsCheck` | 0x00000020 | 47 |
| `wsVScroll` | 0x00000040 | 41 |
| `wsHScroll` | 0x00000080 | 20 |
| `wsSCheck` | 0x00000100 | 13 |
| `wsRight` | 0x00000200 | 9 |
| `wsLeft` | 0x00000400 | 8 |
| `wsHorizontal` | 0x00000800 | 8 |
| `wsTrans` | 0x00001000 | 6 |
| `wsReport` | 0x00002000 | 6 |
| `wsForceSelectChange` | 0x00004000 | 7 |
| `wsRight2Left` | 0x00008000 | 4 |
| `wsLeft2Right` | 0x00010000 | 4 |

---

## 三、ICON 与 COLOR

```ini
[ICON]
ID = 2
Pos = 0,0                  ; ⚠ 只有 2 段，不是矩形；可以是负数（-6, -6）
Dir = \MM\Button\          ; 素材目录（Shape 下的相对路径）
Normal  = NewGame
Focus   = NewGame2
Down    = NewGame3
Disable = Normal
```

```ini
[COLOR]
ID = 301
Normal  = 220,220,220,1 ；白     ; 注意：这里可能带行内注释
Focus   = Normal                  ; ⚠ 可能不是数字，是「沿用某态」的语义串
```

---

## 四、五个必须记住的坑

### ① 三类 ID 空间互相独立

WINDOW / ICON / COLOR 的 ID **各是一套**，靠 `Icon` / `FColor` / `BColor` / `Child`
交叉引用。解析时绝不能混在一个 map 里。
（交叉引用完整性已验证：icon 239 / fcolor 136 / bcolor 116 / child 286，**缺失全为 0**。）

### ② 行内注释要单独处理

`ini.c` 只处理「整行注释」（行首 `;` 或 `#`），值里仍残留行内注释：

```
Normal = 21,89,210,1		; 淡藍
```

`ui.c` 取值后统一 `strip_inline()`（切到 `;` / `#` 之前，再去首尾空白）。

### ③ Style 里有笔误与脏数据

- `wcIcon` 应为 `wsIcon`，原版出现十几次 → **一并识别为 wsIcon**；
- 存在 `Style = Class = WND_CLASS_BASE` 这类把下一行写进来的脏数据
  → 未知 token **一律忽略**，不报错。

### ④ 四态值不一定是数字

COLOR 的 `Normal/Focus/Down/Disable` 可能是 `"21,89,210,1"`，也可能是 `"Normal"`。
**必须保留原串**（`S3UiState.raw`），不能强转整数——强转会静默丢信息。
`n_seg == 0` 即表示「非数字串」。

### ⑤ 解析中文前必须先载入编码表

`s3_text_init(encoding_dir)` 载入 Big5→UTF-8 表；**漏了它不会报错**，
只会把 Big5 字节原样输出（在 UTF-8 视角下是乱码），
表现为「数量全对、中文全错」的静默不一致。`ui_probe.c` 已默认
`engine/assets/encoding`，失败即退出。

---

## 五、验证链路

```
tools/build_ui.py     PAK(Update 覆盖 Sango3) → #include 展开 → Big5 解码 → .workbuddy/data/json/ui.json
build/pc/bin/sango3ui.exe  同一份 PAK → 同语义解析 → build/pc/out/ui_dump.tsv
tools/verify_ui_c.py  逐字段比对（11530 项）
```

```bash
python tools/build_ui.py        # 生成/刷新 Python 基准
python tools/build_pc.py        # 构建
python tools/verify_ui_c.py     # 对照
```

`ui_dump.tsv` 的列序在 `ui_probe.c` 的 `#W/#I/#C` 头注释与
`verify_ui_c.py` 的常量**两侧锁定**；任何一侧增减字段而不同步会静默错位
（M1-a 踩过这个坑，见 `MEMORY.md`）。

值的转义：制表符等控制字符在 TSV 里用 C 风格转义（`\t` `\n` `\r` `\\` `\xNN`），
两侧同一套函数，**不丢信息**（原版数据里真的有制表符）。

---

## 六、下一步（M2 剩余）

- [ ] **控件树运行时**：`s3_ui_window/child` 已可遍历，需要一个运行时
      `S3UiNode` 实例层（坐标合成：子控件相对父控件偏移还是绝对坐标，需实测确认）
- [ ] **图元级渲染**：把 `ICON.Dir + 四态素材名` 解析成 SHP 路径并解码
      （`Dir = \MM\Button\` + `Normal = NewGame` → `Shape\MM\Button\NewGame.shp`）
- [ ] 主菜单场景：先跑通 root ID=1 的 6 个子按钮（開始遊戲/讀取進度/登錄武將/設定選項/遊戲結束）
- [ ] 文本绘制：`wsText` 控件 + `Font/FColor` → 走 M1-b 的字体层
