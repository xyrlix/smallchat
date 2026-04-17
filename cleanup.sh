#!/bin/bash

# SmallChat 清理脚本 - 关闭所有测试进程并清理临时文件

set -e

echo "======================================="
echo "SmallChat 清理脚本"
echo "======================================="
echo ""

# 1. 关闭所有 smallchat 相关进程
echo "[1/3] 关闭所有 smallchat 进程..."

# 获取所有 smallchat_server 进程并杀死
pkill -f "smallchat_server" || true
echo "✓ 已关闭 smallchat_server 进程"

# 获取所有 smallchat_client 进程并杀死
pkill -f "smallchat_client" || true
echo "✓ 已关闭 smallchat_client 进程"

# 有些可能被 timeout 包装，也要杀死
pkill -f "timeout.*smallchat" 2>/dev/null || true
echo "✓ 已关闭被 timeout 包装的进程"

sleep 1

# 验证所有进程已关闭
if pgrep -f "smallchat" > /dev/null 2>&1; then
    echo "⚠ 仍有进程运行，尝试强制杀死..."
    # 注意：这里使用 -10 (SIGTERM) 而不是 -9，并检查 smallchat_server 或 smallchat_client 特定进程
    pgrep -f "smallchat_server" | xargs -r kill 2>/dev/null || true
    pgrep -f "smallchat_client" | xargs -r kill 2>/dev/null || true
    sleep 1
fi

echo ""

# 2. 清理临时测试日志
echo "[2/3] 清理临时测试日志..."

# 清理 /tmp 中的旧日志
TEMP_COUNT=$(find /tmp -maxdepth 1 -name "*client*.log" -o -name "*server*.log" -o -name "smallchat*.log" 2>/dev/null | wc -l)

if [ "$TEMP_COUNT" -gt 0 ]; then
    find /tmp -maxdepth 1 \( -name "*client*.log" -o -name "*server*.log" -o -name "smallchat*.log" \) -delete 2>/dev/null
    echo "✓ 已删除 /tmp 中的 $TEMP_COUNT 个临时日志文件"
else
    echo "✓ /tmp 中无临时日志文件需要删除"
fi

# 清理 tests/logs 目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOGS_DIR="${SCRIPT_DIR}/tests/logs"
if [ -d "$LOGS_DIR" ]; then
    LOG_COUNT=$(find "$LOGS_DIR" -type f 2>/dev/null | wc -l)
    if [ "$LOG_COUNT" -gt 0 ]; then
        rm -f "$LOGS_DIR"/*.log
        echo "✓ 已删除 tests/logs 中的 $LOG_COUNT 个日志文件"
    else
        echo "✓ tests/logs 目录中无日志文件"
    fi
fi

echo ""

# 3. 显示清理结果
echo "[3/3] 清理结果..."
echo ""

RUNNING=$(pgrep -f "smallchat" 2>/dev/null | wc -l)
if [ "$RUNNING" -eq 0 ]; then
    echo "✓ 所有进程已关闭"
else
    echo "⚠ 仍有 $RUNNING 个进程运行"
    pgrep -f "smallchat"
fi

# 验证日志文件
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOGS_DIR="${SCRIPT_DIR}/tests/logs"

TEMP_COUNT=$(find /tmp -maxdepth 1 \( -name "*client*.log" -o -name "*server*.log" -o -name "smallchat*.log" \) 2>/dev/null | wc -l)
LOGS_COUNT=$(find "$LOGS_DIR" -type f 2>/dev/null | wc -l)

if [ "$TEMP_COUNT" -eq 0 ] && [ "$LOGS_COUNT" -eq 0 ]; then
    echo "✓ 所有临时日志已清理"
else
    echo "⚠ 仍有日志文件存在 (/tmp: $TEMP_COUNT, tests/logs: $LOGS_COUNT)"
fi

echo ""
echo "======================================="
echo "清理完成！"
echo "======================================="
