@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -Command "$d = Split-Path -Parent '%~f0'; Get-ChildItem -LiteralPath $d -Recurse -File | Unblock-File"
if errorlevel 1 (
    echo Failed to unblock. Right-click the ZIP, open Properties, check Unblock, then extract again.
) else (
    echo Files unblocked. You can start StickyTodo.exe now.
)
pause
