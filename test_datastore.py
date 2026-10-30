#!/usr/bin/env python3
"""Build and run headless DataStore tests (no GUI).

Exit 0 on green, non-zero on compile or assertion failure.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "src"
TESTS = ROOT / "tests"
BUILD = ROOT / "build" / "tests"
TESTDATA = ROOT / "testdata"

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

CXXFLAGS = [
    "-std=c++17",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-DQT_CORE_LIB",
    "-DUNICODE",
    "-D_UNICODE",
    f"-I{QT / 'include'}",
    f"-I{QT / 'include' / 'QtCore'}",
    f"-I{QT / 'mkspecs' / 'win32-g++'}",
    f"-I{SRC}",
]

LDFLAGS = [
    "-mthreads",
    f"-L{QT / 'lib'}",
    "-lQt6Core",
    "-lrpcrt4",
    "-lole32",
    "-luuid",
    "-luser32",
    "-ladvapi32",
    "-lshell32",
    "-lws2_32",
    "-lversion",
    "-lwinmm",
    "-luserenv",
]


def run(cmd: list[str | Path], env: dict[str, str] | None = None) -> None:
    print(">>", " ".join(str(x) for x in cmd))
    subprocess.run([str(x) for x in cmd], cwd=ROOT, check=True, env=env)


def main() -> int:
    for tool in ("g++.exe",):
        if not (BIN / tool).exists():
            print(f"missing tool: {BIN / tool}", file=sys.stderr)
            return 2
    if not (QTBIN / "moc.exe").exists():
        print(f"missing tool: {QTBIN / 'moc.exe'}", file=sys.stderr)
        return 2

    BUILD.mkdir(parents=True, exist_ok=True)
    TESTDATA.mkdir(parents=True, exist_ok=True)

    moc_cpp = BUILD / "moc_datastore.cpp"
    run([QTBIN / "moc.exe", SRC / "datastore.h", "-o", moc_cpp, f"-I{SRC}"])

    objects: list[Path] = []
    sources = [
        SRC / "models.cpp",
        SRC / "datastore.cpp",
        moc_cpp,
        TESTS / "test_datastore.cpp",
    ]
    for source in sources:
        obj = BUILD / (source.stem + ".o")
        run([BIN / "g++.exe", *CXXFLAGS, "-c", source, "-o", obj])
        objects.append(obj)

    exe = BUILD / "test_datastore.exe"
    run([BIN / "g++.exe", *objects, "-o", exe, *LDFLAGS])

    env = os.environ.copy()
    path_parts = [str(QTBIN), str(BIN), env.get("PATH", "")]
    env["PATH"] = os.pathsep.join(path_parts)
    env["STICKY_TODO_TEST_ROOT"] = str(TESTDATA / "datastore")

    print(">>", exe)
    result = subprocess.run([str(exe)], cwd=ROOT, env=env)
    if result.returncode != 0:
        print("DataStore tests FAILED", file=sys.stderr)
        return result.returncode or 1
    print("DataStore tests OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
