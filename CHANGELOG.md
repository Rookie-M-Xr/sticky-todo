# Changelog

## v0.1.1

- **日期**：2026-10-30（Asia/Shanghai）
- **Tip**：见 Git tag `v0.1.1`（annotated）指向的 commit
- **范围**：已关 TR #15 #16 #17 #22 #23（PATCH；不含 #18/#19）

### 本版纳入

- #15 低完整性：文档推荐 `Start StickyTodo.cmd`；Low 横幅/可退出，不静默抬权
- #16 便携 `config` 只读时回退 `%LOCALAPPDATA%\StickyTodo\data`（可迁移已有 JSON）
- #17 SmartScreen / 解除锁定：发布包文档与发版检查
- #22 菜单不透明纸色 + 四角圆角遮罩，浅色/深色系统下文字可读
- #23 文案精简：去掉冗长 toast/设置引言；不可逆操作需确认；托盘气泡仅首次

### 已知限制

- Linux Wayland：系统级全局热键受限，注册会失败并提示；不额外申请门户权限。
- Linux 包需宿主提供 xcb（Wayland-only 会话可能需兼容层）；详见 Linux 包内 README。
- 低完整性进程必须使用 `Start StickyTodo.cmd` 启动（不会静默抬到 Medium）。

## v0.1.0

- **日期**：2026-10-30（Asia/Shanghai）
- **Tip**：见 Git tag `v0.1.0`（annotated）指向的 commit
- **范围**：已关 TR #1–#11（首发诚实版本号；仓库此前无正式 tag/Release）

### 本版纳入

- #1 完成项超期自动清理与手动归档
- #2 日报侧栏：今日完成快照 + 可编辑 + 一键复制
- #3 周报/月报 + 用户自选汇报页
- #4 Linux 下需点两次关闭才能退出
- #5 Linux 可搬运发布包（不依赖本机 Qt 路径）
- #6 归档完成项支持再导入主列表
- #7 多屏缝隙与强混 DPI 下贴边修正
- #8 [icebox] 待办粘贴图片（未做产品实现，Issue 已关入冰盒）
- #9 全局快捷键显示/隐藏便签（含 Win 真前台判定）
- #10 有托盘时点 X 隐藏到托盘（退出走托盘「退出」）
- #11 轻量关键字过滤

### 已知限制

- Linux Wayland：系统级全局热键受限，注册会失败并提示；不额外申请门户权限。
- Linux 包需宿主提供 xcb（Wayland-only 会话可能需兼容层）；详见 Linux 包内 README。
