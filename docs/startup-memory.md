# 启动内存：代码假设与 MxrPc 口径（未动刀）

本文是调查记录，**不是**发版说明。不改功能、不改版本号。Windows 数字由劳模在 **MxrPc** 按下方口径实测后回填；在数字回来之前禁止凭假设砍设置页/汇报页/番茄/提醒。

## 结论栏（劳模回填；缺一不可）

按小皮 gate，最终结论必须同时写明这四项。当前仓库侧 **没有** Windows 实测值，下表先空着。

| 必填项 | 当前 tip（待测） | 对照 v0.1.5（待测） |
| --- | --- | --- |
| (1) 测的 tip / SHA | `dev-cursor` `baed1fd49341c4be798c214ae505dac5a3ec723b`（调查时 HEAD；若你测的是别的构建请改这里） | annotated tag `v0.1.5` → commit `e8b819e92f74a099c47358cf257fd0a5277c658a` |
| (2) 任务管理器列 | **待填**：进程页「内存」/ 详细信息「工作集」/「内存(专用工作集)」/「提交大小」 | **必须同一列** |
| (3) 启动后空闲多久再读 | **待填**（建议 15s，见脚本默认） | **必须同一空闲时长** |
| (4) 读数 | **待填（禁止用云端编造）** | **待填（禁止用云端编造）** |

辅助字段（建议一并记下，避免测错进程）：

