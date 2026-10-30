# 版本发布约定

正式版必须有 annotated tag + GitHub Release + 带版本号的双端资产。细节以本文件为准；频道只做拍板与 `[release]` 通知。

## 版本号（SemVer）

- 形式：`MAJOR.MINOR.PATCH`，与 `resources/app.rc` 的 `FILEVERSION` / `PRODUCTVERSION` 对齐（第四段固定 `0`，如 `1,0,0,0`）。
- **PATCH**：仅缺陷修复。
- **MINOR**：向后兼容的功能。
- **MAJOR**：数据格式/迁移或破坏性 UX。
- Git tag：`vX.Y.Z`（annotated），打在 `master` 待发 tip；**必须 push 到远程**，禁止只打本地 tag。

## 何时可发

- 本版范围内 TR：无 `doing` / `test`；验收过的已关。
- `python test_datastore.py` 绿。
- 小皮在频道声明「发 vX.Y.Z，范围 …」后再切。

## 劳模发布步骤（顺序）

1. **对版本号**：改 `resources/app.rc`（及任何 UI/关于页若有）、`dist/README.md` 标题版本；同步修正过时说明。
2. **写说明**：`CHANGELOG.md` 增一节 `## vX.Y.Z`（日期 Asia/Shanghai + 本版关闭的 TR 列表 + 已知限制一行）。
3. **提交**：`chore: 发布 vX.Y.Z`（`Refs` 发布管理 TR）；推 `master`。
4. **打 tag**：`git tag -a vX.Y.Z -m "Sticky Todo vX.Y.Z"` → **`git push origin vX.Y.Z`**（远程必须有该 tag）。
5. **出包**（文件名必须带版本）：
   - Win：`StickyTodo-Windows-x86_64-vX.Y.Z.zip`（`dist/` 可运行集：exe、Qt/MinGW DLL、`platforms/`、`Start StickyTodo.cmd`、`unblock.cmd`、`uninstall.cmd`、`dist/README.md`；勿把本机绝对路径写进包内）。
   - Linux：`StickyTodo-linux-x86_64-vX.Y.Z.tar.gz`（`STICKY_LINUX_TAR=1 ./build_linux.sh`）。
6. **GitHub Release**：创建 `vX.Y.Z` Release，正文用 CHANGELOG 该节；上传上述两个资产。
7. **频道**：`[release] vX.Y.Z` + tip SHA + Release 链接；@小测 对 zip/tar **解压冒烟**（不必回归全部历史 TR）。

## 角色

| 角色 | 职责 |
|------|------|
| 小皮 | 定版本号与范围；发版拍板；可拒发 |
| 劳模 | 对版本、CHANGELOG、tag（含远程）、双端包、GitHub Release |
| 小测 | 对发布资产做解压冒烟，频道 PASS/FAIL |
| 小开 | 薄文档（本约定 / README 节）diff，交劳模审推 |

## 禁止

- 无远程 tag 的「口头版本」。
- 用临时目录 / 本机绝对路径打进用户包。
- 未过 DataStore 单测就发。
- 跳过 GitHub Release 只丢私聊附件当正式版。
