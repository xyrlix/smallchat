#include "client.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#ifdef OPENSSL_FOUND
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include <cstring>
#include <iostream>
#include <sstream>

namespace smallchat {

//==============================================================================
// ChatClient 实现
//==============================================================================

ChatClient::ChatClient()
    : socket_fd_(-1), port_(0), connected_(false), running_(false)
#ifdef OPENSSL_FOUND
    , ssl_enabled_(false), ssl_ctx_(nullptr), ssl_(nullptr)
#endif
{
#ifdef _WIN32
    // 初始化Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        // 初始化失败
    }
#endif
#ifdef OPENSSL_FOUND
    // 初始化OpenSSL
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
#endif
}

ChatClient::~ChatClient() {
    disconnect();
#ifdef OPENSSL_FOUND
    cleanupSSL();
#endif
#ifdef _WIN32
    // 清理Winsock
    WSACleanup();
#endif
}

bool ChatClient::connect(const std::string& host, uint16_t port) {
    // 创建socket
#ifdef _WIN32
    socket_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd_ == INVALID_SOCKET) {
        return false;
    }
#else
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        return false;
    }
#endif
    
    // 连接服务器
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
#ifdef _WIN32
        closesocket(socket_fd_);
        socket_fd_ = INVALID_SOCKET;
#else
        close(socket_fd_);
        socket_fd_ = -1;
#endif
        return false;
    }
    
#ifdef _WIN32
    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        closesocket(socket_fd_);
        socket_fd_ = INVALID_SOCKET;
        return false;
    }
#else
    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
#endif
    
    // 设置socket为非阻塞模式，以便在退出时能及时检测
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(socket_fd_, FIONBIO, &mode);
#else
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
#endif
    
#ifdef OPENSSL_FOUND
    // 如果启用了SSL，进行SSL握手
    if (ssl_enabled_) {
        if (!initSSL()) {
            disconnect();
            return false;
        }
        
        ssl_ = SSL_new(ssl_ctx_);
        if (!ssl_) {
            disconnect();
            return false;
        }
        
        if (!SSL_set_fd(ssl_, socket_fd_)) {
            SSL_free(ssl_);
            ssl_ = nullptr;
            disconnect();
            return false;
        }
        
        if (SSL_connect(ssl_) <= 0) {
            SSL_free(ssl_);
            ssl_ = nullptr;
            disconnect();
            return false;
        }
    }
#endif
    
    host_ = host;
    port_ = port;
    connected_ = true;
    
    return true;
}

void ChatClient::disconnect() {
    if (!connected_) return;
    
    connected_ = false;
    
    // 先关闭socket，唤醒阻塞的recv
#ifdef OPENSSL_FOUND
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
#endif
    
    // 关闭socket
    if (socket_fd_ >= 0) {
#ifdef _WIN32
        closesocket(socket_fd_);
        socket_fd_ = INVALID_SOCKET;
#else
        close(socket_fd_);
        socket_fd_ = -1;
#endif
    }
}

bool ChatClient::isConnected() const {
    return connected_;
}

bool ChatClient::login(const std::string& name) {
    if (!connected_) return false;
    
    std::string cmd = "/login " + name;
    return send(cmd);
}

bool ChatClient::sendMessage(const std::string& message) {
    if (!connected_) return false;
    return send(message);
}

bool ChatClient::sendPrivateMessage(const std::string& receiver, const std::string& message) {
    if (!connected_) return false;
    
    std::string cmd = "/whisper " + receiver + " " + message;
    return send(cmd);
}

bool ChatClient::requestUserList() {
    if (!connected_) return false;
    return send("/users");
}

size_t ChatClient::getUserCount() const {
    // 这个需要服务器支持
    return 0;
}

void ChatClient::setMessageCallback(MessageCallback callback) {
    message_callback_ = callback;
}

void ChatClient::setSystemMessageCallback(SystemMessageCallback callback) {
    system_callback_ = callback;
}

void ChatClient::setUserListCallback(UserListCallback callback) {
    userlist_callback_ = callback;
}

void ChatClient::setErrorCallback(ErrorCallback callback) {
    error_callback_ = callback;
}

void ChatClient::setSuccessCallback(SystemMessageCallback callback) {
    success_callback_ = callback;
}

