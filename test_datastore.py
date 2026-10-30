#!/usr/bin/env python3
"""Build and run headless DataStore tests (no GUI).

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


def find_moc() -> Path:
    if IS_WINDOWS:
        moc = QTBIN / "moc.exe"
    else:
        moc = QT / "libexec" / "moc"
        if not moc.exists():
            moc = QTBIN / "moc"
    if not moc.exists():
        print(f"missing tool: {moc}", file=sys.stderr)
        raise SystemExit(2)
    return moc


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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
