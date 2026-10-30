@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -Command "$d = Split-Path -Parent '%~f0'; Get-ChildItem -LiteralPath $d -Recurse -File | Unblock-File"
rem The project folder is marked low integrity, so StickyTodo.exe beside this
rem script is started at low integrity and Windows rejects its tray icon.
rem The build copies a normal-integrity runtime under LocalAppData.
set "RUNTIME=%LOCALAPPDATA%\StickyTodo\runtime\StickyTodo.exe"
set "DATA=%~dp0config"
if exist "%RUNTIME%" (
  start "" /D "%LOCALAPPDATA%\StickyTodo\runtime" "%RUNTIME%" --data-dir "%DATA%"
) else (
  start "" "%~dp0StickyTodo.exe"
)
