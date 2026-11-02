# 版本发布约定

正式版必须有 annotated tag + GitHub Release + 带版本号的双端资产。细节以本文件为准；频道只做拍板与 `[release]` 通知。

## 版本号（SemVer）

- 形式：`MAJOR.MINOR.PATCH`，与 `resources/app.rc` 的 `FILEVERSION` / `PRODUCTVERSION` 对齐（第四段固定 `0`，如 `1,0,0,0`）。
- **PATCH**：仅缺陷修复。
- **MINOR**：向后兼容的功能。
- **MAJOR**：数据格式/迁移或破坏性 UX。
- Git tag：`vX.Y.Z`（annotated），打在 `master` 待发 tip；**必须 push 到远程**，禁止只打本地 tag。

## 发版节奏门槛

正常情况**攒够再发**，不按「一刀一发」切版本。MINOR / PATCH **怎么选号**仍按上方 SemVer；本门槛管的是**何时才发**。

- **小版本**（MINOR 或 PATCH）：自上一正式版起，累计约 **超过 10 点修改/优化**，或累计约 **超过 5 点新功能**，才发一次。
- **大版本**（MAJOR）：架构 / 技术变更，或大功能更新，才发。
- **热修例外**：严重缺陷仍可单独发 PATCH，不凑点数。
- **禁止**「改一点发一版」（一刀一发）。

点数由小皮拍板（约数即可，不搞精确计分）。未达门槛且非热修，频道可拒发。

## 何时可发

- 达上方「发版节奏门槛」，或属严重缺陷热修。
- 本版范围内 TR：无 `doing` / `test`；验收过的已关。
- `python test_datastore.py` 绿。
- 小皮在频道声明「发 vX.Y.Z，范围 …」后再切。

## 劳模发布步骤（顺序）

1. **对版本号**：改 `resources/app.rc`（及任何 UI/关于页若有）、`dist/README.md` 标题版本；同步修正过时说明。
2. **写说明**：
   - `CHANGELOG.md` 增一节 `## vX.Y.Z`（日期 Asia/Shanghai + 本版关闭的 TR 列表 + 已知限制一行）。CHANGELOG **可以**偏工程（TR、tip SHA、内部缺口）。
   - GitHub Release 正文**另写**，按下方「Release 正文（GitHub，中文用户向）」；**禁止**把 CHANGELOG 该节原样粘进 Release。
3. **提交**：`chore: 发布 vX.Y.Z`（`Refs` 发布管理 TR）；推 `master`。
4. **打 tag**：`git tag -a vX.Y.Z -m "Sticky Todo vX.Y.Z"` → **`git push origin vX.Y.Z`**（远程必须有该 tag）。
5. **出包**（文件名必须带版本）：
   - Win：`StickyTodo-Windows-x86_64-vX.Y.Z.zip`（`dist/` 可运行集：exe、Qt/MinGW DLL、`platforms/`、`Start StickyTodo.cmd`、`unblock.cmd`、`uninstall.cmd`、`dist/README.md`；勿把本机绝对路径写进包内）。
   - Linux：`StickyTodo-linux-x86_64-vX.Y.Z.tar.gz`（`STICKY_LINUX_TAR=1 ./build_linux.sh`）。
6. **GitHub Release**：创建 `vX.Y.Z` Release，正文用「Release 正文（GitHub，中文用户向）」；上传上述两个资产。频道拍板**可拒发**只有 TR 列表的正文。
7. **频道**：`[release] vX.Y.Z` + tip SHA + Release 链接；@小测 对 zip/tar **解压冒烟**（不必回归全部历史 TR）。`[release]` / tip SHA / 小测 只出现在频道，不写进 Release 开篇。

### 发版前 Win 包检查（TR #17）

- zip 内关键 `StickyTodo.exe` / DLL **无** Zone.Identifier（`Get-Item .\StickyTodo.exe -Stream Zone.Identifier` 应报不存在；或打包机对产物 `Unblock-File` 后再打 zip）。
- 包内含可用的 `unblock.cmd`。
- `dist/README` SmartScreen 节与上一致（含「更多信息 → 仍要运行」）。

## Release 正文（GitHub，中文用户向）

