# Sticky Todo（源码）

轻量、离线、纯本地的 Windows 桌面便签式待办工具。使用 C++17 + Qt 6 Widgets 编写，无 QML、无网络模块。

交付给最终用户的运行说明、卸载脚本和绿色程序在 `dist/README.md` 与 `dist/` 目录。

## 目录

```text
src/               C++/Qt 源码
resources/         app.ico、app.png、qrc、rc、manifest
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

日常带托盘请用 `dist/Start StickyTodo.cmd`（工程目录带 Low integrity 标签时，直接跑 `dist\StickyTodo.exe` 会降到低完整性，托盘会被系统拒绝）。

`Start StickyTodo.cmd` 会启动 `%LOCALAPPDATA%\StickyTodo\runtime\StickyTodo.exe`（中等完整性副本），并传入：

```text
--data-dir <本包目录>\config
```

数据目录解析顺序：

1. 命令行 `--data-dir`（由启动脚本写入环境变量 `STICKY_TODO_DATA_DIR`）
2. 否则使用 **当前 exe 同级** 的 `config`（便携默认）

因此不要混用多种启动方式却假设只有一份数据：直接跑 LocalAppData 里的 runtime、直接跑 `dist\StickyTodo.exe`、以及 `Start StickyTodo.cmd`，在未显式传入同一 `--data-dir` 时可能落到不同目录。开机自启项会写入「当前会话解析到的数据目录」；**不会**把位于 `%TEMP%` 的测试目录写进启动项。

隔离测试仍可用：

```powershell
$env:STICKY_TODO_DATA_DIR = "$PWD\testdata"
.\dist\StickyTodo.exe
```

`STICKY_TODO_DATA_DIR` / `--data-dir` 只用于测试隔离或启动脚本定点；日常请固定走 `Start StickyTodo.cmd`，设置页可打开当前实际数据目录确认路径。

## 实现要点

- 无边框透明窗口 + 自绘纸质卡片，单 exe 加 Qt 运行库，无后台服务。
- 时间分组、搜索、内联编辑、应用内日历、删除撤销。
- 右侧贴边收起：34×232 竖向标签、悬停展开、点击固定、拖离恢复悬浮。
- 命名 Mutex 单实例、广播消息恢复已有窗口。
- HKCU Run 开机自启、系统托盘、明确的最小化/隐藏/退出语义。
- JSON + QSaveFile 原子写入、`.bak` 备份、损坏文件保留。
- 便携数据目录：程序同级 `config`，不写固定 C 盘路径。
- 6 个 Q_OBJECT 头文件必须全部 moc，否则会出现 `undefined reference to ...` 链接错误。
