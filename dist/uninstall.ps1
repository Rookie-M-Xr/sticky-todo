param(
    [switch]$RemoveData
)

$ErrorActionPreference = 'Continue'
$appName = 'Sticky Todo'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$dataDir = Join-Path $PSScriptRoot 'config'
$runtimeDir = Join-Path $env:LOCALAPPDATA 'StickyTodo\runtime'

Write-Host '正在卸载 Sticky Todo'
Write-Host ''

$instances = @(Get-Process -Name 'StickyTodo' -ErrorAction SilentlyContinue)
if ($instances.Count -eq 0) {
    Write-Host '没有正在运行的便签。'
} else {
    foreach ($process in $instances) {
        Write-Host "正在结束进程 PID $($process.Id) ..."
        try { $null = $process.CloseMainWindow() } catch {}
    }
    Start-Sleep -Seconds 1
    Get-Process -Name 'StickyTodo' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 400
    $left = @(Get-Process -Name 'StickyTodo' -ErrorAction SilentlyContinue)
    if ($left.Count -gt 0) {
        Write-Warning "仍有 $($left.Count) 个进程没有退出。请从托盘退出后再删文件夹。"
    } else {
        Write-Host '已退出正在运行的便签。'
    }
}

$startupError = $null
$removedStartup = $false
try {
    Get-ItemProperty -Path $runKey -Name $appName -ErrorAction Stop | Out-Null
    Remove-ItemProperty -Path $runKey -Name $appName -ErrorAction Stop
    $removedStartup = $true
} catch {
    $message = $_.Exception.Message
    if ($message -notmatch '不存在|does not exist|Cannot find|找不到') {
        $startupError = $message
    }
}
if ($startupError) {
    Write-Warning "没能删除开机启动项：$startupError"
} elseif ($removedStartup) {
    Write-Host '已删除开机启动项。'
} else {
    Write-Host '没有需要删除的开机启动项。'
}

function Remove-Tree([string]$dir, [string]$label) {
    if (-not (Test-Path -LiteralPath $dir)) {
        return
    }
    Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $dir) {
        Write-Warning "没能完全删除${label}：$dir"
    } else {
        Write-Host "已删除${label}：$dir"
    }
}

# Start StickyTodo.cmd 会优先启动这里的副本。只删绿色版文件夹时，这份副本还在，程序看起来就像没卸掉。
Remove-Tree $runtimeDir '隐藏的运行副本'

if ($RemoveData) {
    Remove-Tree $dataDir '待办数据'
} else {
    Write-Host "待办数据仍保留在：$dataDir"
    Write-Host '连数据一起删：卸载时加上 -RemoveData'
}

Write-Host ''
Write-Host '开机启动项和隐藏副本已处理。'
Write-Host '这是绿色版，没有系统安装登记。再删除整个程序文件夹，卸载才完成。'