GitHub Release 给**下载用户**看，不是给频道对账。结构参考 [Cap 0.6.2](https://github.com/CapSoftware/Cap/releases/tag/cap-v0.6.2)：开篇摘要 + 分节、**加粗短标题** + 一行说明；**全文中文**，不照搬英文、不堆 TR。`CHANGELOG.md` 继续偏工程即可。

下版起正文必须过本模板。频道可拒发不合格正文。

### 必遵骨架

顺序固定；**空节省略**（本版没有新功能就不要留空的 `## 新功能`）。

````markdown
开篇 1–3 句：本版给用户带来什么（中文，无 tip / TR 堆砌）

## 新功能

- **短标题**：用户能感知的能力 + 怎么用（可整节省略）

## 修复

- **短标题**：修了什么现象（用户语言）

## 已知限制

- 诚实边界（绿色包、会话内番茄、提醒须进程在跑等）

## 下载

- **Windows**：`StickyTodo-Windows-x86_64-vX.Y.Z.zip`
- **Linux**：`StickyTodo-linux-x86_64-vX.Y.Z.tar.gz`
````

写法：

- 必须全文中文。
- 条目形态与 Cap 同构：`**短标题**：一行用户语言`（用户能看懂在用什么、修了什么）。
- 工程 tip / TR 号 / SHA **不进开篇**；可放折叠「工程备注」，或只写在 CHANGELOG：

````markdown
<details><summary>工程备注</summary>

对应 tip / TR 见 CHANGELOG；Git tag `vX.Y.Z`。

</details>
````

### 反例（禁止当 Release 正文）

只贴 TR / `#` 清单：

```markdown
- #63
- TR #52 PASS
- 范围：已关 TR #40 #41 #50
```

tip SHA 墙：

```markdown
本版 tip e8b819e / 856c2a9 / 8fed4a7，annotated tag 已 push。
```

开篇夹带内部行话（`[release]`、小测、doing/finish、小开）：

```markdown
[release] v0.1.5 请小测解压冒烟。CHANGELOG 见仓库。doing→finish。
```

把 CHANGELOG「本版纳入」原样粘贴同样不合格，例如 `#63 tooltip…`、`#28`、Win zip 槽位由谁补传等。

### 填写示例（v0.1.5）

本版无新功能，故省略 `## 新功能`。

````markdown
Sticky Todo **v0.1.5** 修复了番茄钟悬停后其它按钮提示变成黑底灰字的问题，并让提示圆角与纸色界面一致。本版取代已下架的不完整 v0.1.4 包，请改用本页双端下载。

## 修复

- **提示样式污染**：先看番茄钟提示、再移到删除/日期等按钮时，提示会偶发黑底灰字。现已始终保持纸色可读。
- **提示圆角**：番茄钟与删除等按钮的提示边框贴合圆角，不再露方角框。

## 已知限制

- 仍为绿色包分发，不做安装器、不代码签名；Windows 请用包内 `Start StickyTodo.cmd` 启动。
- 番茄钟仅当前进程有效，重启后进行中/暂停中的钟不会恢复。
- 定时提醒只在软件运行时到点；延后提醒不写入磁盘。
- Linux Wayland 下全局热键可能不可用。

## 下载

- **Windows**：`StickyTodo-Windows-x86_64-v0.1.5.zip`
- **Linux**：`StickyTodo-linux-x86_64-v0.1.5.tar.gz`

<details><summary>工程备注</summary>

对应 tip 含 #63；CHANGELOG / tag 见仓库。

</details>
````

## 角色

| 角色 | 职责 |
|------|------|
| 小皮 | 定版本号与范围、发版节奏（点数约数）；发版拍板；可拒发（含拒发「只有 TR 列表」的正文、未达节奏门槛且非热修） |
| 劳模 | 对版本、CHANGELOG、**用户向 Release 正文**、tag（含远程）、双端包、GitHub Release |
| 小测 | 对发布资产做解压冒烟，频道 PASS/FAIL |
| 小开 | 薄文档（本约定 / README 节）diff，交劳模审推 |

## 禁止

- 无远程 tag 的「口头版本」。
- 用临时目录 / 本机绝对路径打进用户包。
- 未过 DataStore 单测就发。
- 跳过 GitHub Release 只丢私聊附件当正式版。
- 「改一点发一版」/ 一刀一发（未达发版节奏门槛且非严重缺陷热修）。
- 把 CHANGELOG / TR 清单 / tip SHA 墙原样贴成 GitHub Release 正文。
- Release 开篇写 `[release]`、小测、doing/finish 等内部行话。
