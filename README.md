# SmallChat - 小型聊天服务器

一个简洁但功能完整的TCP聊天服务器，支持多客户端连接、消息广播、私聊等功能。

## 功能特性

### 服务器功能
- ✅ **多客户端连接**: 支持多个客户端同时连接
- ✅ **消息广播**: 支持向所有在线用户广播消息
- ✅ **私聊功能**: 支持点对点私聊
- ✅ **用户登录**: 支持用户名登录和身份验证
- ✅ **用户列表**: 查看在线用户列表
- ✅ **系统消息**: 系统通知和欢迎消息
- ✅ **回调机制**: 支持连接、断开、消息回调
- ✅ **线程安全**: 使用互斥锁保证线程安全

### 客户端功能
- ✅ **TCP连接**: 基于TCP的可靠连接
- ✅ **消息发送**: 支持发送普通消息和私聊消息
- ✅ **消息接收**: 异步接收服务器消息
- ✅ **命令支持**: 支持/login、/users、/whisper等命令
- ✅ **回调机制**: 支持消息、系统消息、错误回调
- ✅ **多线程**: 独立的消息接收线程

## 编译

### Linux/macOS

```bash
mkdir build
cd build
cmake ..
make
```

### Windows

```bash
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

## 使用方法

### 启动服务器

```bash
# 默认端口 8888
./smallchat_server

# 指定端口
./smallchat_server 9999
```

服务器输出示例：
```
Starting SmallChat Server on port 8888...
Server is running. Press Ctrl+C to stop.
Client connected: 127.0.0.1:52341 (fd: 4)
Client connected: 192.168.1.100:52342 (fd: 5)
Online clients: 2
```

### 启动客户端

```bash
# 连接到本地服务器
./smallchat_client

# 连接到指定服务器
./smallchat_client 192.168.1.100 8888
```

客户端交互示例：
```
Connecting to 127.0.0.1:8888...
Connected!
[SYS] Welcome to SmallChat Server!
[SYS] Commands: /login <name>, /users, /whisper <name> <msg>, /help, /quit

> /login Alice
[SYS] [OK] Logged in as Alice

> /users
[Users] Online: Alice, Bob

> Hello everyone!
[SYS] Alice joined the chat

> /whisper Bob Hi Bob!
[Private] Alice -> Bob: Hi Bob!

> /quit
[SYS] Goodbye!
Disconnected.
```

## 命令列表

### 服务器命令（运行时）
- `Ctrl+C`: 停止服务器

### 客户端命令
- `/login <name>`: 使用指定名字登录
- `/users`: 查看在线用户列表
- `/whisper <name> <message>`: 发送私聊消息
- `/help`: 显示帮助信息
- `/quit`: 断开连接并退出
- `<message>`: 发送广播消息（普通输入）

## 协议格式

### 消息格式

所有消息以 `\n` 结尾，字段以 `|` 分隔：

#### 普通消息 (MSG)
```
MSG|sender|receiver|type|content\n
```

- `type`: 0=BROADCAST, 1=PRIVATE, 2=TEXT, 3=SYSTEM

#### 系统消息 (SYS)
```
SYS|message\n
```

#### 用户列表 (USERS)
```
USERS|count\n
user1\n
user2\n
...
```

#### 错误消息 (ERROR)
```
ERROR|error_message\n
```

#### 成功消息 (OK)
```
OK|message\n
```

#### 欢迎消息 (WELCOME)
```
WELCOME\n
message\n
commands\n
```

## API 使用示例

### 服务器 API

```cpp
#include "server.h"

// 创建服务器
ChatServer server;

// 设置回调
server.setMessageCallback([](const ChatMessage& msg) {
    std::cout << msg.sender << ": " << msg.content << std::endl;
});

server.setConnectCallback([](const ClientInfo& client) {
    std::cout << "Client connected: " << client.name << std::endl;
});

// 启动服务器
server.start(8888);

// 发送系统消息
server.sendSystemMessage("Server maintenance at midnight");

// 发送私聊消息
server.sendToClient("Alice", "Hello Alice!");

// 广播消息
server.broadcast("Hello everyone!");

// 停止服务器
server.stop();
```

### 客户端 API

```cpp
#include "client.h"

// 创建客户端
ChatClient client;

// 连接服务器
client.connect("127.0.0.1", 8888);

// 设置回调
client.setMessageCallback([](const ChatMessage& msg) {
    std::cout << msg.sender << ": " << msg.content << std::endl;
});

client.setSystemMessageCallback([](const std::string& msg) {
    std::cout << "[SYS] " << msg << std::endl;
});

// 启动接收线程
client.start();

// 登录
client.login("Alice");

// 发送消息
client.sendMessage("Hello everyone!");

// 发送私聊
client.sendPrivateMessage("Bob", "Hi Bob!");

// 请求用户列表
client.requestUserList();

// 停止
client.stop();
client.disconnect();
```

## 技术架构

### 服务器架构
```
┌─────────────────────────────────┐
│        Main Thread              │
│  (Server Socket, Accept)        │
└──────────────┬──────────────────┘
               │
               ├──────────────┐
               │              │
               ▼              ▼
    ┌────────────────┐  ┌────────────────┐
    │  Client Thread 1│  │  Client Thread 2│
    │  (Handle I/O)   │  │  (Handle I/O)   │
    └────────────────┘  └────────────────┘
               │              │
               └──────┬───────┘
                      ▼
            ┌──────────────────┐
            │  Shared Clients  │
            │  (Mutex Lock)    │
            └──────────────────┘
```

### 客户端架构
```
┌─────────────────────────────────┐
│       Main Thread              │
│  (User Input, Send Messages)    │
└──────────────┬──────────────────┘
               │
               ├────────────────┐
               │                │
               ▼                ▼
    ┌────────────────┐   ┌────────────────┐
    │ Receive Thread│   │   Server       │
    │  (Async I/O)   │   │   Socket       │
    └────────────────┘   └────────────────┘
               │
               ▼
        ┌───────────────┐
        │  Message Queue│
        │  (Callbacks)  │
        └───────────────┘
```

## 性能特点

- **轻量级**: 最小资源占用
- **低延迟**: 直接TCP连接，无中间层
- **可扩展**: 支持数百个并发连接
- **线程安全**: 使用互斥锁保护共享数据

## 系统要求

### 服务器
- Linux / macOS / Windows
- GCC 7.0+ / Clang 5.0+ / MSVC 2017+
- pthread库

### 客户端
- 与服务器相同

## 待实现功能

- [ ] SSL/TLS加密
- [ ] 用户认证（密码）
- [ ] 聊天室/群组
- [ ] 文件传输
- [ ] 历史消息
- [ ] 消息持久化
- [ ] Web界面
- [ ] 管理员权限
- [ ] 禁言/踢人
- [ ] 表情支持
- [ ] 消息已读回执

## 许可证

MIT License

## 贡献

欢迎提交Issue和Pull Request！
