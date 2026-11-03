#!/usr/bin/env python3
"""Build and run headless DataStore tests, then offscreen widget tests.

Exit 0 on green, non-zero on compile or assertion failure.
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "src"
TESTS = ROOT / "tests"
BUILD = ROOT / "build" / "tests"
TESTDATA = ROOT / "testdata"

IS_WINDOWS = os.name == "nt"

_qt = os.environ.get("STICKY_QT_DIR")
_mingw = os.environ.get("STICKY_MINGW_DIR")
if not _qt or (IS_WINDOWS and not _mingw):
    print(
        "error: set STICKY_QT_DIR"
        + (" and STICKY_MINGW_DIR" if IS_WINDOWS else "")
        + " to your toolchain prefixes "
        "(e.g. STICKY_QT_DIR=C:\\Qt\\6.11.1\\mingw_64, "
        "STICKY_MINGW_DIR=C:\\mingw64; Linux: STICKY_QT_DIR=/path/to/gcc_64). "
        "No machine-specific defaults are baked into the repo.",
        file=sys.stderr,
    )
    raise SystemExit(2)
QT = Path(_qt)
MINGW = Path(_mingw) if _mingw else None
BIN = (MINGW / "bin") if MINGW else None
QTBIN = QT / "bin"


def run(cmd: list[str | Path], env: dict[str, str] | None = None) -> None:
    print(">>", " ".join(str(x) for x in cmd))
    subprocess.run([str(x) for x in cmd], cwd=ROOT, check=True, env=env)


def find_tool(name: str) -> Path:
    if IS_WINDOWS:
        tool = QTBIN / f"{name}.exe"
    else:
        tool = QT / "libexec" / name
        if not tool.exists():
            tool = QTBIN / name
    if not tool.exists():
        print(f"missing tool: {tool}", file=sys.stderr)
        raise SystemExit(2)
    return tool


def find_moc() -> Path:
    return find_tool("moc")


def find_rcc() -> Path:
    return find_tool("rcc")


def cxx_tool() -> Path:
    if IS_WINDOWS:
        assert BIN is not None
        cxx = BIN / "g++.exe"
        if not cxx.exists():
            print(f"missing tool: {cxx}", file=sys.stderr)
            raise SystemExit(2)
        return cxx
    return Path("g++")


def toolchain() -> tuple[list[str], list[str], Path, list[Path]]:
    """Return (cxxflags, ldflags, exe_path, extra_sources)."""
    moc_cpp = BUILD / "moc_datastore.cpp"
    common_includes = [
        f"-I{QT / 'include'}",
        f"-I{QT / 'include' / 'QtCore'}",
        f"-I{QT / 'include' / 'QtGui'}",
        f"-I{QT / 'include' / 'QtWidgets'}",
        f"-I{SRC}",
    ]
    sources = [
        SRC / "models.cpp",
        SRC / "datastore.cpp",
        SRC / "theme.cpp",
        moc_cpp,
        TESTS / "test_datastore.cpp",
    ]
    if IS_WINDOWS:
        cxxflags = [
            "-std=c++17",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-DQT_WIDGETS_LIB",
            "-DQT_GUI_LIB",
            "-DQT_CORE_LIB",
            "-DUNICODE",
            "-D_UNICODE",
            *common_includes,
            f"-I{QT / 'mkspecs' / 'win32-g++'}",
        ]
        ldflags = [
            "-mthreads",
            f"-L{QT / 'lib'}",
            "-lQt6Widgets",
            "-lQt6Gui",
            "-lQt6Core",
            "-lrpcrt4",
            "-lole32",
            "-loleaut32",
            "-luuid",
            "-luser32",
            "-ladvapi32",
            "-lshell32",
            "-ldwmapi",
            "-lws2_32",
            "-lversion",
            "-lwinmm",
            "-luserenv",
        ]
        sources.append(SRC / "winutils.cpp")
        exe = BUILD / "test_datastore.exe"
    else:
        cxxflags = [
            "-std=c++17",
            "-O2",
            "-fPIC",
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-DQT_WIDGETS_LIB",
            "-DQT_GUI_LIB",
            "-DQT_CORE_LIB",
            *common_includes,
            f"-I{QT / 'mkspecs' / 'linux-g++'}",
        ]
        ldflags = [
            f"-Wl,-rpath,{QT / 'lib'}",
            f"-L{QT / 'lib'}",
            "-lQt6Widgets",
            "-lQt6Gui",
            "-lQt6Core",
            "-lpthread",
            "-ldl",
        ]
        sources.append(SRC / "winutils_linux.cpp")
        exe = BUILD / "test_datastore"
    return cxxflags, ldflags, exe, sources


def main() -> int:
    cxx = cxx_tool()
    moc = find_moc()

    BUILD.mkdir(parents=True, exist_ok=True)
    TESTDATA.mkdir(parents=True, exist_ok=True)

    moc_cpp = BUILD / "moc_datastore.cpp"
    run([moc, SRC / "datastore.h", "-o", moc_cpp, f"-I{SRC}"])

    cxxflags, ldflags, exe, sources = toolchain()
    objects: list[Path] = []
    for source in sources:
        obj = BUILD / (source.stem + ".o")
        run([cxx, *cxxflags, "-c", source, "-o", obj])
        objects.append(obj)

    run([cxx, *objects, "-o", exe, *ldflags])

    env = os.environ.copy()
    path_parts = [str(QTBIN)]
    if BIN is not None:
        path_parts.append(str(BIN))
    path_parts.append(env.get("PATH", ""))
    env["PATH"] = os.pathsep.join(path_parts)
    env["STICKY_TODO_TEST_ROOT"] = str(TESTDATA / "datastore")
    env["LD_LIBRARY_PATH"] = os.pathsep.join(
        [str(QT / "lib"), env.get("LD_LIBRARY_PATH", "")]
    )

    print(">>", exe)
    result = subprocess.run([str(exe)], cwd=ROOT, env=env)
    if result.returncode != 0:
        print("DataStore tests FAILED", file=sys.stderr)
        return result.returncode or 1
    print("DataStore tests OK")

    moc_widgets = BUILD / "moc_widgets.cpp"
    run([moc, SRC / "widgets.h", "-o", moc_widgets, f"-I{SRC}"])
    snack_sources = [
        SRC / "icons.cpp",
        SRC / "widgets.cpp",
        moc_widgets,
        TESTS / "test_snackbar.cpp",
    ]
    snack_objects = [BUILD / "models.o", BUILD / "theme.o"]
    snack_objects.append(BUILD / ("winutils.o" if IS_WINDOWS else "winutils_linux.o"))
    for source in snack_sources:
        obj = BUILD / (source.stem + ".o")
        run([cxx, *cxxflags, "-c", source, "-o", obj])
        snack_objects.append(obj)
    snack_exe = BUILD / ("test_snackbar.exe" if IS_WINDOWS else "test_snackbar")
    run([cxx, *snack_objects, "-o", snack_exe, *ldflags])

    snack_env = env.copy()
    snack_env["QT_QPA_PLATFORM"] = "offscreen"
    plugin_path = QT / "plugins"
    if plugin_path.exists():
        snack_env["QT_PLUGIN_PATH"] = os.pathsep.join(
            [str(plugin_path), snack_env.get("QT_PLUGIN_PATH", "")]
        )
    print(">>", snack_exe)
    snack_result = subprocess.run([str(snack_exe)], cwd=ROOT, env=snack_env)
    if snack_result.returncode != 0:
        print("Snackbar tests FAILED", file=sys.stderr)
        return snack_result.returncode or 1
    print("Snackbar tests OK")

    quick_cpp = TESTS / "test_quickadd.cpp"
    quick_obj = BUILD / "test_quickadd.o"
    run([cxx, *cxxflags, "-c", quick_cpp, "-o", quick_obj])
    quick_objects = [
        BUILD / "models.o",
        BUILD / "theme.o",
        BUILD / ("winutils.o" if IS_WINDOWS else "winutils_linux.o"),
        BUILD / "icons.o",
        BUILD / "widgets.o",
        BUILD / "moc_widgets.o",
        quick_obj,
    ]
    quick_exe = BUILD / ("test_quickadd.exe" if IS_WINDOWS else "test_quickadd")
    run([cxx, *quick_objects, "-o", quick_exe, *ldflags])

    print(">>", quick_exe)
    quick_result = subprocess.run([str(quick_exe)], cwd=ROOT, env=snack_env)
    if quick_result.returncode != 0:
        print("Quick-add tests FAILED", file=sys.stderr)
        return quick_result.returncode or 1
    print("Quick-add tests OK")

    rcc = find_rcc()
    moc_headers = [
        "datastore.h",
        "widgets.h",
        "todoview.h",
        "calendarpopup.h",
        "settingspage.h",
        "dailyreportpage.h",
        "mainwindow.h",
    ]
    window_objects: list[Path] = []
    for header in moc_headers:
        moc_cpp = BUILD / f"moc_{Path(header).stem}.cpp"
        run([moc, SRC / header, "-o", moc_cpp, f"-I{SRC}"])
        obj = BUILD / f"moc_{Path(header).stem}.o"
        run([cxx, *cxxflags, "-c", moc_cpp, "-o", obj])
        window_objects.append(obj)

    qrc_cpp = BUILD / "qrc_resources.cpp"
    run([rcc, ROOT / "resources" / "resources.qrc", "-o", qrc_cpp])
    qrc_obj = BUILD / "qrc_resources.o"
    run([cxx, *cxxflags, "-c", qrc_cpp, "-o", qrc_obj])
    window_objects.append(qrc_obj)

    window_sources = [
        "models.cpp",
        "datastore.cpp",
        "theme.cpp",
        "icons.cpp",
        "widgets.cpp",
        "todoview.cpp",
        "calendarpopup.cpp",
        "settingspage.cpp",
        "dailyreportpage.cpp",
        "mainwindow.cpp",
        "winutils.cpp" if IS_WINDOWS else "winutils_linux.cpp",
    ]
    for source in window_sources:
        src_path = SRC / source
        obj = BUILD / f"{Path(source).stem}_app.o"
        run([cxx, *cxxflags, "-c", src_path, "-o", obj])
        window_objects.append(obj)

    window_cpp = TESTS / "test_quickadd_window.cpp"
    window_obj = BUILD / "test_quickadd_window.o"
    run([cxx, *cxxflags, "-c", window_cpp, "-o", window_obj])
    window_objects.append(window_obj)
    window_exe = BUILD / ("test_quickadd_window.exe" if IS_WINDOWS else "test_quickadd_window")
    run([cxx, *window_objects, "-o", window_exe, *ldflags])

    print(">>", window_exe)
    window_result = subprocess.run([str(window_exe)], cwd=ROOT, env=snack_env)
    if window_result.returncode != 0:
        print("Quick-add window tests FAILED", file=sys.stderr)
        return window_result.returncode or 1
    print("Quick-add window tests OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
