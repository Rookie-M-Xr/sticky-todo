#!/usr/bin/env python3
"""Build Sticky Todo with the local Qt/MinGW toolchain.

qmake and CMake are intentionally not used: this repository builds with a
small, explicit compile/link pipeline that is easy to audit and reproduce.
"""

from __future__ import annotations

import ctypes
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "src"
RES = ROOT / "resources"
BUILD = ROOT / "build"
DIST = ROOT / "dist"

_qt = os.environ.get("STICKY_QT_DIR")
_mingw = os.environ.get("STICKY_MINGW_DIR")
if not _qt or not _mingw:
    print(
        "error: set STICKY_QT_DIR and STICKY_MINGW_DIR to your toolchain prefixes "
        "(e.g. STICKY_QT_DIR=C:\\Qt\\6.11.1\\mingw_64, "
        "STICKY_MINGW_DIR=C:\\mingw64). "
        "No machine-specific defaults are baked into the repo.",
        file=sys.stderr,
    )
    raise SystemExit(2)
QT = Path(_qt)
MINGW = Path(_mingw)
BIN = MINGW / "bin"
QTBIN = QT / "bin"

SOURCES = [
    "main.cpp",
    "models.cpp",
    "datastore.cpp",
    "theme.cpp",
    "icons.cpp",
    "widgets.cpp",
    "todoview.cpp",
    "calendarpopup.cpp",
    "settingspage.cpp",
    "dailyreportpage.cpp",
    "winutils.cpp",
    "mainwindow.cpp",
]

CXXFLAGS = [
    "-std=c++17",
    "-O2",
    "-DNDEBUG",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-DQT_WIDGETS_LIB",
    "-DQT_GUI_LIB",
    "-DQT_CORE_LIB",
    "-DUNICODE",
    "-D_UNICODE",
    f"-I{QT / 'include'}",
    f"-I{QT / 'include' / 'QtCore'}",
    f"-I{QT / 'include' / 'QtGui'}",
    f"-I{QT / 'include' / 'QtWidgets'}",
    f"-I{QT / 'mkspecs' / 'win32-g++'}",
    f"-I{SRC}",
]

LDFLAGS = [
    "-mwindows",
    "-mthreads",
    f"-L{QT / 'lib'}",
    "-lQt6EntryPoint",
    "-lQt6Widgets",
    "-lQt6Gui",
    "-lQt6Core",
    "-lrpcrt4",
    "-lole32",
    "-loleaut32",
    "-luuid",
    "-luser32",
    "-lshell32",
    "-ladvapi32",
    "-ldwmapi",
    "-lversion",
    "-lwinmm",
    "-lws2_32",
    "-luserenv",
    "-lmingw32",
]


def install_medium_runtime(dist: Path) -> None:
    local = os.environ.get("LOCALAPPDATA")
    if not local:
        print("LOCALAPPDATA is unset; skipped the medium-integrity runtime copy")
        return
    runtime = Path(local) / "StickyTodo" / "runtime"
    (runtime / "platforms").mkdir(parents=True, exist_ok=True)
    names = [
        "StickyTodo.exe",
        "Qt6Core.dll",
        "Qt6Gui.dll",
        "Qt6Widgets.dll",
        "libgcc_s_seh-1.dll",
        "libstdc++-6.dll",
        "libwinpthread-1.dll",
    ]
    for name in names:
        destination = runtime / name
        shutil.copy2(dist / name, destination)
        strip_downloaded_mark(destination)
    platform = runtime / "platforms" / "qwindows.dll"
    shutil.copy2(dist / "platforms" / "qwindows.dll", platform)
    strip_downloaded_mark(platform)
    print(f"Medium-integrity runtime: {runtime / 'StickyTodo.exe'}")


def strip_downloaded_mark(path: Path) -> None:
    """Drop the Windows Zone.Identifier stream copied from the toolchain.

    shutil.copy2 preserves alternate data streams. The MinGW runtime DLLs were
    downloaded, so that mark makes SmartScreen block StickyTodo.exe on launch.
    """
    if os.name != "nt":
        return
    ctypes.windll.kernel32.DeleteFileW(str(path) + ":Zone.Identifier")


