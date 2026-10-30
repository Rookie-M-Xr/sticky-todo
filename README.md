# Sticky Todo（源码）

轻量、离线、纯本地的 Windows 桌面便签式待办工具。使用 C++17 + Qt 6 Widgets 编写，无 QML、无网络模块。

交付给最终用户的运行说明、卸载脚本和绿色程序在 `dist/README.md` 与 `dist/` 目录。

## 目录

```text
src/               C++/Qt 源码
resources/         app.ico、app.png、qrc、rc、manifest
docs/              发布约定等文档
build.py           手工 moc/rcc/windres/g++ 构建与部署脚本
test_datastore.py  无 GUI DataStore 单测（JSON 往返 / .bak 恢复）
tests/             DataStore 测试源码
build/             中间产物（可删除）
dist/              绿色发布目录
```

## 构建环境

- Qt 6.11.1 MinGW 64 位（`mingw_64`）
- MinGW-w64 GCC 14.2.0
- Python 3
- 没有使用 qmake 或 CMake；`build.py` 直接调用 `moc`、`rcc`、`windres` 和 `g++`

构建前必须设置环境变量（未设置则 `build.py` 失败退出）：

```powershell
$env:STICKY_QT_DIR = 'C:\Qt\6.11.1\mingw_64'
$env:STICKY_MINGW_DIR = 'C:\mingw64'
```

路径按本机 Qt / MinGW 安装位置修改；仓库不写死任何本机绝对路径。

## 构建

```powershell
python .\build.py
```

产物：`dist/StickyTodo.exe`。脚本会编译 11 个源文件、为 6 个含 `Q_OBJECT` 的头文件生成 moc、编译 qrc/rc，并复制 Qt DLL、MinGW 运行库和 `platforms/qwindows.dll`。

## 无 GUI 测试（DataStore）

本地一键跑 DataStore JSON 往返与损坏恢复（从 `.bak` 恢复），不启动窗口：

```powershell
python .\test_datastore.py
```

成功退出码为 0（stdout 含 `ALL GREEN`）；失败为非 0。临时数据写在 `testdata/`（已 gitignore）。

## 运行调试

### 推荐启动（带托盘）

日常请用 `dist/Start StickyTodo.cmd`，**不要**把直接双击 `dist\StickyTodo.exe` 当作可自愈到中等完整性的路径。

原因：工程/`dist` 目录若带 Low integrity 或「来自 Internet」区域标记，直接跑 exe 会继承**低完整性**；Windows 常拒绝托盘。Low 进程无法无 UAC 地抬到 Medium（已实测 WMI / 任务计划 / Explorer Shell 均失败）。程序会**降级继续运行**，并显示横幅，请改用 `Start StickyTodo.cmd`。

`Start StickyTodo.cmd` 会启动 `%LOCALAPPDATA%\StickyTodo\runtime\StickyTodo.exe`（中等完整性副本），并传入：

```text
--data-dir <本包目录>\config
```

请解压到有写权限的目录（勿装到 Program Files 等无写位置）；若便携 `config` 不可写，程序会回退到 `%LOCALAPPDATA%\StickyTodo\data` 并提示新旧路径。

### 数据目录

解析顺序：

1. 命令行 `--data-dir` / 环境变量 `STICKY_TODO_DATA_DIR`（由启动脚本写入）
2. 否则使用 **当前 exe 同级** 的 `config`（便携默认）；不可写时尝试去掉目录只读属性，仍失败则回退 `%LOCALAPPDATA%\StickyTodo\data`（可迁移已有 JSON）

因此不要混用多种启动方式却假设只有一份数据：直接跑 LocalAppData 里的 runtime、直接跑 `dist\StickyTodo.exe`、以及 `Start StickyTodo.cmd`，在未显式传入同一 `--data-dir` 时可能落到不同目录。开机自启项会写入「当前会话解析到的数据目录」；**不会**把位于 `%TEMP%` 的测试目录写进启动项。

隔离测试仍可用：

```powershell
$env:STICKY_TODO_DATA_DIR = "$PWD\testdata"
.\dist\StickyTodo.exe
```

`STICKY_TODO_DATA_DIR` / `--data-dir` 只用于测试隔离或启动脚本定点；日常请固定走 `Start StickyTodo.cmd`，设置页可打开当前实际数据目录确认路径。

无托盘时点 `×` / `—` 会保存并退出（TR #14）。低完整性直开 exe 时界面会提示改用启动脚本，关闭行为同上。

## 多屏说明（TR #7）

主路径（`9fd469a` 起）：拖动用各屏 `availableGeometry` 并集跨屏；松手按窗口中心所在屏贴边，并持久化该屏名。

