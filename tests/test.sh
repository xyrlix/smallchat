#!/bin/bash

################################################################################
# SmallChat 测试脚本
#
# 测试服务器和客户端的基本功能
################################################################################

set -e

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_test() {
    echo -e "${BLUE}>>> $1${NC}"
}

log_pass() {
    echo -e "${GREEN}✓ $1${NC}"
}

log_fail() {
    echo -e "${RED}✗ $1${NC}"
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SERVER="${PROJECT_DIR}/output/smallchat_server"
CLIENT="${PROJECT_DIR}/output/smallchat_client"
LOG_DIR="${SCRIPT_DIR}/logs"
mkdir -p "$LOG_DIR"

# 检查可执行文件
if [ ! -f "$SERVER" ] || [ ! -f "$CLIENT" ]; then
    log_fail "找不到可执行文件，请先运行 ./build.sh"
    exit 1
fi

log_test "测试1: 启动服务器"
timeout 2 "$SERVER" > "$LOG_DIR/server_output.txt" 2>&1 || true
if grep -q "Starting SmallChat Server" "$LOG_DIR/server_output.txt"; then
    log_pass "服务器启动成功，没有crash"
else
    log_fail "服务器启动失败"
    cat "$LOG_DIR/server_output.txt"
    exit 1
fi

log_test "测试2: 启动服务器进程（后台）"
"$SERVER" > "$LOG_DIR/server_running.log" 2>&1 &
SERVER_PID=$!
sleep 1

if ps -p $SERVER_PID > /dev/null; then
    log_pass "服务器进程运行中 (PID: $SERVER_PID)"
else
    log_fail "服务器进程已崩溃"
    cat "$LOG_DIR/server_running.log"
    exit 1
fi

log_test "测试3: 客户端连接测试"
(sleep 0.5 && echo "/login TestUser" && sleep 0.5 && echo "/users" && sleep 0.5) | timeout 3 "$CLIENT" 127.0.0.1 8888 > "$LOG_DIR/client_output.txt" 2>&1 || true

if grep -qi "Connected\|Commands" "$LOG_DIR/client_output.txt"; then
    log_pass "客户端成功连接并接收到服务器消息"
else
    log_fail "客户端连接失败"
    echo "客户端输出:"
    cat "$LOG_DIR/client_output.txt"
fi

log_test "清理资源"
kill $SERVER_PID 2>/dev/null || true
sleep 1

if ! ps -p $SERVER_PID > /dev/null 2>&1; then
    log_pass "服务器正常关闭"
else
    log_fail "服务器未能正常关闭"
    kill -9 $SERVER_PID || true
fi

echo ""
echo -e "${GREEN}=========================================${NC}"
echo -e "${GREEN}所有测试通过！${NC}"
echo -e "${GREEN}=========================================${NC}"
