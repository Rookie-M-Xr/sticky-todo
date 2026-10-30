#!/usr/bin/env bash
# Build a portable Linux package for Sticky Todo (Qt 6 Widgets).
# Copies required Qt/ICU libs next to the binary — no absolute aqt path needed at runtime.
#
# Env:
#   STICKY_QT_DIR     Qt prefix (default: /home/box/Qt/6.8.3/gcc_64 if present)
#   STICKY_LINUX_OUT  Output directory (default: <repo>/../sticky-todo-linux)
#   STICKY_LINUX_TAR  If set to 1, also write a .tar.gz next to the out dir
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$ROOT/src"
RES="$ROOT/resources"
BUILD="$ROOT/build-linux"
OUT="${STICKY_LINUX_OUT:-$ROOT/../sticky-todo-linux}"

if [[ -n "${STICKY_QT_DIR:-}" ]]; then
  QT="$STICKY_QT_DIR"
elif [[ -d /home/box/Qt/6.8.3/gcc_64 ]]; then
  QT=/home/box/Qt/6.8.3/gcc_64
else
  echo "error: set STICKY_QT_DIR to your Qt 6 gcc_64 prefix" >&2
  exit 2
fi
if [[ ! -d "$QT" ]]; then
  echo "error: Qt prefix not found: $QT" >&2
  exit 2
fi

MOC="${QT}/libexec/moc"
RCC="${QT}/libexec/rcc"
[[ -x "$MOC" ]] || MOC="${QT}/bin/moc"
[[ -x "$RCC" ]] || RCC="${QT}/bin/rcc"
for tool in "$MOC" "$RCC"; do
  if [[ ! -x "$tool" ]]; then
    echo "error: missing $tool" >&2
    exit 2
  fi
done

CXX="${CXX:-g++}"
command -v "$CXX" >/dev/null || { echo "error: g++ not found" >&2; exit 2; }

# Fresh stage dir (keep config/ if re-running so smoke data can stay)
mkdir -p "$BUILD" "$OUT/lib" "$OUT/plugins/platforms" "$OUT/config"

CXXFLAGS=(
  -std=c++17 -O2 -DNDEBUG -Wall -Wextra -Wno-unused-parameter
  -fPIC
  -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_CORE_LIB
  -DQT_NO_DEBUG
  -I"${QT}/include"
  -I"${QT}/include/QtCore"
  -I"${QT}/include/QtGui"
  -I"${QT}/include/QtWidgets"
  -I"${QT}/mkspecs/linux-g++"
  -I"${SRC}"
)

# $ORIGIN/lib so the binary finds bundled Qt without LD_LIBRARY_PATH
LDFLAGS=(
  -Wl,-rpath,'$ORIGIN/lib'
  -L"${QT}/lib"
  -lQt6Widgets -lQt6Gui -lQt6Core
  -lpthread -ldl
)

SOURCES=(
  main.cpp models.cpp datastore.cpp theme.cpp icons.cpp widgets.cpp
  todoview.cpp calendarpopup.cpp settingspage.cpp dailyreportpage.cpp mainwindow.cpp
)

MOC_HEADERS=(
  datastore.h widgets.h todoview.h calendarpopup.h settingspage.h dailyreportpage.h mainwindow.h
)

echo "== moc =="
generated=()
for header in "${MOC_HEADERS[@]}"; do
  out="$BUILD/moc_${header%.h}.cpp"
  echo ">> $MOC $header"
  "$MOC" "$SRC/$header" -o "$out" -I"$SRC"
  generated+=("$out")
done

echo "== rcc =="
"$RCC" "$RES/resources.qrc" -o "$BUILD/qrc_resources.cpp"
generated+=("$BUILD/qrc_resources.cpp")

echo "== compile sources =="
objects=()
for source in "${SOURCES[@]}"; do
  obj="$BUILD/${source%.cpp}.o"
  echo ">> $CXX -c $source"
  "$CXX" "${CXXFLAGS[@]}" -c "$SRC/$source" -o "$obj"
  objects+=("$obj")
done

echo ">> $CXX -c winutils_linux.cpp"
"$CXX" "${CXXFLAGS[@]}" -c "$SRC/winutils_linux.cpp" -o "$BUILD/winutils_linux.o"
objects+=("$BUILD/winutils_linux.o")

for gen in "${generated[@]}"; do
  base="$(basename "$gen" .cpp)"
  obj="$BUILD/${base}.o"
  echo ">> $CXX -c $(basename "$gen")"
  "$CXX" "${CXXFLAGS[@]}" -c "$gen" -o "$obj"
  objects+=("$obj")
done

echo "== link =="
# Replace previous binary if running
"$CXX" "${objects[@]}" -o "$OUT/StickyTodo" "${LDFLAGS[@]}"
chmod +x "$OUT/StickyTodo"

copy_soname() {
  # $1 = path to libFoo.so.N (usually a symlink to libFoo.so.N.x.y)
  local src="$1"
  local dest_dir="$2"
  local link_name real_name real_path
  link_name="$(basename "$src")"
  real_path="$(readlink -f "$src")"
  real_name="$(basename "$real_path")"
  cp -f "$real_path" "$dest_dir/$real_name"
  ln -sfn "$real_name" "$dest_dir/$link_name"
}

echo "== bundle Qt + ICU into ./lib (no symlink to build-machine prefix) =="
rm -rf "$OUT/lib"
mkdir -p "$OUT/lib"

