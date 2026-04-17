# SmallChat 测试脚本指南

本目录包含 SmallChat 项目的所有测试脚本。

## 文件结构

```
tests/
├── README.md              # 本文件
├── logs/                  # 测试日志目录（由测试脚本自动生成）
├── test.sh               # 基础功能测试
└── test_concurrent.sh    # 并发连接测试（支持快速和完整模式）
```

## 测试脚本说明

### test.sh - 基础功能测试
测试服务器启动、客户端连接等基本功能。

```bash
cd /workspaces/smallchat
./tests/test.sh
```

**检查内容：**
- 服务器启动功能
- 客户端连接功能
- 基本消息收发

---

### test_concurrent.sh - 并发连接测试

统一的并发连接测试脚本，支持两种运行模式：

#### 完整测试（默认）
全面验证系统功能和压力测试，3个客户端并发连接。

```bash
cd /workspaces/smallchat
./tests/test_concurrent.sh       # 完整测试
```

**检查内容：**
- 3个客户端同时连接
- 客户端登录、消息发送、用户列表查询、退出
- 服务端是否正常响应和关闭

**端口：** 7777  
**运行时间：** ~20-25秒

---

#### 快速测试
最小化场景快速验证死锁修复，2个客户端连接。

```bash
cd /workspaces/smallchat
./tests/test_concurrent.sh --fast   # 快速测试
```

**检查内容：**
- 2个客户端同时连接（原始死锁的关键点）
- 客户端登录、消息发送、退出
- 验证死锁问题是否已解决

**端口：** 8888  
**运行时间：** ~15-20秒

---

#### 显示帮助信息

```bash
./tests/test_concurrent.sh --help   # 显示使用说明
```

---

## 日志文件

所有测试脚本的日志输出都保存在 `tests/logs/` 目录中：

- `server.log` - 服务器输出
- `client1.log` - 客户端1输出
- `client2.log` - 客户端2输出
- `client3.log` - 客户端3输出（仅完整测试产生）

### 查看日志

```bash
# 查看所有日志
ls -lah tests/logs/

# 查看特定日志
cat tests/logs/server.log
tail -f tests/logs/client1.log

# 实时查看日志
tail -f tests/logs/server.log
```

---

## 一键清理脚本

项目根目录提供 `cleanup.sh` 脚本，用于关闭所有测试进程和清理临时文件。

```bash
./cleanup.sh
```

**功能：**
- ✓ 关闭所有 smallchat_server 进程
- ✓ 关闭所有 smallchat_client 进程
- ✓ 清理 `/tmp` 中的旧日志文件
- ✓ 清理 `tests/logs/` 中的日志文件

---

## 快速使用指南

### 完整测试流程

```bash
cd /workspaces/smallchat

# 1. 编译项目
./build.sh

# 2. 运行测试
./tests/test.sh                    # 基础测试
./tests/test_concurrent.sh         # 完整压力测试
./tests/test_concurrent.sh --fast  # 快速验证

# 3. 清理所有进程和日志
./cleanup.sh
```

### 快速验证死锁修复

```bash
cd /workspaces/smallchat
./build.sh
./tests/test_concurrent.sh --fast  # 快速验证（~20秒）
./cleanup.sh
```

### 全面压力测试

```bash
cd /workspaces/smallchat
./build.sh
./tests/test_concurrent.sh         # 完整测试（~25秒）
./cleanup.sh
```

### 如果出现问题

如果测试过程中服务端响应缓慢或无响应：

```bash
# 立即关闭所有进程和清理日志
./cleanup.sh

# 如果 cleanup.sh 无效，手动杀死进程
pkill -9 -f smallchat

# 查看日志找出问题
cat tests/logs/server.log
tail tests/logs/client*.log
```

---

## 测试结果解释

### ✓ 测试通过
- **快速测试通过：** 死锁问题已解决，两个客户端通信正常
- **完整测试通过：** 系统已验证可支持多个并发连接

### ✗ 测试失败
可能表示：
- 服务端超时无响应
- 客户端连接失败
- 数据收发异常
- 服务端无法正常关闭

查看 `tests/logs/` 目录中的相关日志文件确定问题原因。

---

## 脚本对比

| 特性 | test.sh | test_concurrent.sh | test_concurrent.sh --fast |
|-----|---------|------------------|--------------------------|
| 客户端数 | 1 | 3 | 2 |
| 测试消息发送 | ✓ | ✓ | ✓ |
| 测试用户列表 | ✗ | ✓ | ✗ |
| 测试死锁修复 | ✗ | ✓ | ✓ |
| 运行时间 | ~10秒 | ~25秒 | ~20秒 |
| 目的 | 基础验证 | 压力测试 | 快速验证 |

---

## 修复说明

原始问题：服务端在处理第二个客户端连接时因互斥锁死锁而无响应。

修复方案：
1. 将 `clients_mutex_` 从 `std::mutex` 改为 `std::recursive_mutex`
2. 重新组织 `stop()` 函数，避免在持有锁的情况下等待线程

详见项目文档：[docs/01_architecture.md](../docs/01_architecture.md)

---

## 环境要求

- Linux/macOS
- Bash shell
- 已编译的可执行文件（运行 `./build.sh` 生成）

## 故障排除

| 问题 | 解决方案 |
|------|--------|
| 脚本无执行权限 | `chmod +x tests/test*.sh` |
| 找不到可执行文件 | 运行 `./build.sh` 编译项目 |
| 端口被占用 | 运行 `./cleanup.sh` 关闭旧进程 |
| 日志文件堆积 | 运行 `./cleanup.sh` 清理旧日志 |
| 快速测试使用的端口冲突 | 指定不同端口或等待前一个测试完成 |

---

*最后更新: 2026-04-17*
