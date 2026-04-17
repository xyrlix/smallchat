#!/bin/bash

################################################################################
# SmallChat 编译脚本
# 
# 用法: ./build.sh [选项]
#   - 无参数: 编译项目
#   - clean:  清理编译结果
#   - help:   显示帮助信息
#
# 输出: output/ 目录下的可执行文件
################################################################################

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 脚本路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
OUTPUT_DIR="${SCRIPT_DIR}/output"

# 日志函数
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_header() {
    echo ""
    echo -e "${GREEN}=========================================${NC}"
    echo -e "${GREEN}$1${NC}"
    echo -e "${GREEN}=========================================${NC}"
}

# 清理函数
clean() {
    log_header "清理编译结果"
    if [ -d "$BUILD_DIR" ]; then
        log_info "删除 build 目录..."
        rm -rf "$BUILD_DIR"
    fi
    if [ -d "$OUTPUT_DIR" ]; then
        log_info "删除 output 目录..."
        rm -rf "$OUTPUT_DIR"
    fi
    log_info "清理完成！"
}

# 编译函数
build() {
    log_header "SmallChat 编译"
    
    # 创建build目录
    if [ ! -d "$BUILD_DIR" ]; then
        log_info "创建 build 目录..."
        mkdir -p "$BUILD_DIR"
    fi
    
    # 创建output目录
    if [ ! -d "$OUTPUT_DIR" ]; then
        log_info "创建 output 目录..."
        mkdir -p "$OUTPUT_DIR"
    fi
    
    # 进入build目录
    cd "$BUILD_DIR"
    
    # 运行cmake，指定输出目录
    log_info "运行 cmake..."
    cmake .. -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$OUTPUT_DIR"
    
    # 编译
    log_info "编译项目..."
    make -j"$(nproc)" 2>&1
    
    cd "$SCRIPT_DIR"
    
    log_header "编译完成！"
    echo ""
    log_info "可执行文件位置:"
    echo "  服务器: ${OUTPUT_DIR}/smallchat_server"
    echo "  客户端: ${OUTPUT_DIR}/smallchat_client"
    echo "  测试:   ${OUTPUT_DIR}/test_chat"
    echo ""
    log_info "运行命令:"
    echo "  启动服务器: ./output/smallchat_server"
    echo "  启动客户端: ./output/smallchat_client"
    echo ""
}

# 帮助信息
usage() {
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  (无)   - 编译项目，输出到 output 目录"
    echo "  clean  - 清理编译结果"
    echo "  help   - 显示此帮助信息"
}

# 主逻辑
main() {
    case "${1:-}" in
        clean)
            clean
            ;;
        help|-h|--help)
            usage
            ;;
        *)
            build
            ;;
    esac
}

main "$@"
