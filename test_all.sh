#!/bin/bash

# 测试脚本

# 启动服务器
./output/smallchat_server 8888 > /tmp/server.log 2>&1 &
SERVER_PID=$!
sleep 1

# 测试客户端1
{
    echo "/login alice"
    sleep 0.5
    echo "Hello everyone!"
    sleep 0.5
    echo "/users"
    sleep 0.5
    echo "/quit"
} | ./output/smallchat_client 127.0.0.1 8888 > /tmp/client1.log 2>&1

sleep 1

# 测试客户端2
{
    echo "/login bob"
    sleep 0.5
    echo "/w alice Hi Alice!"
    sleep 0.5
    echo "/users"
    sleep 0.5
    echo "/quit"
} | ./output/smallchat_client 127.0.0.1 8888 > /tmp/client2.log 2>&1

sleep 1

# 停止服务器
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

# 显示测试结果
echo "=== Server Log ==="
tail -20 /tmp/server.log
echo ""
echo "=== Client 1 Output ==="
tail -20 /tmp/client1.log
echo ""
echo "=== Client 2 Output ==="
tail -20 /tmp/client2.log
echo ""
echo "=== Test Finished ==="

# 清理
rm /tmp/server.log /tmp/client1.log /tmp/client2.log 2>/dev/null
