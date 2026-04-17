#!/bin/bash

################################################################################
# SmallChat 运行脚本
#
# 用法: ./run.sh [命令]
#   - 无参数: 交互式选择运行模式
#   - server: 启动服务器
#   - client: 启动客户端
#   - test:   运行测试
#   - help:   显示帮助信息
################################################################################

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 脚本路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/output"

# 日志函数
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_header() {
    echo ""
    echo -e "${BLUE}=========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}=========================================${NC}"
}

# 检查可执行文件
check_executable() {
    local exe=$1
    if [ ! -f "$exe" ]; then
        log_error "找不到 $exe"
        log_error "请先运行 ./build.sh 进行编译"
        exit 1
    fi
}

# 启动服务器
run_server() {
    local server="${OUTPUT_DIR}/smallchat_server"
    check_executable "$server"
    
    log_header "启动 SmallChat 服务器"
    log_info "监听端口: 8888"
    log_info "按 Ctrl+C 停止服务器"
    echo ""
    
    "$server"
}

# 启动客户端
run_client() {
    local client="${OUTPUT_DIR}/smallchat_client"
    check_executable "$client"
    
    log_header "启动 SmallChat 客户端"
    log_info "连接地址: localhost:8888"
    log_info "按 Ctrl+C 退出客户端"
    echo ""
    
    "$client"
}

# 运行测试
run_test() {
    local test_exe="${OUTPUT_DIR}/test_chat"
    check_executable "$test_exe"
    
    log_header "运行 SmallChat 测试"
    echo ""
    
    "$test_exe"
}

# 交互式模式
interactive_mode() {
    log_header "SmallChat - 选择运行模式"
    echo "1) 启动服务器"
    echo "2) 启动客户端"
    echo "3) 运行测试"
    echo "4) 退出"
    echo ""
    
    read -p "请选择 (1-4): " choice
    echo ""
    
    case $choice in
        1)
            run_server
            ;;
        2)
            run_client
            ;;
        3)
            run_test
            ;;
        4)
            log_info "退出"
            ;;
        *)
            log_error "无效的选择"
            exit 1
            ;;
    esac
}

# 帮助信息
usage() {
    echo "用法: $0 [命令]"
    echo ""
    echo "命令:"
    echo "  (无)     - 交互式模式"
    echo "  server   - 启动服务器 (端口: 8888)"
    echo "  client   - 启动客户端 (连接到 localhost:8888)"
    echo "  test     - 运行测试程序"
    echo "  help     - 显示此帮助信息"
}

# 主逻辑
main() {
    case "${1:-}" in
        server)
            run_server
            ;;
        client)
            run_client
            ;;
        test)
            run_test
            ;;
        help|-h|--help)
            usage
            ;;
        "")
            interactive_mode
            ;;
        *)
            log_error "未知的命令: $1"
            echo ""
            usage
            exit 1
            ;;
    esac
}

main "$@"
