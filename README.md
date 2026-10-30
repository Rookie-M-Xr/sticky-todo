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

默认路径：

```text
D:\cache-QT6\6.11.1\mingw_64
D:\cache-mingw64\x86_64-14.2.0\mingw64
```

如安装位置不同，设置环境变量：

```powershell
$env:STICKY_QT_DIR = 'C:\Qt\6.11.1\mingw_64'
$env:STICKY_MINGW_DIR = 'C:\mingw64'
```

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

日常带托盘请用 `dist/Start StickyTodo.cmd`（工程目录低完整性时直接跑 exe 可能看不到托盘）。

```powershell
$env:STICKY_TODO_DATA_DIR = "$PWD\testdata"
.\dist\StickyTodo.exe
```

`STICKY_TODO_DATA_DIR` 只用于测试隔离；未设置时使用 exe 同级的 `config` 目录。

## 实现要点

- 无边框透明窗口 + 自绘纸质卡片，单 exe 加 Qt 运行库，无后台服务。
- 时间分组、搜索、内联编辑、应用内日历、删除撤销。
- 右侧贴边收起：34×232 竖向标签、悬停展开、点击固定、拖离恢复悬浮。
- 命名 Mutex 单实例、广播消息恢复已有窗口。
- HKCU Run 开机自启、系统托盘、明确的最小化/隐藏/退出语义。
- JSON + QSaveFile 原子写入、`.bak` 备份、损坏文件保留。
- 便携数据目录：程序同级 `config`，不写固定 C 盘路径。
- 6 个 Q_OBJECT 头文件必须全部 moc，否则会出现 `undefined reference to ...` 链接错误。
