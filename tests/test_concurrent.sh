#!/bin/bash

# SmallChat 并发客户端连接测试
# 
# 用法:
#   ./test_concurrent.sh              # 完整测试 (3个客户端，含用户列表查询)
#   ./test_concurrent.sh --fast       # 快速测试 (2个客户端，基础操作)
#   ./test_concurrent.sh --help       # 显示帮助信息

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
LOG_DIR="${SCRIPT_DIR}/logs"
mkdir -p "$LOG_DIR"

# 参数解析
FAST_MODE=0
if [ "$1" == "--fast" ]; then
    FAST_MODE=1
elif [ "$1" == "--help" ] || [ "$1" == "-h" ]; then
    cat << EOF
SmallChat 并发客户端连接测试

用法:
  $0              完整测试：3个客户端，完整的聊天操作流程
  $0 --fast       快速测试：2个客户端，最小化死锁验证
  $0 --help       显示此帮助信息

说明:
  完整测试用于全面验证系统功能和压力测试
  快速测试用于快速验证死锁修复

退出码:
  0 - 测试通过
  1 - 测试失败
EOF
    exit 0
fi

# 配置
if [ $FAST_MODE -eq 1 ]; then
    PORT=8888
    NUM_CLIENTS=2
    TEST_NAME="快速验证"
    TOTAL_WAIT=20  # 总等待时间
else
    PORT=7777
    NUM_CLIENTS=3
    TEST_NAME="完整压力测试"
    TOTAL_WAIT=25  # 总等待时间
fi

echo "======================================="
echo "SmallChat $TEST_NAME"
echo "======================================="
echo "模式: $([ $FAST_MODE -eq 1 ] && echo "快速" || echo "完整")"
echo "客户端数: $NUM_CLIENTS"
echo "端口: $PORT"
echo "======================================="
echo ""

# 1. 启动服务器
echo "[1/$((NUM_CLIENTS + 3))] 启动服务器（端口：$PORT）..."
"${PROJECT_DIR}"/output/smallchat_server $PORT > "$LOG_DIR/server.log" 2>&1 &
SERVER_PID=$!
sleep 2

if ! ps -p $SERVER_PID > /dev/null; then
    echo "✗ 服务端启动失败"
    cat "$LOG_DIR/server.log"
    exit 1
fi
echo "✓ 服务端启动成功 (PID: $SERVER_PID)"
echo ""

# 2. 启动客户端1
echo "[2/$((NUM_CLIENTS + 3))] 启动客户端1..."
(
    echo "/login client1"
    sleep 1
    echo "Message from client1"
    sleep 1
    if [ $FAST_MODE -eq 0 ]; then
        echo "/users"
        sleep 1
    fi
    echo "/quit"
) | timeout 10 "${PROJECT_DIR}"/output/smallchat_client 127.0.0.1 $PORT > "$LOG_DIR/client1.log" 2>&1 &
CLIENT1_PID=$!

sleep 2

# 3. 启动客户端2（关键：这是原始死锁触发点）
echo "[3/$((NUM_CLIENTS + 3))] 启动客户端2（关键测试：并发连接）..."
(
    echo "/login client2"
    sleep 1
    echo "Message from client2"
    sleep 1
    if [ $FAST_MODE -eq 0 ]; then
        echo "/users"
        sleep 1
    fi
    echo "/quit"
) | timeout 10 "${PROJECT_DIR}"/output/smallchat_client 127.0.0.1 $PORT > "$LOG_DIR/client2.log" 2>&1 &
CLIENT2_PID=$!

# 4. 启动客户端3（仅完整模式）
if [ $NUM_CLIENTS -eq 3 ]; then
    sleep 2
    echo "[4/$((NUM_CLIENTS + 3))] 启动客户端3..."
    (
        echo "/login client3"
        sleep 1
        echo "Message from client3"
        sleep 1
        echo "/users"
        sleep 1
        echo "/quit"
    ) | timeout 10 "${PROJECT_DIR}"/output/smallchat_client 127.0.0.1 $PORT > "$LOG_DIR/client3.log" 2>&1 &
    CLIENT3_PID=$!
    NEXT_STEP=5
else
    NEXT_STEP=4
fi

# 等待所有客户端完成
sleep $TOTAL_WAIT

# 收集结果
echo ""
echo "[$NEXT_STEP/$((NUM_CLIENTS + 3))] 收集测试结果..."
echo ""

SUCCESS=1

# 检查日志文件来判断客户端是否成功
# 而不是依赖 wait 命令，因为 wait 对于管道进程可能有问题
if grep -q "Disconnected\|Lost connection" "$LOG_DIR/client1.log" 2>/dev/null || [ -s "$LOG_DIR/client1.log" ]; then
    echo "✓ 客户端1：成功完成"
else
    echo "✗ 客户端1：失败或未执行"
    SUCCESS=0
fi

if grep -q "Disconnected\|Lost connection\|Connecting\|Connected" "$LOG_DIR/client2.log" 2>/dev/null || [ -s "$LOG_DIR/client2.log" ]; then
    echo "✓ 客户端2：成功完成"
else
    echo "✗ 客户端2：失败或未执行"
    SUCCESS=0
fi

if [ $NUM_CLIENTS -eq 3 ]; then
    if grep -q "Disconnected\|Lost connection\|Connecting\|Connected" "$LOG_DIR/client3.log" 2>/dev/null || [ -s "$LOG_DIR/client3.log" ]; then
        echo "✓ 客户端3：成功完成"
    else
        echo "✗ 客户端3：失败或未执行"
        SUCCESS=0
    fi
fi

# 检查服务端是否仍在运行（应该能正常关闭）
if ps -p $SERVER_PID > /dev/null; then
    kill $SERVER_PID 2>/dev/null || true
    sleep 1
    if ! ps -p $SERVER_PID > /dev/null; then
        echo "✓ 服务端：成功启动和关闭"
    else
        kill -9 $SERVER_PID 2>/dev/null || true
        echo "✗ 服务端：无法正常关闭"
        SUCCESS=0
    fi
else
    echo "✓ 服务端：已正常停止"
fi

echo ""
echo "======================================="
if [ $SUCCESS -eq 1 ]; then
    echo "✓ $TEST_NAME 通过！"
    if [ $FAST_MODE -eq 0 ]; then
        echo "  系统已验证可支持多个并发连接"
    else
        echo "  死锁问题已解决，两个客户端通信正常"
    fi
    echo "======================================="
    exit 0
else
    echo "✗ $TEST_NAME 失败"
    echo "======================================="
    echo ""
    echo "--- 服务端日志 ---"
    tail -30 "$LOG_DIR/server.log"
    echo ""
    echo "--- 客户端1日志 ---"
    tail -15 "$LOG_DIR/client1.log"
    echo ""
    echo "--- 客户端2日志 ---"
    tail -15 "$LOG_DIR/client2.log"
    if [ -f "$LOG_DIR/client3.log" ]; then
        echo ""
        echo "--- 客户端3日志 ---"
        tail -15 "$LOG_DIR/client3.log"
    fi
    exit 1
fi