def run(cmd: list[str]) -> None:
    print(">>", " ".join(str(x) for x in cmd))
    subprocess.run([str(x) for x in cmd], cwd=ROOT, check=True)


def main() -> int:
    for tool in ("g++.exe", "windres.exe"):
        if not (BIN / tool).exists():
            print(f"missing tool: {BIN / tool}", file=sys.stderr)
            return 2
    for tool in ("moc.exe", "rcc.exe"):
        if not (QTBIN / tool).exists():
            print(f"missing tool: {QTBIN / tool}", file=sys.stderr)
            return 2

    BUILD.mkdir(parents=True, exist_ok=True)
    DIST.mkdir(parents=True, exist_ok=True)

    # Qt meta-object compiler for every header that declares Q_OBJECT.
    moc_headers = [
        "datastore.h",
        "widgets.h",
        "todoview.h",
        "calendarpopup.h",
        "settingspage.h",
        "dailyreportpage.h",
        "mainwindow.h",
    ]
    generated: list[Path] = []
    for header in moc_headers:
        moc_cpp = BUILD / ("moc_" + Path(header).stem + ".cpp")
        run([
            QTBIN / "moc.exe",
            SRC / header,
            "-o", moc_cpp,
            f"-I{SRC}",
        ])
        generated.append(moc_cpp)

    # Resources: icon bitmap (Qt) and Windows icon/manifest/version (windres).
    run([
        QTBIN / "rcc.exe",
        RES / "resources.qrc",
        "-o", BUILD / "qrc_resources.cpp",
    ])
    run([
        BIN / "windres.exe",
        "--use-temp-file",
        RES / "app.rc",
        "-o", BUILD / "app_res.o",
        f"-I{RES}",
    ])

    objects: list[Path] = []
    for source in SOURCES:
        obj = BUILD / (Path(source).stem + ".o")
        run([BIN / "g++.exe", *CXXFLAGS, "-c", SRC / source, "-o", obj])
        objects.append(obj)

    for generated_cpp in generated + [BUILD / "qrc_resources.cpp"]:
        obj = BUILD / (generated_cpp.stem + ".o")
        run([BIN / "g++.exe", *CXXFLAGS, "-c", generated_cpp, "-o", obj])
        objects.append(obj)

    exe = DIST / "StickyTodo.exe"
    run([BIN / "g++.exe", *objects, BUILD / "app_res.o", "-o", exe, *LDFLAGS])

    # Deploy the Qt runtime and the Windows platform plugin.  windeployqt is
    # not used here because it spawns helper processes that are unavailable in
    # this build sandbox; the required file set for Qt Widgets is small.
    qt_runtime = [
        "Qt6Core.dll",
        "Qt6Gui.dll",
        "Qt6Widgets.dll",
    ]
    mingw_runtime = [
        "libgcc_s_seh-1.dll",
        "libstdc++-6.dll",
        "libwinpthread-1.dll",
    ]
    for name in qt_runtime:
        destination = DIST / name
        shutil.copy2(QT / "bin" / name, destination)
        strip_downloaded_mark(destination)
    for name in mingw_runtime:
        destination = DIST / name
        shutil.copy2(BIN / name, destination)
        strip_downloaded_mark(destination)
    (DIST / "platforms").mkdir(exist_ok=True)
    platform = DIST / "platforms" / "qwindows.dll"
    shutil.copy2(QT / "plugins" / "platforms" / "qwindows.dll", platform)
    strip_downloaded_mark(platform)
    strip_downloaded_mark(exe)
    for file in DIST.rglob("*"):
        if file.is_file():
            strip_downloaded_mark(file)

    # The project directory carries a low integrity label, so running the exe
    # from dist drops the process to low integrity and Windows rejects its
    # tray icon. Keep a medium-integrity copy under LocalAppData; a low launch
    # hands off to it.
    install_medium_runtime(DIST)

    print()
    print(f"Built {exe}")
    total = sum(p.stat().st_size for p in DIST.rglob("*") if p.is_file())
    print(f"Deployment size: {total / 1024 / 1024:.1f} MiB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
