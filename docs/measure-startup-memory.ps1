# Measure StickyTodo startup memory on Windows (MxrPc).
# Prints Working Set / Private Bytes / Private Working Set / Commit-related fields.
# Does not interpret results. Pair with docs/startup-memory.md.
#
# Example (empty data dir, 15s idle):
#   powershell -NoProfile -ExecutionPolicy Bypass -File docs\measure-startup-memory.ps1 `
#     -Exe D:\path\StickyTodo.exe -Label v0.1.5 -GitSha e8b819e92f74a099c47358cf257fd0a5277c658a
#
# Use the same -IdleSeconds, same empty DataDir pattern, and the same Task Manager
# columns when comparing current tip vs tag v0.1.5.

param(
    [Parameter(Mandatory = $true)]
    [string]$Exe,
    [string]$DataDir = "",
    [int]$IdleSeconds = 15,
    [string]$Label = "",
    [string]$GitSha = "",
    [switch]$KeepRunning
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Format-Bytes([long]$n) {
    if ($n -lt 0) { return "-" }
    "{0:N0}  ({1:N2} MiB)" -f $n, ($n / 1MB)
}

$exePath = [System.IO.Path]::GetFullPath($Exe)
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "exe not found: $exePath"
}

$existing = @(Get-Process -Name "StickyTodo" -ErrorAction SilentlyContinue)
if ($existing.Count -gt 0) {
    $paths = $existing | ForEach-Object { try { $_.Path } catch { "?" } }
    throw "StickyTodo already running (pids $($existing.Id -join ','); paths $($paths -join '; ')). Exit it first (single-instance mutex)."
}

if ([string]::IsNullOrWhiteSpace($DataDir)) {
    $DataDir = Join-Path $env:TEMP ("sticky-todo-mem-" + [guid]::NewGuid().ToString("N"))
}
New-Item -ItemType Directory -Force -Path $DataDir | Out-Null
$dataFull = [System.IO.Path]::GetFullPath($DataDir)

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exePath
$psi.Arguments = "--data-dir `"$dataFull`""
$psi.WorkingDirectory = [System.IO.Path]::GetDirectoryName($exePath)
$psi.UseShellExecute = $true
$started = [System.Diagnostics.Process]::Start($psi)
if (-not $started) { throw "failed to start $exePath" }

Start-Sleep -Seconds $IdleSeconds

$proc = Get-Process -Id $started.Id -ErrorAction SilentlyContinue
if (-not $proc) {
    $proc = @(Get-Process -Name "StickyTodo" -ErrorAction SilentlyContinue) | Select-Object -First 1
}
if (-not $proc) {
    throw "process exited before sample (idle ${IdleSeconds}s). Check Low IL / missing Qt DLL / single-instance."
}

$imagePath = $null
try { $imagePath = $proc.Path } catch { $imagePath = $exePath }

$privateWs = $null
try {
    $perf = Get-CimInstance Win32_PerfFormattedData_PerfProc_Process |
        Where-Object { $_.IDProcess -eq $proc.Id } |
        Select-Object -First 1
    if ($perf -and $null -ne $perf.WorkingSetPrivate) {
        $privateWs = [int64]$perf.WorkingSetPrivate
    }
} catch {
    $privateWs = $null
}

$pageFileKb = $null
try {
    $wmi = Get-CimInstance Win32_Process -Filter ("ProcessId={0}" -f $proc.Id)
    if ($wmi) { $pageFileKb = [int64]$wmi.PageFileUsage }
} catch {
    $pageFileKb = $null
}

$commitBytes = if ($null -ne $pageFileKb) { $pageFileKb * 1024 } else { $null }

Write-Output "==== StickyTodo startup sample ===="
Write-Output ("label          : {0}" -f $(if ($Label) { $Label } else { "(unset)" }))
Write-Output ("git_sha        : {0}" -f $(if ($GitSha) { $GitSha } else { "(unset)" }))
Write-Output ("exe_arg        : {0}" -f $exePath)
Write-Output ("image_path     : {0}" -f $imagePath)
Write-Output ("pid            : {0}" -f $proc.Id)
Write-Output ("data_dir       : {0}" -f $dataFull)
Write-Output ("idle_seconds   : {0}" -f $IdleSeconds)
Write-Output ("start_time     : {0:yyyy-MM-dd HH:mm:ss}" -f $proc.StartTime)
Write-Output ""
Write-Output "Task Manager 详细信息 mapping (do not mix columns):"
Write-Output ("  工作集              WorkingSet64           = {0}" -f (Format-Bytes $proc.WorkingSet64))
Write-Output ("  专用工作集          WorkingSetPrivate      = {0}" -f $(if ($null -ne $privateWs) { Format-Bytes $privateWs } else { "(counter unavailable)" }))
Write-Output ("  专用字节/Private Bytes  PrivateMemorySize64 = {0}" -f (Format-Bytes $proc.PrivateMemorySize64))
Write-Output ("  提交大小(约)        Win32 PageFileUsage    = {0}" -f $(if ($null -ne $commitBytes) { Format-Bytes $commitBytes } else { "(unavailable)" }))
Write-Output ("  峰值工作集          PeakWorkingSet64       = {0}" -f (Format-Bytes $proc.PeakWorkingSet64))
Write-Output ("  虚拟内存            VirtualMemorySize64    = {0}" -f (Format-Bytes $proc.VirtualMemorySize64))
Write-Output ""
Write-Output "Win11 进程页 default 内存 column is often Active Private Working Set, not 工作集."
Write-Output "Record whichever column User used, then compare the SAME column on v0.1.5."

if (-not $KeepRunning) {
    try { Stop-Process -Id $proc.Id -Force -ErrorAction Stop } catch { }
}
