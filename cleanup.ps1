#!/usr/bin/env powershell

# SmallChat 清理脚本 - 关闭所有测试进程并清理临时文件

Write-Host "======================================="
Write-Host "SmallChat 清理脚本"
Write-Host "======================================="
Write-Host ""

# 1. 关闭所有 smallchat 相关进程
Write-Host "[1/3] 关闭所有 smallchat 进程..."

# 获取所有 smallchat 进程并杀死
try {
    $processes = Get-Process | Where-Object { $_.ProcessName -like "smallchat*" }
    if ($processes.Count -gt 0) {
        Write-Host "发现 $($processes.Count) 个 smallchat 进程，正在杀死..."
        $processes | ForEach-Object { 
            Write-Host "杀死进程: $($_.ProcessName) (PID: $($_.Id))"
            $_.Kill()
        }
        Start-Sleep -Seconds 2
    } else {
        Write-Host "没有发现 smallchat 进程"
    }
} catch {
    Write-Host "关闭进程时出错: $($_.Exception.Message)"
}

# 再次检查并尝试强制杀死
try {
    $remainingProcesses = Get-Process | Where-Object { $_.ProcessName -like "smallchat*" }
    if ($remainingProcesses.Count -gt 0) {
        Write-Host "仍有 $($remainingProcesses.Count) 个进程运行，尝试强制杀死..."
        $remainingProcesses | ForEach-Object { 
            try {
                $_.Kill()
                $_.WaitForExit(2000)
            } catch {
                Write-Host "杀死进程 $($_.ProcessName) (PID: $($_.Id)) 时出错: $($_.Exception.Message)"
            }
        }
        Start-Sleep -Seconds 1
    }
} catch {
    Write-Host "验证进程时出错: $($_.Exception.Message)"
}

Write-Host ""

# 2. 清理临时测试日志
Write-Host "[2/3] 清理临时测试日志..."

# 清理 %TEMP% 中的旧日志
try {
    $tempDir = $env:TEMP
    $tempLogs = Get-ChildItem -Path $tempDir -Name "*client*.log", "*server*.log", "smallchat*.log" -ErrorAction SilentlyContinue
    if ($tempLogs.Count -gt 0) {
        $tempLogs | ForEach-Object { Remove-Item -Path "$tempDir\$_" -Force -ErrorAction SilentlyContinue }
        Write-Host "已删除 %TEMP% 中的 $($tempLogs.Count) 个临时日志文件"
    } else {
        Write-Host "%TEMP% 中无临时日志文件需要删除"
    }
} catch {
    Write-Host "清理 %TEMP% 日志时出错: $($_.Exception.Message)"
}

# 清理 tests/logs 目录
try {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $logsDir = "$scriptDir\tests\logs"
    if (Test-Path -Path $logsDir) {
        $logFiles = Get-ChildItem -Path $logsDir -Name "*.log" -ErrorAction SilentlyContinue
        if ($logFiles.Count -gt 0) {
            $logFiles | ForEach-Object { Remove-Item -Path "$logsDir\$_" -Force -ErrorAction SilentlyContinue }
            Write-Host "已删除 tests/logs 中的 $($logFiles.Count) 个日志文件"
        } else {
            Write-Host "tests/logs 目录中无日志文件"
        }
    } else {
        Write-Host "tests/logs 目录不存在"
    }
} catch {
    Write-Host "清理 tests/logs 时出错: $($_.Exception.Message)"
}

Write-Host ""

# 3. 显示清理结果
Write-Host "[3/3] 清理结果..."
Write-Host ""

try {
    $runningProcesses = Get-Process | Where-Object { $_.ProcessName -like "smallchat*" }
    if ($runningProcesses.Count -eq 0) {
        Write-Host "所有进程已关闭"
    } else {
        Write-Host "仍有 $($runningProcesses.Count) 个进程运行"
        $runningProcesses | ForEach-Object { Write-Host "  - $($_.ProcessName) (PID: $($_.Id))" }
    }
} catch {
    Write-Host "检查运行进程时出错: $($_.Exception.Message)"
}

# 验证日志文件
try {
    $tempDir = $env:TEMP
    $tempLogs = Get-ChildItem -Path $tempDir -Name "*client*.log", "*server*.log", "smallchat*.log" -ErrorAction SilentlyContinue
    $tempCount = $tempLogs.Count
    
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $logsDir = "$scriptDir\tests\logs"
    if (Test-Path -Path $logsDir) {
        $logFiles = Get-ChildItem -Path $logsDir -Name "*.log" -ErrorAction SilentlyContinue
        $logsCount = $logFiles.Count
    } else {
        $logsCount = 0
    }
    
    if ($tempCount -eq 0 -and $logsCount -eq 0) {
        Write-Host "所有临时日志已清理"
    } else {
        Write-Host "仍有日志文件存在 (%TEMP%: $tempCount, tests/logs: $logsCount)"
    }
} catch {
    Write-Host "验证日志文件时出错: $($_.Exception.Message)"
}

Write-Host ""
Write-Host "======================================="
Write-Host "清理完成！"
Write-Host "======================================="