- 进程映像路径（`StickyTodo.exe` 的 Path，不要测到 `cmd.exe` / runtime 副本混用）
- 启动方式：直接 exe + `--data-dir`，或 `Start StickyTodo.cmd`（会跑 `%LOCALAPPDATA%\StickyTodo\runtime\`）
- 数据：空目录 vs User 真实 `config`
- 构建：Release `-O2 -DNDEBUG`（`build.py`）还是调试器
- 完整性：Medium（`Start StickyTodo.cmd`）还是 Low（横幅多一块 widget）

本云端环境：**无 Qt、无 Win GUI**，不能代替 MxrPc。未在 Linux 上编包测 RSS（本机没有 Qt prefix）。未改 DataStore，未跑 `python tests/test_datastore.py`（该脚本也要本机 `STICKY_QT_DIR`）。

## 用户现象（调查输入，不是实测）

User（Asia/Shanghai 2026-11-02）：以前启动大约 **2–8 MB**，现在几乎总是 **10 MB+**。当前船头曾是 v0.1.5；`master`/`dev-cursor` 另含 #60 置顶 + 文档。

## 先分清「相对谁变高」

代码上要把两段增量拆开，否则会误砍 #60。

| 对照 | 源码差 | 启动期额外常驻对象（代码能看到的） |
| --- | --- | --- |
| **HEAD vs v0.1.5** | #60 置顶 + 文档；`src` 约 +78 行 | **几乎没有。** 置顶只在右键菜单（用时才 `new QMenu`）和列表绘制里加一条 paint 分支；空闲主窗口不因 #60 多出一个 hidden page / timer / 托盘图 |
| **v0.1.5 vs v0.1.0** | 约 +2800 行 `src` | **有。** 设置页多了提醒卡 + 番茄卡（`QSpinBox`/`QTimeEdit`）；每条待办多一个 `PomodoroChip`；v0.1.1 起 `Theme::applyApplicationChrome()` |

若劳模测得 **HEAD ≈ v0.1.5（同一列、同一空闲）**，则 User 的「比以前高」应对 v0.1.0/v0.1.1 解释，**不要**在 #60 上动刀。

若 **HEAD 明显高于 v0.1.5**，#60 仍很难单独解释数 MB 基线（见 H5）；那时优先查：测错 exe、数据目录里待办变多、列混用、Low IL、debug 构建。

## 启动时实际构造了什么

`main.cpp`：Fusion、`Theme::uiFont(13)`（会走字体族枚举）、`Theme::applyApplicationChrome()`（v0.1.1+）、窗口图标、立刻 `MainWindow` + `show()`。

无 QWebEngine、无 QML、无 SVG 控件。Windows `build.py` 只部署 `Qt6Core/Gui/Widgets` + `qwindows.dll`，没有 `imageformats` 插件（qrc 里的 PNG 走 Qt 内置）。

`MainWindow::buildUi()` 在启动时 **一次性** `new`：

- 主列表 `TodoScrollView`
- **整页** `SettingsPage`（构造函数里 `buildUi()` + `refresh()`，含提醒/番茄卡片）
- **三份** `DailyReportPage`（日/周/月，各带一个空的 `QListWidget`；`refresh()` 要等打开 tab 才跑，但控件树已在）
- 侧栏、托盘 `QMenu`（不显示也已创建）、搜索框、撰写条等

`QStackedWidget` 的隐藏页 **不会销毁**，只是不画。设置页上的 `QTimeEdit`/`QSpinBox`/`QScrollArea` 和三份 `QListWidget` 在点设置/汇报之前就已经在进程里。

`SettingsPage` 卡片（相对 v0.1.0 多出来的）：「定时提醒」+「番茄钟」。`SettingRow` 从 19 个增到 26 个；每个 row 还给 title/subtitle `QLabel` 设独立 `setStyleSheet`。

每条 `TodoRow`（v0.1.2+）在 ctor 里就 `new PomodoroChip`，只是多数时候 `setVisible(false)`。有 N 条待办就有 N 个额外 `QAbstractButton`。空列表走 `EmptyStateWidget`，不建 row。

番茄 tick 是懒的（`ensurePomodoroTick`，开钟才有 1s 定时器）。提醒 tick 启动就有：5s 一次，对象很小。

`CalendarPopup` 用时才 `new`，不进启动基线。

## 排序假设（代码证据；不是 Win 读数）

金额级判断全是 **量级猜测**，等 MxrPc 同一口径才能定。

### H1 — 口径/列/进程认错（优先排除）

2–8 MB 作为 **Qt 6.11 Widgets + Fusion + 中文字体** 的「工作集」偏低：三个 Qt DLL + `qwindows.dll` 映射通常就把工作集抬到十几 MB 量级（这是常识，不是这次测到的数）。更常见的是：

- Win11 **进程**页「内存」= Active Private Working Set，**不是**详细信息「工作集」
- `Private Bytes`（`PrivateMemorySize64`）≠ 「专用工作集」
- 测到了 `Start StickyTodo.cmd` 的 `cmd.exe`，或 `LocalAppData\StickyTodo\runtime` 里一份旧 exe
- 启动后立刻读（托盘还在 400ms 重试，字体/样式还在 polish）

**怎么证伪：** 劳模脚本同时打出映像路径和多列；User 说的「10MB+」必须能对上其中 **一列**，v0.1.5 用同一列。

### H2 — 设置页启动即建（相对 v0.1.0 最像「多出来的控件树」）

`buildUi()` 无条件 `new SettingsPage`。v0.1.2 起多提醒卡（`QSpinBox` + **两个 `QTimeEdit`** + `QLineEdit`），v0.1.3 起多番茄卡（又一个 `QSpinBox`、统计行）。`QTimeEdit`/`QSpinBox` 内部还有 line edit 和上下按钮。再叠加每行 `setStyleSheet`（Qt 会给该控件单独做 stylesheet 包装，比调 palette 更肥）。

这能解释 **v0.1.0 → v0.1.2/v0.1.3** 的基线上升，**不能**解释 HEAD vs v0.1.5（设置页在 v0.1.5 已是现在这套）。

### H3 — 三份汇报 `QListWidget` 启动即建（基线有，但不是 v0.1.0 之后才有）

v0.1.0 起就有日/周/月三页。空列表也有 view/model/滚动条。对「比 v0.1.0 高」贡献小；对「比 v0.1.5 高」无贡献。

### H4 — 每条待办一个 `PomodoroChip`（数据相关，不是空列表基线）

v0.1.2 起。空数据几乎为 0；待办多时线性涨。测 User 真数据前必须先测 **空 `--data-dir`**，否则会把「列表变长」当成「版本变肥」。

### H5 — #60 置顶（HEAD vs v0.1.5）

`handlePinToggle`、分组、`paintEvent` 多一个背景色、右键多一项。空闲启动路径不 new 新 window / 不加载新资源。**单独不足以解释 2–8 → 10+。** 除非实测 HEAD 明显高于 v0.1.5，再查是否测错包。

### H6 — v0.1.1 起的 application chrome

`Theme::applyApplicationChrome()`：Light `colorScheme`、纸色 `QPalette`、应用级 stylesheet、`PopupChromeFilter`。菜单/tooltip 的圆角 `QImage` 遮罩只在弹出时分配，**空闲不应常驻**（#64 修的是用过番茄后的 tooltip，不是启动 RSS）。stylesheet 引擎本身会占一点缓存，相对 v0.1.0 有、相对 v0.1.5 无差（HEAD 的 `theme.cpp` 与 v0.1.5 同行数）。

### H7 — 字体

`Theme::uiFamily()` 第一次调用 `QFontDatabase::families()` 再线性比对 YaHei/PingFang/Noto/Segoe。v0.1.0 就有。中文 UI 会把微软雅黑等栅格进工作集，**机器相关**，不是 tag 差。可能把「工作集」抬高，但解释不了「同一台机器以前 2–8、现在 10+」，除非以前其实看的是别的列。

### H8 — 图标

`app.png` 自导入起就是 512×512 RGBA（文件 7558 字节；解码后约 1 MiB 位图）。`QApplication::setWindowIcon` 与 `MainWindow::setWindowIcon` 各 load 一次同一 qrc。托盘优先 `LoadImage` exe 里的 ico，失败才走 `QImage(":/icons/app.png")`。体积不足以单独解释数 MB 级 **增量**（各 tag 的 png 一样大）。

### H9 — 数据文件

启动 `loadTodos/Settings/Reminders/PomodoroStats` + 三份汇报 JSON。空目录可忽略。User 若积了大量归档/汇报，只影响真数据工况，所以空目录对照是硬条件。

### 已排除或降级

- QWebEngine / QML：源码没有。
- 调试堆：`build.py` 是 `-O2 -DNDEBUG`。若 User 用 IDE Debug，那是另一条线，和 tag 无关。
- 提醒 5s `QTimer`、DataStore 的 save timer：对象很小。
- `PaperCard` 180 个点：可忽略。

## 若数字回来之后，最小安全刀（现在不动）

只在「空目录、同一列、同一空闲」下 **v0.1.5 已高于旧记忆，且 HEAD≈v0.1.5** 时，才值得谈产品取舍：

1. **延迟创建** `SettingsPage` 和三份 `DailyReportPage`（第一次打开再 `new`）。不删功能，只推迟。风险：第一次进设置/汇报多一帧；`MainWindow` 里大量 `m_settingsPage->` 要改成可空。
2. `SettingRow` 少用 per-widget `setStyleSheet`，改 palette / 父级 QSS。视觉回归风险，要手测。
3. `QFontDatabase::hasFamily` 代替全量 `families()`。v0.1.0 就有，可能只省枚举，不省雅黑栅格。
4. 窗口图标用更小的 pixmap，而不是把 512² PNG 解进 `QIcon`。省的是位图，不是功能。

**不要做：** 去掉番茄芯片、去掉提醒 UI、合并汇报页、为了 RSS 关掉 Fusion/纸色菜单。那是砍功能，小皮已否。

若 **HEAD 明显高于 v0.1.5**：先不要改产品代码，先核对 exe 路径和数据目录。

## MxrPc 怎么测（劳模）

脚本：`docs/measure-startup-memory.ps1`。

1. 退出所有 `StickyTodo.exe`（有单实例锁）。
2. **空数据、同一空闲、两次：**
   - A：GitHub Release **v0.1.5** zip 里的 `StickyTodo.exe`（SHA `e8b819e`）
   - B：当前要测的 tip 用同一套 Qt 6.11.1 MinGW `build.py` 打出来的 exe（调查时 SHA `baed1fd`）
3. 不要用混了 PATH 的「半套 DLL」。记录 `image_path`。
4. 建议先 **不要**走 `Start StickyTodo.cmd`（避免测到旧 runtime）。确认口径后再加一条 cmd 启动。
5. 默认空闲 **15 秒**，鼠标不要放在窗口上（避免 tooltip #64 路径）。不要打开设置/汇报/右键菜单。
6. 把脚本打印的 **工作集 / 专用工作集 / Private Bytes / 提交大小** 全部贴回；并写明 User 当时看的是哪一列。
7. 可选第二轮：同一 exe + User 真实 `config`，看 H4/H9。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File docs\measure-startup-memory.ps1 `
  -Exe C:\path\to\v0.1.5\StickyTodo.exe `
  -Label v0.1.5 -GitSha e8b819e92f74a099c47358cf257fd0a5277c658a -IdleSeconds 15

powershell -NoProfile -ExecutionPolicy Bypass -File docs\measure-startup-memory.ps1 `
  -Exe C:\path\to\current\StickyTodo.exe `
  -Label HEAD -GitSha baed1fd49341c4be798c214ae505dac5a3ec723b -IdleSeconds 15
```

## 本次仓库改动范围

- 本文件：假设与口径。
- `docs/measure-startup-memory.ps1`：MxrPc 取样。
- **不**改 `src/`。
