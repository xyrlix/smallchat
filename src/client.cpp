#include "client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>

namespace smallchat {

//==============================================================================
// ChatClient 实现
//==============================================================================

ChatClient::ChatClient()
    : socket_fd_(-1), port_(0), connected_(false), running_(false) {
}

ChatClient::~ChatClient() {
    disconnect();
}

bool ChatClient::connect(const std::string& host, uint16_t port) {
    // 创建socket
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        return false;
    }
    
    // 连接服务器
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        close(socket_fd_);
        return false;
    }
    
    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(socket_fd_);
        return false;
    }
    
    host_ = host;
    port_ = port;
    connected_ = true;
    
    return true;
}

void ChatClient::disconnect() {
    if (!connected_) return;
    
    running_ = false;
    connected_ = false;
    
    // 发送退出命令
    send("/quit");
    
    // 关闭socket
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    
    // 等待线程结束
    if (receive_thread_.joinable()) {
        receive_thread_.join();
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
    
    while (running_ && connected_) {
        ssize_t bytes_read = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_read <= 0) {
            if (error_callback_) {
                error_callback_("Connection lost");
            }
            disconnect();
            return;
        }
        
        buffer[bytes_read] = '\0';
        std::string data(buffer);
        
        // 处理接收到的消息
        processMessage(data);
    }
}

void ChatClient::processMessage(const std::string& data) {
    // 处理消息时，我们需要按消息类型来处理
    size_t start = 0;
    size_t end;
    
    while (start < data.length()) {
        // 首先检查是否是MSG类型消息（因为它的格式特殊）
        if (data.substr(start, 3) == "MSG") {
            // 处理MSG类型消息
            end = data.find(ChatProtocol::MSG_SEPARATOR, start);
            if (end == std::string::npos) break;
            
            // 提取完整的MSG消息，从start到end
            std::string msg_data = data.substr(start, end - start);
            
            ChatMessage chat_msg;
            if (ChatProtocol::decode(msg_data, chat_msg)) {
                if (message_callback_) {
                    message_callback_(chat_msg);
                }
            }
            
            start = end + ChatProtocol::MSG_SEPARATOR.length();
        } else {
            // 处理其他类型消息
            end = data.find(ChatProtocol::MSG_SEPARATOR, start);
            if (end == std::string::npos) break;
            
            std::string type = data.substr(start, end - start);
            start = end + ChatProtocol::MSG_SEPARATOR.length();
            
            if (type == "WELCOME") {
                // 欢迎消息
                end = data.find(ChatProtocol::MSG_SEPARATOR, start);
                if (end == std::string::npos) break;
                if (system_callback_) {
                    system_callback_(data.substr(start, end - start));
                }
                start = end + ChatProtocol::MSG_SEPARATOR.length();
                
                end = data.find(ChatProtocol::MSG_SEPARATOR, start);
                if (end == std::string::npos) break;
                if (system_callback_) {
                    system_callback_(data.substr(start, end - start));
                }
                start = end + ChatProtocol::MSG_SEPARATOR.length();
            } else if (type == "OK") {
                // 成功消息
                end = data.find(ChatProtocol::MSG_SEPARATOR, start);
                if (end == std::string::npos) break;
                if (system_callback_) {
                    system_callback_("[OK] " + data.substr(start, end - start));
                }
                start = end + ChatProtocol::MSG_SEPARATOR.length();
            } else if (type == "ERROR") {
                // 错误消息
                end = data.find(ChatProtocol::MSG_SEPARATOR, start);
                if (end == std::string::npos) break;
                if (error_callback_) {
                    error_callback_(data.substr(start, end - start));
                }
                start = end + ChatProtocol::MSG_SEPARATOR.length();
            } else if (type == "SYS") {
                // 系统消息
                end = data.find(ChatProtocol::MSG_SEPARATOR, start);
                if (end == std::string::npos) break;
                if (system_callback_) {
                    system_callback_(data.substr(start, end - start));
                }
                start = end + ChatProtocol::MSG_SEPARATOR.length();
            } else if (type == "USERS") {
                // 用户列表
                end = data.find(ChatProtocol::MSG_SEPARATOR, start);
                if (end == std::string::npos) break;
                size_t count = std::stoul(data.substr(start, end - start));
                start = end + ChatProtocol::MSG_SEPARATOR.length();
                
                std::vector<std::string> users;
                for (size_t i = 0; i < count; i++) {
                    end = data.find(ChatProtocol::MSG_SEPARATOR, start);
                    if (end == std::string::npos) break;
                    users.push_back(data.substr(start, end - start));
                    start = end + ChatProtocol::MSG_SEPARATOR.length();
                }
                
                if (userlist_callback_) {
                    userlist_callback_(users);
                }
            }
        }
    }
}

bool ChatClient::send(const std::string& data) {
    if (!connected_) return false;
    
    std::string message = data + ChatProtocol::MSG_SEPARATOR;
    ssize_t sent = ::send(socket_fd_, message.c_str(), message.length(), 0);
    
    return sent == static_cast<ssize_t>(message.length());
}

bool ChatClient::receive(std::string& data) {
    char buffer[4096];
    ssize_t bytes_read = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read <= 0) {
        return false;
    }
    
    buffer[bytes_read] = '\0';
    data = buffer;
    return true;
}

} // namespace smallchat
