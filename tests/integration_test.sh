#!/bin/bash

################################################################################
# SmallChat 完整测试脚本
#
# 测试服务器和客户端的完整功能，包括登录、用户列表、私信和广播消息
################################################################################

set -e

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_test() {
    echo -e "${YELLOW}>>> $1${NC}"
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
TEST_PORT=8888
LOG_DIR="${SCRIPT_DIR}/logs"

mkdir -p "$LOG_DIR"

cleanup() {
    log_info "清理测试环境..."
    if [ ! -z "$SERVER_PID" ] && ps -p $SERVER_PID > /dev/null 2>&1; then
        kill $SERVER_PID 2>/dev/null || true
        wait $SERVER_PID 2>/dev/null || true
    fi
    rm -f /tmp/input.txt /tmp/server.log /tmp/client.log 2>/dev/null || true
}

trap cleanup EXIT

check_executables() {
    if [ ! -f "$SERVER" ] || [ ! -f "$CLIENT" ]; then
        log_fail "找不到可执行文件，请先运行 ./build.sh"
        exit 1
    fi
    log_pass "可执行文件检查通过"
}

test_server_startup() {
    log_test "测试1: 服务器启动"
    rm -f "$LOG_DIR/server_startup.log"
    timeout 2 "$SERVER" $TEST_PORT > "$LOG_DIR/server_startup.log" 2>&1 || true

    if grep -q "Starting SmallChat Server" "$LOG_DIR/server_startup.log"; then
        log_pass "服务器启动成功"
    else
        log_fail "服务器启动失败"
        cat "$LOG_DIR/server_startup.log"
        exit 1
    fi
}

test_server_running() {
    log_test "测试2: 服务器进程运行"
    "$SERVER" $TEST_PORT > "$LOG_DIR/server_running.log" 2>&1 &
    SERVER_PID=$!
    sleep 1

    if ps -p $SERVER_PID > /dev/null; then
        log_pass "服务器进程运行中 (PID: $SERVER_PID)"
    else
        log_fail "服务器进程已崩溃"
        cat "$LOG_DIR/server_running.log"
        exit 1
    fi
}

test_client_connection() {
    log_test "测试3: 客户端连接"

    {
        sleep 0.3
        echo "/login TestUser"
        sleep 0.3
        echo "/users"
        sleep 0.3
        echo "/quit"
    } | timeout 5 "$CLIENT" 127.0.0.1 $TEST_PORT > "$LOG_DIR/client_connection.log" 2>&1 || true

    if grep -qi "Connected\|Welcome" "$LOG_DIR/client_connection.log"; then
        log_pass "客户端成功连接"
    else
        log_fail "客户端连接失败"
        echo "客户端输出:"
        cat "$LOG_DIR/client_connection.log"
        exit 1
    fi
}

test_login() {
    log_test "测试4: 用户登录功能"

    {
        sleep 0.3
        echo "/login alice"
        sleep 0.5
        echo "/quit"
    } | timeout 5 "$CLIENT" 127.0.0.1 $TEST_PORT > "$LOG_DIR/login_test.log" 2>&1 || true

    if grep -qi "\[OK\]" "$LOG_DIR/login_test.log"; then
        log_pass "登录功能正常"
    else
        log_fail "登录功能失败"
        echo "登录测试输出:"
        cat "$LOG_DIR/login_test.log"
        exit 1
    fi
}

test_user_list() {
    log_test "测试5: 用户列表功能"

    {
        sleep 0.3
        echo "/login bob"
        sleep 0.3
        echo "/users"
        sleep 0.3
        echo "/quit"
    } | timeout 5 "$CLIENT" 127.0.0.1 $TEST_PORT > "$LOG_DIR/users_test.log" 2>&1 || true

    if grep -qi "Online\|bob" "$LOG_DIR/users_test.log"; then
        log_pass "用户列表功能正常"
    else
        log_fail "用户列表功能失败"
        echo "用户列表测试输出:"
        cat "$LOG_DIR/users_test.log"
        exit 1
    fi
}

test_broadcast_message() {
    log_test "测试6: 广播消息功能"

    {
        sleep 0.3
        echo "/login charlie"
        sleep 0.3
        echo "Hello everyone!"
        sleep 0.3
        echo "/quit"
    } | timeout 5 "$CLIENT" 127.0.0.1 $TEST_PORT > "$LOG_DIR/broadcast_test.log" 2>&1 || true

    if grep -qi "charlie.*joined\|Hello everyone" "$LOG_DIR/broadcast_test.log"; then
        log_pass "广播消息功能正常"
    else
        log_fail "广播消息功能失败"
        echo "广播消息测试输出:"
        cat "$LOG_DIR/broadcast_test.log"
        exit 1
    fi
}

test_multiple_clients() {
    log_test "测试7: 多客户端同时在线"

    "$SERVER" $((TEST_PORT+1)) > "$LOG_DIR/server_multi.log" 2>&1 &
    local MULTI_SERVER_PID=$!
    sleep 1

    {
        sleep 0.2
        echo "/login user1"
        sleep 0.2
        echo "/quit"
    } | timeout 3 "$CLIENT" 127.0.0.1 $((TEST_PORT+1)) > "$LOG_DIR/multi_client1.log" 2>&1 || true

    {
        sleep 0.2
        echo "/login user2"
        sleep 0.2
        echo "/quit"
    } | timeout 3 "$CLIENT" 127.0.0.1 $((TEST_PORT+1)) > "$LOG_DIR/multi_client2.log" 2>&1 || true

    kill $MULTI_SERVER_PID 2>/dev/null || true
    wait $MULTI_SERVER_PID 2>/dev/null || true

    if grep -qi "user1.*joined.*user2.*joined\|user2.*joined" "$LOG_DIR/multi_client1.log" "$LOG_DIR/multi_client2.log"; then
        log_pass "多客户端功能正常"
    else
        log_fail "多客户端功能失败"
        echo "多客户端测试输出:"
        cat "$LOG_DIR/multi_client1.log"
        cat "$LOG_DIR/multi_client2.log"
        exit 1
    fi
}

main() {
    echo ""
    echo -e "${GREEN}=========================================${NC}"
    echo -e "${GREEN}  SmallChat 完整功能测试${NC}"
    echo -e "${GREEN}=========================================${NC}"
    echo ""

    check_executables
    test_server_startup
    test_server_running
    test_client_connection
    test_login
    test_user_list
    test_broadcast_message
    test_multiple_clients

    echo ""
    echo -e "${GREEN}=========================================${NC}"
    echo -e "${GREEN}  所有测试通过！${NC}"
    echo -e "${GREEN}=========================================${NC}"
    echo ""
}

main "$@"
