@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -Command "$d = Split-Path -Parent '%~f0'; Get-ChildItem -LiteralPath $d -Recurse -File | Unblock-File"
rem Recommended launcher: copy to a medium-integrity runtime under LocalAppData
rem and start with --data-dir pointing at this package's config. Double-clicking
rem StickyTodo.exe from a Low-labeled folder stays Low (no silent self-heal).
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