已知边界：屏间物理空隙、或分辨率/DPI 差异导致逻辑矩形高度不等时，并集会出现“假空隙”。当前会把窗口中心拉回**最近屏**的可用区，避免松手飞回主屏。若布局极端（大缝、重叠、旋转竖屏组合）仍异常，请在 Issue 附上分辨率+DPI+相对位置后再议；不保证覆盖所有混 DPI 奇形布局。

## 全局快捷键（TR #9）

设置页「全局快捷键」可录制/清除组合键（如 `Ctrl+Alt+T`）：失焦时仍可显示/聚焦或隐藏便签。

- **Windows**：`RegisterHotKey`；有托盘时隐藏进托盘，与托盘「隐藏」一致；关闭/退出路径不变。
- **Linux X11（xcb）**：尽力 `XGrabKey`（运行时 `dlopen libX11`）；无托盘 stub 时热键隐藏只 `hide()`（不退出），再按同一热键唤回。
- **Linux Wayland**：系统级全局热键受限，注册会失败并提示；不额外申请门户权限以保持轻量。
- 单实例：热键由已运行实例处理；第二次启动仍只激活已有窗口，不新开进程。

## 提交约定

采用 Conventional Commits：类型前缀 + **中文**说明，禁止无前缀裸提交。

| 前缀 | 含义 |
|------|------|
| `feat` | 新功能 |
| `fix` | 缺陷修复 |
| `chore` | 构建/工具/杂项（不改产品行为） |
| `docs` | 文档 |
| `test` | 测试 |
| `refactor` | 重构（无行为变化） |

有对应 TR（GitHub Issue）时在说明里关联，便于跟踪。能直接关单的可用 GitHub 关键字 `Fixes #n`（英文，便于自动关闭）。

示例：

- `fix: 修复多屏贴边回跳（TR #7）`
- `feat: 日报快照与一键复制（TR #2）`
- `fix: 修复多屏贴边回跳（TR #7）`（正文另起一行写 `Fixes #7` 可自动关单）
- `docs: README 去掉本机绝对路径`
- `chore: 构建脚本改为仅使用环境变量`

## Issue 标签

开源协作用 GitHub Issue。标签分三类，可并存；**优先级只挂一个 P**：

| 类 | Labels | 说明 |
|----|--------|------|
| 类型 | `bug` / `enhancement` / `chore` / `documentation` | 问题性质 |
| 状态 | `doing` → `finish` → `test` | 流转；关单后去掉状态 label |
| 优先级 | `P0` … `P8`（只挂一个） | `P0` 最高；调序只改 label，标题不写 Px |

标题写清楚现象即可。外来 Issue 由维护者按内容挂 P。

## 发布

正式发版约定见 [`docs/RELEASE.md`](docs/RELEASE.md)（SemVer、`vX.Y.Z` tag 须推远程、CHANGELOG、GitHub Release、Win zip / Linux tar 文件名带版本）。

- 小皮：定版本与范围并拍板。
- 劳模：对版本、tag、双端包、Release。
- 小测：对发布资产解压冒烟，频道报 PASS/FAIL。

用户包说明仍以 `dist/README.md` 为准。

## 实现要点

- 无边框透明窗口 + 自绘纸质卡片，单 exe 加 Qt 运行库，无后台服务。
- 时间分组、搜索、内联编辑、应用内日历、删除撤销。
- 右侧贴边收起：34×232 竖向标签、悬停展开、点击固定、拖离恢复悬浮。
- 命名 Mutex 单实例、广播消息恢复已有窗口。
- HKCU Run 开机自启、系统托盘、明确的最小化/隐藏/退出语义。
- JSON + QSaveFile 原子写入、`.bak` 备份、损坏文件保留。
- 便携数据目录：优先程序同级 `config`；不可写时回退 `%LOCALAPPDATA%\StickyTodo\data`（TR #16）。
- 6 个 Q_OBJECT 头文件必须全部 moc，否则会出现 `undefined reference to ...` 链接错误。

## Linux 可搬运包（TR #5）

在 Linux x86_64 上用 aqt / 官方 Qt 的 `gcc_64` 前缀编一份可解压即跑的包（自带 Qt 运行库，不依赖编包机绝对路径）：

```bash
export STICKY_QT_DIR=/path/to/Qt/6.x.y/gcc_64
./build_linux.sh
# 可选：STICKY_LINUX_TAR=1 ./build_linux.sh   # 额外打 StickyTodo-linux-x86_64.tar.gz
```

默认产物目录：`../sticky-todo-linux/`（可用 `STICKY_LINUX_OUT` 改）。运行见该目录内 `README.md` / `./run.sh`。

Linux 使用 `src/winutils_linux.cpp` 代替 Windows 的 `winutils.cpp`（托盘/开机自启为 stub）。`src/mainwindow.cpp` 在非 Windows 下跳过 Win32 头与 `nativeEventFilter` 实现。

