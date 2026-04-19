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
- ✅ **SSL/TLS加密**: 使用OpenSSL实现安全通信
- ✅ **用户认证（密码）**: 支持密码注册和登录
- ✅ **聊天室/群组**: 支持创建、加入、离开聊天室
- ✅ **文件传输**: 支持发送文件给用户和聊天室
- ✅ **历史消息**: 存储和获取消息历史
- ✅ **消息持久化**: 将消息保存到文件，服务器重启后恢复
- ✅ **Web界面**: 基于WebSocket的实时Web聊天界面
- ✅ **管理员权限**: 支持管理员特权操作
- ✅ **禁言/踢人**: 管理员可以禁言和踢人
- ✅ **表情支持**: 支持文本表情自动转换为emoji
- ✅ **消息已读回执**: 跟踪消息已读状态并通知发送者

### 客户端功能

- ✅ **TCP连接**: 基于TCP的可靠连接
- ✅ **消息发送**: 支持发送普通消息和私聊消息
- ✅ **消息接收**: 异步接收服务器消息
- ✅ **命令支持**: 支持/login、/users、/whisper等命令
- ✅ **回调机制**: 支持消息、系统消息、错误回调
- ✅ **多线程**: 独立的消息接收线程
- ✅ **文件传输**: 支持接收和发送文件
- ✅ **表情支持**: 支持文本表情自动转换为emoji
- ✅ **消息已读回执**: 支持发送已读回执

### Web界面功能

- ✅ **实时聊天**: 基于WebSocket的实时通信
- ✅ **响应式设计**: 适配不同屏幕尺寸
- ✅ **用户认证**: 支持登录和注册
- ✅ **聊天室管理**: 创建、加入、离开聊天室
- ✅ **文件传输**: 支持发送文件
- ✅ **历史消息**: 查看历史消息
- ✅ **表情支持**: 支持表情符号
- ✅ **消息已读回执**: 显示消息已读状态

## 编译

### 依赖要求
- CMake 3.16+
- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- OpenSSL (for SSL/TLS encryption)
- libwebsockets (for WebSocket support)

### Linux/macOS

```bash
# 安装依赖（Ubuntu/Debian）
sudo apt-get install libssl-dev libwebsockets-dev

# 安装依赖（macOS）
brew install openssl libwebsockets

# 编译
mkdir build
cd build
cmake ..
make
```

### Windows

```bash
# 安装依赖
# 1. 下载并安装 OpenSSL: https://slproweb.com/products/Win32OpenSSL.html
# 2. 下载并安装 libwebsockets: https://libwebsockets.org/

# 编译
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -DOPENSSL_ROOT_DIR=<path_to_openssl>
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

### 启动 WebSocket 服务器

```bash
# 默认端口 8080
./smallchat_web_server

# 访问 Web 界面
# 打开浏览器访问: http://localhost:8080
```

Web 服务器会自动启动内部 TCP 服务器（端口 8888），无需单独启动 smallchat_server。

### 启动客户端

```bash
# 连接到本地服务器
./smallchat_client

# 连接到指定服务器
./smallchat_client 192.168.1.100 8888
```

客户端交互示例：
```
Welcome to SmallChat! Please login first.
Use: /login <name> or /register <name> <password>
[SYS] Welcome to SmallChat Server!
> /register Alice password123
[OK] User registered successfully: Alice
[Alice] > /login Alice password123
[Alice] > Hello everyone!
[Alice]: Hello everyone!
[Alice] > /users
[Users] Online: Alice, Bob
[Alice] > /q
Disconnecting...
Disconnected.
```

## 命令列表

### 服务器命令（运行时）
- `Ctrl+C`: 停止服务器

### 客户端命令
- `/login <name> [password]` (`/l`): 使用指定名字和密码登录
- `/register <name> <password>` (`/r`): 注册新用户
- `/users` (`/u`): 查看在线用户列表
- `/whisper <name> <message>` (`/w`): 发送私聊消息
- `/help` (`/h`): 显示帮助信息
- `/quit` (`/q`): 断开连接并退出
- `/nick <name>` (`/n`): 更改昵称
- `/info`: 显示服务器信息
- `/createroom <name> [private] [password]` (`/cr`): 创建聊天室
- `/joinroom <name> [password]` (`/jr`): 加入聊天室
- `/leaveroom` (`/lr`): 离开当前聊天室
- `/rooms` (`/ro`): 列出所有聊天室
- `/roommembers` (`/rm`): 列出当前聊天室成员
- `/transfer <name> <file>` (`/t`): 向指定用户发送文件
- `/file <file>`: 向当前聊天室发送文件
- `/history [count]`: 获取消息历史
- `/roomhistory [count]`: 获取当前聊天室消息历史
- `/mute <name> [time]`: 禁言用户（管理员权限）
- `/unmute <name>`: 解除禁言（管理员权限）
- `/kick <name> [reason]`: 踢人（管理员权限）
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

#### 已读回执消息 (READ_RECEIPT)

```
READ_RECEIPT|message_id|user\n
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

- [ ] 多语言支持
- [ ] 消息加密
- [ ] 消息撤回
- [ ] 消息编辑
- [ ] 用户状态（在线/离线/忙碌）
- [ ] 头像支持
- [ ] 语音消息
- [ ] 视频通话
- [ ] 消息搜索
- [ ] 消息置顶
- [ ] 消息标记
- [ ] 群组管理员
- [ ] 群组公告
- [ ] 邀请加入群组
- [ ] 退出群组确认
- [ ] 防刷屏机制
- [ ] 消息速率限制
- [ ] 服务器集群
- [ ] 负载均衡
- [ ] 数据库支持
- [ ] 缓存优化

## 许可证

MIT License

## 贡献

欢迎提交Issue和Pull Request！
