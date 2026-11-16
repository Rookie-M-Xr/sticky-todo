@echo off
setlocal EnableExtensions
powershell -NoProfile -ExecutionPolicy Bypass -Command "$d = Split-Path -Parent '%~f0'; Get-ChildItem -LiteralPath $d -Recurse -File | Unblock-File"
rem cmd.exe is Medium. Double-clicking StickyTodo.exe from a Low-labeled
rem folder stays Low (no silent self-heal). If THIS package is not Low,
rem start its current exe in place — never a leftover LocalAppData runtime.
rem Low trees: always overwrite %LOCALAPPDATA%\StickyTodo\runtime from this
rem package, then start that Medium copy. Do not copy-once.
set "PKG=%~dp0"
set "PKG_EXE=%~dp0StickyTodo.exe"
set "DATA=%~dp0config"
set "RUNTIME_DIR=%LOCALAPPDATA%\StickyTodo\runtime"
set "RUNTIME=%RUNTIME_DIR%\StickyTodo.exe"

if not exist "%PKG_EXE%" (
  echo StickyTodo.exe not found next to this script.
  exit /b 1
)

set "LOW_IL="
icacls "%PKG_EXE%" "%~dp0." 2>nul | findstr /I /C:"Low Mandatory Level" /C:"S-1-16-4096" >nul
if not errorlevel 1 set "LOW_IL=1"

if not defined LOW_IL (
  start "" /D "%~dp0." "%PKG_EXE%" --data-dir "%DATA%"
  goto :eof
)

mkdir "%RUNTIME_DIR%\platforms" 2>nul
copy /Y "%PKG_EXE%" "%RUNTIME%" >nul
if errorlevel 1 (
  echo Failed to refresh the medium-integrity runtime from this package.
  echo Close StickyTodo if it is already running, then try again.
  exit /b 1
)
copy /Y "%~dp0Qt6Core.dll" "%RUNTIME_DIR%\" >nul
copy /Y "%~dp0Qt6Gui.dll" "%RUNTIME_DIR%\" >nul
copy /Y "%~dp0Qt6Widgets.dll" "%RUNTIME_DIR%\" >nul
copy /Y "%~dp0libgcc_s_seh-1.dll" "%RUNTIME_DIR%\" >nul
copy /Y "%~dp0libstdc++-6.dll" "%RUNTIME_DIR%\" >nul
copy /Y "%~dp0libwinpthread-1.dll" "%RUNTIME_DIR%\" >nul
copy /Y "%~dp0platforms\qwindows.dll" "%RUNTIME_DIR%\platforms\" >nul
start "" /D "%RUNTIME_DIR%" "%RUNTIME%" --data-dir "%DATA%"