void ChatClient::start() {
    if (running_) return;
    
    running_ = true;
    receive_thread_ = std::thread(&ChatClient::receiveThread, this);
}

void ChatClient::stop() {
    if (!running_) return;
    
    running_ = false;
    
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
}

void ChatClient::receiveThread() {
    char buffer[4096];
    std::string received_data;
    
    while (running_ && connected_) {
        // 使用select等待数据，设置1秒超时，以便能及时检测退出信号
#ifdef _WIN32
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(socket_fd_, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int ret = select(0, &read_fds, nullptr, nullptr, &timeout);
        if (ret <= 0) {
            // 超时或错误，检查是否需要退出
            if (!running_ || !connected_) {
                break;
            }
            continue;
        }
#else
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(socket_fd_, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int ret = select(socket_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
        if (ret <= 0) {
            if (!running_ || !connected_) {
                break;
            }
            continue;
        }
#endif
        
        int bytes_read = 0;
        
#ifdef OPENSSL_FOUND
        // 检查是否使用SSL
        if (ssl_enabled_ && ssl_) {
            bytes_read = SSL_read(ssl_, buffer, sizeof(buffer) - 1);
        } else {
#endif
            // 普通socket接收
#ifdef _WIN32
            bytes_read = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
#else
            bytes_read = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
#endif
#ifdef OPENSSL_FOUND
        }
#endif
        
        if (bytes_read <= 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAEAGAIN) {
                // 非阻塞模式下没有数据，继续循环
                continue;
            }
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
#endif
            // 连接真正断开
            if (error_callback_) {
                error_callback_("Connection lost");
            }
            disconnect();
            return;
        }
        
        buffer[bytes_read] = '\0';
        received_data += buffer;
        
        // 处理接收到的消息，按消息分隔符分割
        size_t pos = 0;
        while ((pos = received_data.find(ChatProtocol::MSG_SEPARATOR)) != std::string::npos) {
            std::string message = received_data.substr(0, pos);
            received_data = received_data.substr(pos + ChatProtocol::MSG_SEPARATOR.length());
            
            if (!message.empty()) {
                processMessage(message);
            }
        }
    }
}

void ChatClient::processMessage(const std::string& data) {
    // 处理MSG类型消息
    if (data.substr(0, 3) == "MSG") {
        ChatMessage chat_msg;
        if (ChatProtocol::decode(data, chat_msg)) {
            if (message_callback_) {
                message_callback_(chat_msg);
            }
        }
        return;
    }
    
    // 处理文件传输消息
    if (data.substr(0, 13) == "FILE_TRANSFER") {
        std::cout << "[FILE] Incoming file transfer request" << std::endl;
        return;
    }
    
    // 处理文件请求消息
    if (data.substr(0, 12) == "FILE_REQUEST") {
        std::cout << "[FILE] Incoming file request" << std::endl;
        return;
    }
    
    // 处理文件响应消息
    if (data.substr(0, 13) == "FILE_RESPONSE") {
        std::cout << "[FILE] File transfer response received" << std::endl;
        return;
    }
    
    // 处理文件数据消息
    if (data.substr(0, 9) == "FILE_DATA") {
        std::cout << "[FILE] Receiving file data" << std::endl;
        return;
    }
    
    // 处理已读回执消息
    if (data.substr(0, 12) == "READ_RECEIPT") {
        return;
    }
    
    // 处理其他类型消息（使用分隔符解析）
    size_t start = 0;
    size_t end = data.find(ChatProtocol::FIELD_SEPARATOR, start);
    if (end == std::string::npos) return;
    
    std::string type = data.substr(start, end - start);
    start = end + ChatProtocol::FIELD_SEPARATOR.length();
    
    if (type == "WELCOME") {
        // 欢迎消息
        end = data.find(ChatProtocol::FIELD_SEPARATOR, start);
        if (end == std::string::npos) return;
        std::string welcome_msg = data.substr(start, end - start);
        if (system_callback_) {
            system_callback_("[SYS] " + welcome_msg);
        } else {
            std::cout << "[SYS] " << welcome_msg << std::endl;
        }
        start = end + ChatProtocol::FIELD_SEPARATOR.length();
        
        end = data.find(ChatProtocol::FIELD_SEPARATOR, start);
        if (end == std::string::npos) return;
        std::string commands = data.substr(start, end - start);
        if (system_callback_) {
            system_callback_("[SYS] " + commands);
        } else {
            std::cout << "[SYS] " << commands << std::endl;
        }
    } else if (type == "OK") {
        // 成功消息 - 读取到字符串末尾（因为后面直接是换行符，不是字段分隔符）
        std::string msg = data.substr(start);
        std::cout << "[DEBUG] Received OK message: " << msg << std::endl;
        if (success_callback_) {
            success_callback_(msg);
        } else if (system_callback_) {
            system_callback_("[OK] " + msg);
        } else {
            std::cout << "[OK] " << msg << std::endl;
        }
    } else if (type == "ERROR") {
        // 错误消息 - 读取到字符串末尾（因为后面直接是换行符，不是字段分隔符）
        std::string error = data.substr(start);
        std::cout << "[DEBUG] Received ERROR message: " << error << std::endl;
        if (error_callback_) {
            error_callback_(error);
        } else {
            std::cerr << "[ERROR] " << error << std::endl;
        }
    } else if (type == "SYS") {
        // 系统消息
        end = data.find(ChatProtocol::FIELD_SEPARATOR, start);
        if (end == std::string::npos) return;
        std::string msg = data.substr(start, end - start);
        if (system_callback_) {
            system_callback_("[SYS] " + msg);
        } else {
            std::cout << "[SYS] " << msg << std::endl;
        }
    } else if (type == "USERS") {
        // 用户列表
        end = data.find(ChatProtocol::FIELD_SEPARATOR, start);
        if (end == std::string::npos) return;
        size_t count = std::stoul(data.substr(start, end - start));
        start = end + ChatProtocol::FIELD_SEPARATOR.length();
        
        std::vector<std::string> users;
        for (size_t i = 0; i < count; i++) {
            end = data.find(ChatProtocol::FIELD_SEPARATOR, start);
            if (end == std::string::npos) break;
            users.push_back(data.substr(start, end - start));
            start = end + ChatProtocol::FIELD_SEPARATOR.length();
        }
        
        if (userlist_callback_) {
            userlist_callback_(users);
        }
    }
}

// SSL相关方法
#ifdef OPENSSL_FOUND
bool ChatClient::initSSL() {
    if (ssl_ctx_) {
        return true;
    }
    
    ssl_ctx_ = SSL_CTX_new(SSLv23_client_method());
    if (!ssl_ctx_) {
        return false;
    }
    
    SSL_CTX_set_options(ssl_ctx_, SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    return true;
}

void ChatClient::cleanupSSL() {
    if (ssl_) {
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
}

void ChatClient::enableSSL(bool enable) {
    ssl_enabled_ = enable;
}

bool ChatClient::isSSLEnabled() const {
    return ssl_enabled_;
}
#endif

bool ChatClient::send(const std::string& data) {
    if (!connected_) return false;
    
    std::string message = data + ChatProtocol::MSG_SEPARATOR;
    
#ifdef OPENSSL_FOUND
    // 检查是否使用SSL
    if (ssl_enabled_ && ssl_) {
        int sent = SSL_write(ssl_, message.c_str(), message.length());
        return sent == static_cast<int>(message.length());
    }
#endif
    
    // 普通socket发送
#ifdef _WIN32
    int sent = ::send(socket_fd_, message.c_str(), static_cast<int>(message.length()), 0);
    return sent == static_cast<int>(message.length());
#else
    ssize_t sent = ::send(socket_fd_, message.c_str(), message.length(), 0);
    return sent == static_cast<ssize_t>(message.length());
#endif
}

bool ChatClient::receive(std::string& data) {
    char buffer[4096];
    int bytes_read = 0;
    
#ifdef OPENSSL_FOUND
    // 检查是否使用SSL
    if (ssl_enabled_ && ssl_) {
        bytes_read = SSL_read(ssl_, buffer, sizeof(buffer) - 1);
    } else {
#endif
        // 普通socket接收
#ifdef _WIN32
        bytes_read = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
#else
        bytes_read = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
#endif
#ifdef OPENSSL_FOUND
    }
#endif
    
    if (bytes_read <= 0) {
        return false;
    }
    
    buffer[bytes_read] = '\0';
    data = buffer;
    return true;
}

} // namespace smallchat