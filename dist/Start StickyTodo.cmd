@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -Command "$d = Split-Path -Parent '%~f0'; Get-ChildItem -LiteralPath $d -Recurse -File | Unblock-File"
rem Fallback launcher: prefer medium-integrity copy under LocalAppData (same as
rem in-app low-IL handoff). StickyTodo.exe self-heals when double-clicked; this
rem script remains an optional unblock + start path.
set "RUNTIME_DIR=%LOCALAPPDATA%\StickyTodo\runtime"
set "RUNTIME=%RUNTIME_DIR%\StickyTodo.exe"
set "DATA=%~dp0config"
if not exist "%RUNTIME%" (
  mkdir "%RUNTIME_DIR%\platforms" 2>nul
  copy /Y "%~dp0StickyTodo.exe" "%RUNTIME_DIR%\StickyTodo.exe" >nul
  copy /Y "%~dp0Qt6Core.dll" "%RUNTIME_DIR%\" >nul
  copy /Y "%~dp0Qt6Gui.dll" "%RUNTIME_DIR%\" >nul
  copy /Y "%~dp0Qt6Widgets.dll" "%RUNTIME_DIR%\" >nul
  copy /Y "%~dp0libgcc_s_seh-1.dll" "%RUNTIME_DIR%\" >nul
  copy /Y "%~dp0libstdc++-6.dll" "%RUNTIME_DIR%\" >nul
  copy /Y "%~dp0libwinpthread-1.dll" "%RUNTIME_DIR%\" >nul
  copy /Y "%~dp0platforms\qwindows.dll" "%RUNTIME_DIR%\platforms\" >nul
)
if exist "%RUNTIME%" (
  start "" /D "%RUNTIME_DIR%" "%RUNTIME%" --data-dir "%DATA%"
) else (
  start "" "%~dp0StickyTodo.exe"
)