# Core widgets stack + xcb QPA + ICU used by aqt Qt 6.8
for name in \
  libQt6Core.so.6 \
  libQt6Gui.so.6 \
  libQt6Widgets.so.6 \
  libQt6DBus.so.6 \
  libQt6XcbQpa.so.6 \
  libicudata.so.73 \
  libicui18n.so.73 \
  libicuuc.so.73
do
  if [[ ! -e "$QT/lib/$name" ]]; then
    echo "error: missing $QT/lib/$name" >&2
    exit 2
  fi
  copy_soname "$QT/lib/$name" "$OUT/lib"
done

echo "== bundle platform plugins =="
rm -rf "$OUT/plugins"
mkdir -p "$OUT/plugins/platforms"
cp -f "$QT/plugins/platforms/libqxcb.so" "$OUT/plugins/platforms/"
cp -f "$QT/plugins/platforms/libqoffscreen.so" "$OUT/plugins/platforms/"
# Optional GL integrations (harmless if display has no GL)
if [[ -d "$QT/plugins/xcbglintegrations" ]]; then
  mkdir -p "$OUT/plugins/xcbglintegrations"
  cp -f "$QT/plugins/xcbglintegrations/"*.so "$OUT/plugins/xcbglintegrations/" 2>/dev/null || true
fi
# Common image formats (icons / screenshots)
if [[ -d "$QT/plugins/imageformats" ]]; then
  mkdir -p "$OUT/plugins/imageformats"
  for f in libqgif.so libqico.so libqjpeg.so libqsvg.so; do
    [[ -f "$QT/plugins/imageformats/$f" ]] && cp -f "$QT/plugins/imageformats/$f" "$OUT/plugins/imageformats/"
  done
fi

# Flat platforms/ for older run helpers (Qt prefers plugins/platforms via QT_PLUGIN_PATH)
mkdir -p "$OUT/platforms"
cp -f "$OUT/plugins/platforms/"*.so "$OUT/platforms/"

cat > "$OUT/run.sh" << 'RUN'
#!/usr/bin/env bash
# Portable launcher: prefers bundled ./lib and ./plugins (no build-machine Qt path).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [[ -d "$HERE/plugins" ]]; then
  export QT_PLUGIN_PATH="$HERE/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
elif [[ -d "$HERE/platforms" ]]; then
  export QT_PLUGIN_PATH="$HERE${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
fi
# Optional override if you intentionally want an external Qt
if [[ -n "${STICKY_QT_DIR:-}" ]]; then
  export LD_LIBRARY_PATH="${STICKY_QT_DIR}/lib:$LD_LIBRARY_PATH"
  export QT_PLUGIN_PATH="${STICKY_QT_DIR}/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
fi

export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
DATA_DIR="${STICKY_TODO_DATA_DIR:-$HERE/config}"
mkdir -p "$DATA_DIR"
exec "$HERE/StickyTodo" --data-dir "$DATA_DIR" "$@"
RUN
chmod +x "$OUT/run.sh"

SHORT="$(cd "$ROOT" && git rev-parse --short HEAD 2>/dev/null || echo unknown)"
SUBJ="$(cd "$ROOT" && git log -1 --pretty=%s 2>/dev/null || echo build)"
BUILT="$(date '+%Y-%m-%d %H:%M %Z')"
printf 'commit=%s\n%s\nbuilt: %s\nqt=%s\nportable=1\n' "$SHORT" "$SUBJ" "$BUILT" "$QT" > "$OUT/BUILD_INFO.txt"

cat > "$OUT/README.md" << 'READ'
# Sticky Todo — Linux 可搬运包

解压后即可在常见 Linux x86_64 上运行，**不依赖**编包机器的 aqt Qt 安装路径。
包内自带 Qt 6 Widgets / Gui / Core、XCB 平台插件与 ICU。

## 系统依赖（发行版自带即可）

仍需目标机提供常见系统库（一般桌面发行版已装）：

- `libxcb*` / `libX11` / `libxkbcommon`（含 `libxkbcommon-x11`）
- `libGL` 或 `libOpenGL`、`libEGL`
- `libfontconfig`、`libfreetype`、`libglib-2.0`
- `libstdc++` / `libgcc_s`（与编包机 glibc 代际接近更稳妥）

无显示器冒烟可用：`QT_QPA_PLATFORM=offscreen ./run.sh`。

## 运行

```bash
tar -xzf StickyTodo-linux-x86_64.tar.gz
cd sticky-todo-linux   # 或解压出的目录名
./run.sh
```

数据目录默认是包内 `config/`（也可用环境变量 `STICKY_TODO_DATA_DIR` 或参数 `--data-dir`）。

## 说明

- Linux 构建下系统托盘 / 开机自启为 stub（见源码 `src/winutils_linux.cpp`）。
- 无托盘时关闭窗口即退出并落盘（与 TR #4 一致）。
- 从源码编包：在仓库根设置 `STICKY_QT_DIR` 后执行 `./build_linux.sh`；可选 `STICKY_LINUX_TAR=1` 生成 tar.gz。
READ

# Optional archive for hand-off
if [[ "${STICKY_LINUX_TAR:-0}" == "1" ]]; then
  parent="$(cd "$(dirname "$OUT")" && pwd)"
  base="$(basename "$OUT")"
  tar_path="$parent/StickyTodo-linux-x86_64.tar.gz"
  echo "== tar $tar_path =="
  tar -C "$parent" -czf "$tar_path" "$base"
  echo "tarball: $tar_path"
fi

echo "OK: $OUT/StickyTodo (portable libs under $OUT/lib)"
