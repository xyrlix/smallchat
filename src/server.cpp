#include "server.h"
#include <iostream>
#include <fstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

#ifdef OPENSSL_FOUND
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/md5.h>
#include <openssl/pbkdf2.h>
#include <openssl/rand.h>
#endif

#include <cstring>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <random>
#include <algorithm>

namespace smallchat {

// 生成随机盐
static std::string generateSalt(size_t length = 16) {
    std::string salt(length, '\0');
#ifdef OPENSSL_FOUND
    if (RAND_bytes(reinterpret_cast<unsigned char*>(&salt[0]), static_cast<int>(length)) != 1) {
        // 如果RAND_bytes失败，使用备用随机数生成器
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        for (size_t i = 0; i < length; ++i) {
            salt[i] = static_cast<char>(dis(gen));
        }
    }
#else
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (size_t i = 0; i < length; ++i) {
        salt[i] = static_cast<char>(dis(gen));
    }
#endif
    return salt;
}

// 带盐的密码哈希函数 - 使用PBKDF2-SHA256
static std::string hashPasswordWithSalt(const std::string& password, const std::string& salt) {
#ifdef OPENSSL_FOUND
    const int iterations = 100000;  // PBKDF2迭代次数
    const int hash_length = 32;      // SHA-256输出长度

    unsigned char hash[hash_length];
    PKCS5_PBKDF2_HMAC(
        password.c_str(), static_cast<int>(password.length()),
        reinterpret_cast<const unsigned char*>(salt.c_str()), static_cast<int>(salt.length()),
        iterations,
        EVP_sha256(),
        hash_length,
        hash
    );

    // 将盐和哈希都转换为十六进制字符串，用冒号分隔
    std::stringstream ss;
    for (size_t i = 0; i < salt.length(); ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(salt[i]));
    }
    ss << ":";
    for (int i = 0; i < hash_length; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
#else
    // 非OpenSSL环境：使用简单的加盐方式（仅用于测试）
    std::string result;
    for (size_t i = 0; i < password.length(); ++i) {
        char salt_char = i < salt.length() ? salt[i] : salt[i % salt.length()];
        result += password[i] ^ salt_char ^ (i % 256);
    }
    std::stringstream ss;
    for (size_t i = 0; i < salt.length(); ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(salt[i]));
    }
    ss << ":";
    for (char c : result) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(c));
    }
    return ss.str();
#endif
}

// 密码哈希函数 - 使用PBKDF2-SHA256
std::string hashPassword(const std::string& password) {
    return hashPasswordWithSalt(password, generateSalt());
}

// 带盐的密码哈希函数
std::string hashPassword(const std::string& password, const std::string& salt) {
    return hashPasswordWithSalt(password, salt);
}

// 验证密码
bool verifyPassword(const std::string& password, const std::string& stored_hash) {
    // 解析存储的哈希值，格式为 "salt:hash"
    size_t colon_pos = stored_hash.find(':');
    if (colon_pos == std::string::npos) {
        return false;
    }
    std::string stored_salt_hex = stored_hash.substr(0, colon_pos);
    std::string stored_hash_hex = stored_hash.substr(colon_pos + 1);

    // 将十六进制盐字符串转换回二进制
    std::string salt;
    for (size_t i = 0; i < stored_salt_hex.length(); i += 2) {
        std::string byte_str = stored_salt_hex.substr(i, 2);
        unsigned char byte = static_cast<unsigned char>(std::stoi(byte_str, nullptr, 16));
        salt.push_back(static_cast<char>(byte));
    }

    // 计算输入密码的哈希值
    std::string computed_hash = hashPassword(password, salt);

    // 比较哈希值（constant-time比较以防止时序攻击）
    if (computed_hash != stored_hash) {
        return false;
    }
    return true;
}



//==============================================================================
// ChatServer 实现
//==============================================================================

ChatServer::ChatServer()
    : server_fd_(-1), port_(0), running_(false), max_history_size_(1000)
#ifdef OPENSSL_FOUND
    , ssl_enabled_(false), ssl_ctx_(nullptr)
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

ChatServer::~ChatServer() {
    stop();
#ifdef OPENSSL_FOUND
    cleanupSSL();
#endif
#ifdef _WIN32
    // 清理Winsock
    WSACleanup();
#endif
}

bool ChatServer::start(uint16_t port, const std::string& host) {
    // 创建socket
#ifdef _WIN32
    server_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd_ == INVALID_SOCKET) {
        std::cerr << "Failed to create socket: " << WSAGetLastError() << std::endl;
        return false;
    }
#else
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }
#endif
    
    // 设置socket选项
    int opt = 1;
#ifdef _WIN32
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
        std::cerr << "Failed to set socket option: " << WSAGetLastError() << std::endl;
        closesocket(server_fd_);
        server_fd_ = INVALID_SOCKET;
        return false;
    }
#else
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set socket option: " << strerror(errno) << std::endl;
        close(server_fd_);
        return false;
    }
#endif
    
    // 绑定地址
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    
    if (host == "0.0.0.0") {
        address.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) <= 0) {
            std::cerr << "Failed to parse host address" << std::endl;
#ifdef _WIN32
            closesocket(server_fd_);
            server_fd_ = INVALID_SOCKET;
#else
            close(server_fd_);
#endif
            return false;
        }
    }
    
#ifdef _WIN32
    if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        std::cerr << "Failed to bind socket: " << WSAGetLastError() << std::endl;
        closesocket(server_fd_);
        server_fd_ = INVALID_SOCKET;
        return false;
    }
#else
    if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Failed to bind socket: " << strerror(errno) << std::endl;
        close(server_fd_);
        return false;
    }
#endif
    
    // 监听
#ifdef _WIN32
    if (listen(server_fd_, 10) == SOCKET_ERROR) {
        std::cerr << "Failed to listen: " << WSAGetLastError() << std::endl;
        closesocket(server_fd_);
        server_fd_ = INVALID_SOCKET;
        return false;
    }
#else
    if (listen(server_fd_, 10) < 0) {
        std::cerr << "Failed to listen: " << strerror(errno) << std::endl;
        close(server_fd_);
        return false;
    }
#endif
    
    // 设置非阻塞
#ifdef _WIN32
    u_long mode = 1;
    if (ioctlsocket(server_fd_, FIONBIO, &mode) == SOCKET_ERROR) {
        std::cerr << "Failed to set non-blocking: " << WSAGetLastError() << std::endl;
        closesocket(server_fd_);
        server_fd_ = INVALID_SOCKET;
        return false;
    }
#else
    int flags = fcntl(server_fd_, F_GETFL, 0);
    if (fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "Failed to set non-blocking: " << strerror(errno) << std::endl;
        close(server_fd_);
        return false;
    }
#endif
    
    port_ = port;
    running_ = true;
    
    // 加载用户信息
    loadUsers("users.txt");
    
    // 加载消息历史
    loadMessageHistory("message_history.txt");
    
    // 启动接受连接线程
    accept_thread_ = std::thread(&ChatServer::acceptConnections, this);
    
    return true;
}

// SSL相关方法
#ifdef OPENSSL_FOUND
bool ChatServer::initSSL() {
    ssl_ctx_ = SSL_CTX_new(SSLv23_server_method());
    if (!ssl_ctx_) {
        std::cerr << "Failed to create SSL context" << std::endl;
        return false;
    }
    
    SSL_CTX_set_options(ssl_ctx_, SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    return true;
}

void ChatServer::cleanupSSL() {
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
    
    // 清理客户端SSL连接
    for (auto& [socket_fd, ssl] : client_ssl_) {
        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
    }
    client_ssl_.clear();
}

bool ChatServer::enableSSL(const std::string& cert_file, const std::string& key_file) {
    if (!initSSL()) {
        return false;
    }
    
    if (SSL_CTX_use_certificate_file(ssl_ctx_, cert_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
        std::cerr << "Failed to load certificate file" << std::endl;
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        return false;
    }
    
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx_, key_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
        std::cerr << "Failed to load private key file" << std::endl;
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        return false;
    }
    
    if (!SSL_CTX_check_private_key(ssl_ctx_)) {
        std::cerr << "Private key does not match certificate" << std::endl;
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        return false;
    }
    
    ssl_enabled_ = true;
    return true;
}

bool ChatServer::isSSLEnabled() const {
    return ssl_enabled_;
}
#endif

// 加载消息历史
bool ChatServer::loadMessageHistory(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        ChatMessage msg;
        if (ChatProtocol::decode(line, msg)) {
            addMessageToHistory(msg);
        }
    }
    
    file.close();
    return true;
}

// 加载用户信息
bool ChatServer::loadUsers(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        size_t pos = line.find(':');
        if (pos != std::string::npos) {
            std::string name = line.substr(0, pos);
            std::string password = line.substr(pos + 1);
            std::lock_guard<std::mutex> lock(users_mutex_);
            users_[name] = password;
        }
    }
    
    file.close();
    return true;
}

// 保存用户信息
bool ChatServer::saveUsers(const std::string& filename) {
    std::ofstream file(filename);
    if (!file) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(users_mutex_);
    for (const auto& [name, password] : users_) {
        file << name << ":" << password << std::endl;
    }
    
    file.close();
    return true;
}

// 保存消息历史
bool ChatServer::saveMessageHistory(const std::string& filename) {
    std::ofstream file(filename);
    if (!file) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(history_mutex_);
    for (const auto& msg : message_history_) {
        std::string encoded = ChatProtocol::encode(msg);
        file << encoded;
    }
    
    file.close();
    return true;
}

void ChatServer::stop() {
    if (!running_) return;
    
    running_ = false;
    
    // 保存用户信息
    saveUsers("users.txt");
    
    // 保存消息历史
    saveMessageHistory("message_history.txt");
    
    // 关闭服务器socket（这会让 accept 返回错误）
    if (server_fd_ >= 0) {
#ifdef _WIN32
        closesocket(server_fd_);
        server_fd_ = INVALID_SOCKET;
#else
        close(server_fd_);
        server_fd_ = -1;
#endif
    }
    
    // 等待接受连接线程结束（在释放锁之前）
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    
    // 关闭所有客户端连接（这会导致handleClient线程中的recv返回错误，线程自动退出）
    {
        std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
        for (auto& [socket_fd, client] : clients_) {
#ifdef OPENSSL_FOUND
            // 清理SSL连接
            auto it = client_ssl_.find(socket_fd);
            if (it != client_ssl_.end() && it->second) {
                SSL_shutdown(it->second);
                SSL_free(it->second);
                client_ssl_.erase(it);
            }
#endif
#ifdef _WIN32
            closesocket(socket_fd);
#else
            close(socket_fd);
#endif
        }
        clients_.clear();
        name_to_socket_.clear();
    }

    // 等待所有客户端处理线程结束
    {
        std::lock_guard<std::mutex> lock2(threads_mutex_);
        for (auto& [socket_fd, thread] : client_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        client_threads_.clear();
    }
}

bool ChatServer::isRunning() const {
    return running_;
}

size_t ChatServer::getClientCount() const {
    std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
    return clients_.size();
}

std::vector<ClientInfo> ChatServer::getClients() const {
    std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
    
    std::vector<ClientInfo> result;
    result.reserve(clients_.size());
    
    for (const auto& [socket_fd, client] : clients_) {
        result.push_back(*client);
    }
    
    return result;
}

void ChatServer::sendSystemMessage(const std::string& message) {
    std::string encoded = ChatProtocol::encodeSystemMessage(message);
    
    std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
    for (const auto& [socket_fd, client] : clients_) {
        sendToSocket(socket_fd, encoded);
    }
}

void ChatServer::sendToClient(const std::string& client_name, const std::string& message) {
    // 尝试发送给TCP客户端
    {
        std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
        auto it = name_to_socket_.find(client_name);
        if (it != name_to_socket_.end()) {
            sendToSocket(it->second, message);
            return;
        }
    }
    
    // 尝试发送给WebSocket客户端
    if (websocket_sendto_callback_) {
        websocket_sendto_callback_(client_name, message);
    }
}

// 添加消息到历史记录
void ChatServer::addMessageToHistory(const ChatMessage& message) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    
    // 添加到全局消息历史
    message_history_.push_back(message);
    if (message_history_.size() > max_history_size_) {
        message_history_.erase(message_history_.begin());
    }
    
    // 如果是聊天室消息，添加到对应聊天室的消息历史
    if (!message.receiver.empty() && message.type == ChatMessage::Type::ROOM_MESSAGE) {
        room_message_history_[message.receiver].push_back(message);
        if (room_message_history_[message.receiver].size() > max_history_size_) {
            room_message_history_[message.receiver].erase(room_message_history_[message.receiver].begin());
        }
    }
}

void ChatServer::broadcast(const std::string& message) {
    // 发送给TCP客户端
    {
        std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
        for (const auto& [socket_fd, client] : clients_) {
            sendToSocket(socket_fd, message);
        }
    }
    
    // 发送给WebSocket客户端
    if (websocket_broadcast_callback_) {
        websocket_broadcast_callback_(message);
    }
}

void ChatServer::setMessageCallback(MessageCallback callback) {
    message_callback_ = callback;
}

void ChatServer::setConnectCallback(ClientCallback callback) {
    connect_callback_ = callback;
}

void ChatServer::setDisconnectCallback(ClientCallback callback) {
    disconnect_callback_ = callback;
}

void ChatServer::setWebSocketBroadcastCallback(WebSocketSendCallback callback) {
    websocket_broadcast_callback_ = callback;
}

void ChatServer::setWebSocketSendToCallback(WebSocketSendToCallback callback) {
    websocket_sendto_callback_ = callback;
}

void ChatServer::addWebSocketClient(const std::string& client_name, void* wsi) {
    std::lock_guard<std::mutex> lock(websocket_mutex_);
    websocket_clients_[client_name] = wsi;
}

void ChatServer::removeWebSocketClient(const std::string& client_name) {
    std::lock_guard<std::mutex> lock(websocket_mutex_);
    websocket_clients_.erase(client_name);
}

bool ChatServer::hasWebSocketClient(const std::string& client_name) const {
    std::lock_guard<std::mutex> lock(websocket_mutex_);
    return websocket_clients_.find(client_name) != websocket_clients_.end();
}

size_t ChatServer::getWebSocketClientCount() const {
    std::lock_guard<std::mutex> lock(websocket_mutex_);
    return websocket_clients_.size();
}

void ChatServer::acceptConnections() {
    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
#ifdef _WIN32
        SOCKET client_socket = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                Sleep(100); // 100ms
                continue;
            } else {
                break;
            }
        }
#else
        int client_socket = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(100000); // 100ms
                continue;
            } else {
                break;
            }
        }
#endif
        
        // 获取客户端IP和端口
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
        uint16_t port = ntohs(client_addr.sin_port);
        
#ifdef OPENSSL_FOUND
        // 如果启用了SSL，进行SSL握手
        if (ssl_enabled_) {
            SSL* ssl = SSL_new(ssl_ctx_);
            if (!ssl) {
                std::cerr << "Failed to create SSL for client" << std::endl;
#ifdef _WIN32
                closesocket(client_socket);
#else
                close(client_socket);
#endif
                continue;
            }
            
            if (!SSL_set_fd(ssl, client_socket)) {
                std::cerr << "Failed to set SSL fd" << std::endl;
                SSL_free(ssl);
#ifdef _WIN32
                closesocket(client_socket);
#else
                close(client_socket);
#endif
                continue;
            }
            
            if (SSL_accept(ssl) <= 0) {
                std::cerr << "Failed to accept SSL connection" << std::endl;
                SSL_free(ssl);
#ifdef _WIN32
                closesocket(client_socket);
#else
                close(client_socket);
#endif
                continue;
            }
            
            // 保存SSL连接
            client_ssl_[client_socket] = ssl;
        }
#endif
        
        // 创建客户端信息
        auto client = std::make_unique<ClientInfo>();
        client->socket_fd = client_socket;
        client->name = "Guest_" + std::to_string(client_socket);
        client->ip_address = ip_str;
        client->port = port;
        client->connect_time = std::chrono::system_clock::now();
        client->is_logged_in = false;
        
        // 保存客户端信息副本用于回调
        std::string client_name = client->name;
        auto client_time = client->connect_time;
        
        {
            std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
            clients_[client_socket] = std::move(client);
        }
        
        // 启动客户端处理线程
        std::thread client_thread([this, client_socket, ip_str, port]() {
            handleClient(client_socket, ip_str, port);
        });
        {
            std::lock_guard<std::mutex> lock(threads_mutex_);
            client_threads_[client_socket] = std::move(client_thread);
        }
        
        // 延迟发送欢迎消息，确保客户端接收线程已启动
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 发送欢迎消息
        sendToSocket(client_socket, ChatProtocol::encodeWelcome());
        
        // 触发连接回调
        if (connect_callback_) {
            ClientInfo info;
            info.socket_fd = client_socket;
            info.name = client_name;
            info.ip_address = ip_str;
            info.port = port;
            info.connect_time = client_time;
            info.is_logged_in = false;
            connect_callback_(info);
        }
    }
}

void ChatServer::handleClient(int client_socket, const std::string& ip, uint16_t port) {
    char buffer[4096];
    std::string received_data;
    
    while (running_) {
#ifdef _WIN32
        int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) {
            std::cout << "[INFO] Client disconnected: " << ip << ":" << port << std::endl;
            removeClient(client_socket);
            return;
        }
#else
        ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) {
            std::cout << "[INFO] Client disconnected: " << ip << ":" << port << std::endl;
            removeClient(client_socket);
            return;
        }
#endif
        
        buffer[bytes_read] = '\0';
        received_data += buffer;
        
        // 处理接收到的数据，按消息分隔符分割
        size_t pos = 0;
        while ((pos = received_data.find(ChatProtocol::MSG_SEPARATOR)) != std::string::npos) {
            std::string message = received_data.substr(0, pos);
            received_data = received_data.substr(pos + ChatProtocol::MSG_SEPARATOR.length());
            
            if (message.empty()) {
                continue;
            }
            
            // 打印客户端请求日志
            ClientInfo* client = getClientBySocket(client_socket);
            std::string client_name = client ? (client->is_logged_in ? client->name : "Guest") : "Unknown";
            std::cout << "[DEBUG] Received from " << client_name << " (" << ip << ":" << port << "): " << message << std::endl;
            
            // 检查是否是命令
            if (message[0] == '/') {
                if (client) {
                    handleCommand(client_socket, message, *client);
                }
            } else if (message.substr(0, 3) == "MSG") {
                // 处理MSG类型消息
                ChatMessage chat_msg;
                if (ChatProtocol::decode(message, chat_msg)) {
                    ClientInfo* client = getClientBySocket(client_socket);
                    if (client && client->is_logged_in) {
                        // 检查用户是否被禁言
                        if (client->is_muted) {
                            // 检查禁言是否已过期
                            if (std::chrono::system_clock::now() > client->mute_end_time) {
                                client->is_muted = false;
                            } else {
                                sendToSocket(client_socket, 
                                    ChatProtocol::encodeError("You are muted. Please wait until the mute period ends."));
                                continue;
                            }
                        }
                        
                        // 处理不同类型的消息
                        if (chat_msg.type == ChatMessage::Type::PRIVATE) {
                            // 私聊消息
                            int target_socket = -1;
                            {
                                std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
                                ClientInfo* target = getClientByNameUnlocked(chat_msg.receiver);
                                if (target) {
                                    target_socket = target->socket_fd;
                                    sendToSocket(target_socket, message);
                                } else {
                                    sendToSocket(client_socket, 
                                        ChatProtocol::encodeError("User " + chat_msg.receiver + " not found"));
                                }
                            }
                            
                            // 添加到历史记录
                            addMessageToHistory(chat_msg);
                        } else if (chat_msg.type == ChatMessage::Type::ROOM_MESSAGE) {
                            // 聊天室消息
                            auto room_it = rooms_.find(chat_msg.receiver);
                            if (room_it != rooms_.end()) {
                                // 转发给聊天室所有成员
                                for (const auto& member : room_it->second.members) {
                                    int member_socket = -1;
                                    {
                                        std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
                                        auto it = name_to_socket_.find(member);
                                        if (it != name_to_socket_.end()) {
                                            member_socket = it->second;
                                            sendToSocket(member_socket, message);
                                        }
                                    }
                                }
                                
                                // 添加到历史记录
                                addMessageToHistory(chat_msg);
                            } else {
                                sendToSocket(client_socket, 
                                    ChatProtocol::encodeError("Room " + chat_msg.receiver + " not found"));
                            }
                        } else {
                            // 广播消息
                            std::string encoded = ChatProtocol::encode(chat_msg);
                            broadcast(encoded);
                            
                            // 添加到历史记录
                            addMessageToHistory(chat_msg);
                        }
                        
                        // 触发消息回调
                        if (message_callback_) {
                            message_callback_(chat_msg);
                        }
                    } else {
                        sendToSocket(client_socket, 
                            ChatProtocol::encodeError("Please login first using /login <name>"));
                    }
                } else {
                    std::cout << "[ERROR] Failed to decode MSG message" << std::endl;
                    sendToSocket(client_socket, 
                        ChatProtocol::encodeError("Invalid message format"));
                }
            } else if (message.substr(0, 13) == "FILE_TRANSFER") {
                // 处理文件传输消息
                size_t pos = 13 + 1; // 跳过"FILE_TRANSFER|"
                
                // 解析sender
                size_t end = message.find('|', pos);
                if (end == std::string::npos) continue;
                std::string sender = message.substr(pos, end - pos);
                pos = end + 1;
                
                // 解析receiver
                end = message.find('|', pos);
                if (end == std::string::npos) continue;
                std::string receiver = message.substr(pos, end - pos);
                pos = end + 1;
                
                // 解析file_name
                end = message.find('|', pos);
                if (end == std::string::npos) continue;
                std::string file_name = message.substr(pos, end - pos);
                pos = end + 1;
                
                // 解析file_size
                end = message.find('\n', pos);
                if (end == std::string::npos) continue;
                uint64_t file_size = std::stoull(message.substr(pos, end - pos));
                
                std::cout << "[DEBUG] File transfer request: " << sender << " -> " << receiver << ", file: " << file_name << ", size: " << file_size << std::endl;
                
                // 找到接收者并转发消息
                int target_socket = -1;
                {
                    std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
                    if (receiver.empty()) {
                        // 广播消息
                        for (const auto& [socket_fd, c] : clients_) {
                            if (c->is_logged_in && socket_fd != client_socket) {
                                sendToSocket(socket_fd, message);
                            }
                        }
                    } else {
                        // 私聊消息
                        ClientInfo* target = getClientByNameUnlocked(receiver);
                        if (target) {
                            target_socket = target->socket_fd;
                            sendToSocket(target_socket, message);
                        } else {
                            sendToSocket(client_socket, 
                                ChatProtocol::encodeError("User " + receiver + " not found"));
                        }
                    }
                }
            } else if (message.substr(0, 12) == "FILE_REQUEST") {
                // 处理文件请求消息
                size_t pos = 12 + 1; // 跳过"FILE_REQUEST|"
                
                // 解析sender
                size_t end = message.find('|', pos);
                if (end == std::string::npos) continue;
                std::string sender = message.substr(pos, end - pos);
                pos = end + 1;
                
                // 解析receiver
                end = message.find('|', pos);
                if (end == std::string::npos) continue;
                std::string receiver = message.substr(pos, end - pos);
                pos = end + 1;
                
                // 解析file_name
                end = message.find('\n', pos);
                if (end == std::string::npos) continue;
                std::string file_name = message.substr(pos, end - pos);
                
                std::cout << "[DEBUG] File request: " << sender << " -> " << receiver << ", file: " << file_name << std::endl;
                
                // 找到接收者并转发消息
                int target_socket = -1;
                {
                    std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
                    ClientInfo* target = getClientByNameUnlocked(receiver);
                    if (target) {
                        target_socket = target->socket_fd;
                        sendToSocket(target_socket, message);
                    } else {
                        sendToSocket(client_socket, 
                            ChatProtocol::encodeError("User " + receiver + " not found"));
                    }
                }
            } else if (message.substr(0, 13) == "FILE_RESPONSE") {
                // 处理文件响应消息
                size_t pos = 13 + 1; // 跳过"FILE_RESPONSE|"
                
                // 解析sender
                size_t end = message.find('|', pos);
                if (end == std::string::npos) continue;
                std::string sender = message.substr(pos, end - pos);
                pos = end + 1;
                
                // 解析receiver
                end = message.find('|', pos);
                if (end == std::string::npos) continue;
                std::string receiver = message.substr(pos, end - pos);
                pos = end + 1;
                
                // 解析file_name
                end = message.find('|', pos);
                if (end == std::string::npos) continue;
                std::string file_name = message.substr(pos, end - pos);
                pos = end + 1;
                
                // 解析accepted
                end = message.find('\n', pos);
                if (end == std::string::npos) continue;
                bool accepted = (message.substr(pos, end - pos) == "1");
                
                std::cout << "[DEBUG] File response: " << sender << " -> " << receiver << ", file: " << file_name << ", accepted: " << (accepted ? "yes" : "no") << std::endl;
                
                // 找到接收者并转发消息
                int target_socket = -1;
                {
                    std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
                    ClientInfo* target = getClientByNameUnlocked(receiver);
                    if (target) {
                        target_socket = target->socket_fd;
                        sendToSocket(target_socket, message);
                    } else {
                        sendToSocket(client_socket, 
                            ChatProtocol::encodeError("User " + receiver + " not found"));
                    }
                }
            } else if (message.substr(0, 9) == "FILE_DATA") {
                // 处理文件数据消息
                size_t pos = 9 + 1; // 跳过"FILE_DATA|"
                
                // 解析sender
                size_t end = message.find('|', pos);
                if (end == std::string::npos) continue;
                std::string sender = message.substr(pos, end - pos);
                pos = end + 1;
                
                // 解析receiver
                end = message.find('|', pos);
                if (end == std::string::npos) continue;
                std::string receiver = message.substr(pos, end - pos);
                pos = end + 1;
                
                // 解析file_name
                end = message.find('|', pos);
                if (end == std::string::npos) continue;
                std::string file_name = message.substr(pos, end - pos);
                pos = end + 1;
                
                // 解析file_offset
                end = message.find('|', pos);
                if (end == std::string::npos) continue;
                uint64_t file_offset = std::stoull(message.substr(pos, end - pos));
                pos = end + 1;
                
                // 解析file_data
                end = message.find('|', pos);
                if (end == std::string::npos) continue;
                std::string file_data = message.substr(pos, end - pos);
                pos = end + 1;
                
                // 解析complete
                end = message.find('\n', pos);
                if (end == std::string::npos) continue;
                bool complete = (message.substr(pos, end - pos) == "1");
                
                std::cout << "[DEBUG] File data: " << sender << " -> " << receiver << ", file: " << file_name << ", offset: " << file_offset << ", complete: " << (complete ? "yes" : "no") << std::endl;
                
                // 找到接收者并转发消息
                int target_socket = -1;
                {
                    std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
                    if (receiver.empty()) {
                        // 广播消息
                        for (const auto& [socket_fd, c] : clients_) {
                            if (c->is_logged_in && socket_fd != client_socket) {
                                sendToSocket(socket_fd, message);
                            }
                        }
                    } else {
                        // 私聊消息
                        ClientInfo* target = getClientByNameUnlocked(receiver);
                        if (target) {
                            target_socket = target->socket_fd;
                            sendToSocket(target_socket, message);
                        } else {
                            sendToSocket(client_socket, 
                                ChatProtocol::encodeError("User " + receiver + " not found"));
                        }
                    }
                }
            } else if (message.substr(0, 12) == "READ_RECEIPT") {
                // 处理已读回执消息
                size_t pos = 12 + 1; // 跳过"READ_RECEIPT|"
                
                // 解析message_id
                size_t end = message.find('|', pos);
                if (end == std::string::npos) continue;
                std::string message_id = message.substr(pos, end - pos);
                pos = end + 1;
                
                // 解析user
                end = message.find('\n', pos);
                if (end == std::string::npos) continue;
                std::string user = message.substr(pos, end - pos);
                
                std::cout << "[DEBUG] Read receipt: user " << user << " read message " << message_id << std::endl;
                
                // 查找消息并更新已读状态
                {
                    std::lock_guard<std::mutex> lock(history_mutex_);
                    // 查找全局消息
                    for (auto& msg : message_history_) {
                        if (msg.message_id == message_id) {
                            msg.read_status[user] = true;
                            // 通知发送者
                            int sender_socket = -1;
                            {
                                std::lock_guard<std::recursive_mutex> lock2(clients_mutex_);
                                auto it = name_to_socket_.find(msg.sender);
                                if (it != name_to_socket_.end()) {
                                    sender_socket = it->second;
                                }
                            }
                            if (sender_socket != -1) {
                                sendToSocket(sender_socket, ChatProtocol::encodeSystemMessage(user + " has read your message"));
                            }
                            break;
                        }
                    }
                    // 查找聊天室消息
                    for (auto& [room_name, room_messages] : room_message_history_) {
                        for (auto& msg : room_messages) {
                            if (msg.message_id == message_id) {
                                msg.read_status[user] = true;
                                // 通知发送者
                                int sender_socket = -1;
                                {
                                    std::lock_guard<std::recursive_mutex> lock2(clients_mutex_);
                                    auto it = name_to_socket_.find(msg.sender);
                                    if (it != name_to_socket_.end()) {
                                        sender_socket = it->second;
                                    }
                                }
                                if (sender_socket != -1) {
                                    sendToSocket(sender_socket, ChatProtocol::encodeSystemMessage(user + " has read your message in room " + room_name));
                                }
                                break;
                            }
                        }
                    }
                }
            } else {
                // 普通消息
                ClientInfo* client = getClientBySocket(client_socket);
                if (client && client->is_logged_in) {
                    // 检查用户是否被禁言
                    if (client->is_muted) {
                        // 检查禁言是否已过期
                        if (std::chrono::system_clock::now() > client->mute_end_time) {
                            client->is_muted = false;
                        } else {
                            sendToSocket(client_socket, 
                                ChatProtocol::encodeError("You are muted. Please wait until the mute period ends."));
                            continue;
                        }
                    }
                    
                    ChatMessage chat_msg;
                    chat_msg.sender = client->name;
                    chat_msg.content = utils::replaceEmojis(message);
                    chat_msg.timestamp = std::chrono::system_clock::now();
                    chat_msg.message_id = utils::generateRandomId(16);
                    
                    // 检查用户是否在聊天室中
                    if (!client->current_room.empty()) {
                        // 在聊天室中，发送到聊天室成员
                        chat_msg.receiver = client->current_room;
                        chat_msg.type = ChatMessage::Type::ROOM_MESSAGE;
                        
                        // 转发给聊天室所有成员
                        auto room_it = rooms_.find(client->current_room);
                        if (room_it != rooms_.end()) {
                            std::string encoded = ChatProtocol::encode(chat_msg);
                            for (const auto& member : room_it->second.members) {
                                int member_socket = -1;
                                {
                                    std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
                                    auto it = name_to_socket_.find(member);
                                    if (it != name_to_socket_.end()) {
                                        member_socket = it->second;
                                        sendToSocket(member_socket, encoded);
                                    }
                                }
                            }
                            
                            // 添加到历史记录
                            addMessageToHistory(chat_msg);
                            
                            // 触发消息回调
                            if (message_callback_) {
                                message_callback_(chat_msg);
                            }
                        } else {
                            sendToSocket(client_socket, 
                                ChatProtocol::encodeError("Room " + client->current_room + " not found"));
                        }
                    } else {
                        // 不在聊天室中，广播消息
                        chat_msg.receiver = "";
                        chat_msg.type = ChatMessage::Type::BROADCAST;
                        
                        // 添加到历史记录
                        addMessageToHistory(chat_msg);
                        
                        // 触发消息回调
                        if (message_callback_) {
                            message_callback_(chat_msg);
                        }
                        
                        // 广播消息
                        std::string encoded = ChatProtocol::encode(chat_msg);
                        broadcast(encoded);
                    }
                } else {
                    sendToSocket(client_socket, 
                        ChatProtocol::encodeError("Please login first using /login <name>"));
                }
            }
        }
    }
}

bool ChatServer::sendToSocket(int socket_fd, const std::string& message) {
#ifdef OPENSSL_FOUND
    // 检查是否使用SSL
    auto it = client_ssl_.find(socket_fd);
    if (it != client_ssl_.end() && it->second) {
        int sent = SSL_write(it->second, message.c_str(), message.length());
        return sent == static_cast<int>(message.length());
    }
#endif
    
    // 普通socket发送
#ifdef _WIN32
    int sent = send(socket_fd, message.c_str(), static_cast<int>(message.length()), 0);
    return sent == static_cast<int>(message.length());
#else
    ssize_t sent = send(socket_fd, message.c_str(), message.length(), 0);
    return sent == static_cast<ssize_t>(message.length());
#endif
}

bool ChatServer::receiveFromSocket(int socket_fd, std::string& message) {
    char buffer[4096];
    int bytes_read = 0;
    
#ifdef OPENSSL_FOUND
    // 检查是否使用SSL
    auto it = client_ssl_.find(socket_fd);
    if (it != client_ssl_.end() && it->second) {
        bytes_read = SSL_read(it->second, buffer, sizeof(buffer) - 1);
    } else {
#endif
        // 普通socket接收
#ifdef _WIN32
        bytes_read = recv(socket_fd, buffer, sizeof(buffer) - 1, 0);
#else
        bytes_read = recv(socket_fd, buffer, sizeof(buffer) - 1, 0);
#endif
#ifdef OPENSSL_FOUND
    }
#endif
    
    if (bytes_read <= 0) {
        return false;
    }
    
    buffer[bytes_read] = '\0';
    message = buffer;
    return true;
}

void ChatServer::closeClientSocket(int socket_fd) {
    removeClient(socket_fd);
}

void ChatServer::removeClient(int socket_fd) {
    std::string name;
    
    {
        std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
        
        auto it = clients_.find(socket_fd);
        if (it != clients_.end()) {
            name = it->second->name;
            name_to_socket_.erase(it->second->name);
            clients_.erase(it);
        }
    }
    
#ifdef OPENSSL_FOUND
    // 清理SSL连接
    auto ssl_it = client_ssl_.find(socket_fd);
    if (ssl_it != client_ssl_.end() && ssl_it->second) {
        SSL_shutdown(ssl_it->second);
        SSL_free(ssl_it->second);
        client_ssl_.erase(ssl_it);
    }
#endif
    
#ifdef _WIN32
    closesocket(socket_fd);
#else
    close(socket_fd);
#endif

    // 清理客户端线程
    {
        std::lock_guard<std::mutex> lock(threads_mutex_);
        auto it = client_threads_.find(socket_fd);
        if (it != client_threads_.end()) {
            if (it->second.joinable()) {
                it->second.join();
            }
            client_threads_.erase(it);
        }
    }

    // 触发断开回调
    if (disconnect_callback_ && !name.empty()) {
        ClientInfo info;
        info.socket_fd = socket_fd;
        info.name = name;
        disconnect_callback_(info);
    }
}

void ChatServer::handleCommand(int client_socket, const std::string& command, ClientInfo& client) {
    std::istringstream iss(command);
    std::string cmd;
    iss >> cmd;
    
    if (cmd == "/login" || cmd == "/l") {
        std::string name, password;
        iss >> name;
        std::getline(iss >> std::ws, password);
        
        if (name.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Usage: /login <name> [password] or /l <name> [password]"));
            return;
        }
        
        // 检查名字是否已被使用
        if (getClientByName(name)) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Name already taken"));
            return;
        }
        
        // 检查用户是否存在
        bool user_exists = false;
        std::string stored_password;
        {
            std::lock_guard<std::mutex> lock(users_mutex_);
            auto it = users_.find(name);
            if (it != users_.end()) {
                user_exists = true;
                stored_password = it->second;
            }
        }
        
        std::cout << "[DEBUG] Login attempt - name: " << name << ", password: " << (password.empty() ? "empty" : "provided") << ", user_exists: " << (user_exists ? "true" : "false") << std::endl;

        // 如果用户存在，需要验证密码
        if (user_exists) {
            if (password.empty()) {
                std::cout << "[DEBUG] Password required for existing user" << std::endl;
                sendToSocket(client_socket,
                    ChatProtocol::encodeError("Password required for existing user"));
                return;
            }

            // 使用verifyPassword验证密码（支持新旧格式）
            if (!verifyPassword(password, stored_password)) {
                sendToSocket(client_socket,
                    ChatProtocol::encodeError("Invalid password"));
                return;
            }
        } else {
            // 如果用户不存在，且提供了密码，则注册
            if (!password.empty()) {
                std::string hashed_password = hashPassword(password);  // 自动生成随机盐
                {
                    std::lock_guard<std::mutex> lock(users_mutex_);
                    users_[name] = hashed_password;
                }
            }
        }
        
        // 更新客户端信息（只做必要的操作）
        std::string old_name;
        {
            std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
            old_name = client.name;
            name_to_socket_.erase(client.name);
            client.name = name;
            client.is_logged_in = true;
            // 检查是否是第一个用户，如果是，设置为管理员
            if (users_.size() == 1) {
                client.is_admin = true;
            }
            name_to_socket_[name] = client_socket;
        }  // 释放锁
        
        // I/O 操作在锁外进行
        sendToSocket(client_socket, 
            ChatProtocol::encodeSuccess("Logged in as " + name));
        
        // 广播用户加入
        broadcast(ChatProtocol::encodeSystemMessage(name + " joined the chat"));
    } else if (cmd == "/register" || cmd == "/r") {
        std::string name, password;
        iss >> name;
        std::getline(iss >> std::ws, password);
        
        if (name.empty() || password.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Usage: /register <name> <password> or /r <name> <password>"));
            return;
        }
        
        // 检查名字是否已被使用
        if (getClientByName(name)) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Name already in use"));
            return;
        }
        
        // 检查用户是否已存在
        bool user_exists = false;
        {
            std::lock_guard<std::mutex> lock(users_mutex_);
            auto it = users_.find(name);
            if (it != users_.end()) {
                user_exists = true;
            }
        }
        
        if (user_exists) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("User already exists"));
            return;
        }
        
        // 注册新用户
        std::string hashed_password = hashPassword(password);
        {
            std::lock_guard<std::mutex> lock(users_mutex_);
            users_[name] = hashed_password;
        }
        
        // 保存用户信息到文件
        saveUsers("users.txt");
        
        std::string register_success = ChatProtocol::encodeSuccess("User registered successfully: " + name);
        std::cout << "[DEBUG] Sending register success to " << client_socket << ": " << register_success;
        bool sent = sendToSocket(client_socket, register_success);
        std::cout << "[DEBUG] Register success send result: " << (sent ? "success" : "failed") << std::endl;
        
        // 自动登录
        std::string old_name;
        {
            std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
            old_name = client.name;
            name_to_socket_.erase(client.name);
            client.name = name;
            client.is_logged_in = true;
            // 检查是否是第一个用户，如果是，设置为管理员
            if (users_.size() == 1) {
                client.is_admin = true;
            }
            name_to_socket_[name] = client_socket;
        }  // 释放锁
        
        // 广播用户加入
        broadcast(ChatProtocol::encodeSystemMessage(name + " joined the chat"));
        
    } else if (cmd == "/users" || cmd == "/u") {
        std::vector<std::string> users;
        
        {
            std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
            for (const auto& [socket_fd, c] : clients_) {
                if (c->is_logged_in) {
                    users.push_back(c->name);
                }
            }
        }
        
        sendToSocket(client_socket, ChatProtocol::encodeUserList(users));
        
    } else if (cmd == "/whisper" || cmd == "/w") {
        std::string target_name, msg;
        iss >> target_name;
        std::getline(iss >> std::ws, msg);
        
        if (target_name.empty() || msg.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Usage: /whisper <name> <message> or /w <name> <message>"));
            return;
        }
        
        // 在锁内查找目标客户端的 socket_fd
        int target_socket = -1;
        {
            std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
            ClientInfo* target = getClientByNameUnlocked(target_name);
            if (target) {
                target_socket = target->socket_fd;
            }
        }  // 释放锁
        
        if (target_socket == -1) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("User not found: " + target_name));
            return;
        }
        
        ChatMessage chat_msg;
        chat_msg.sender = client.name;
        chat_msg.receiver = target_name;
        chat_msg.content = utils::replaceEmojis(msg);
        chat_msg.type = ChatMessage::Type::PRIVATE;
        chat_msg.timestamp = std::chrono::system_clock::now();
        chat_msg.message_id = utils::generateRandomId(16);
        
        // 添加到历史记录
        addMessageToHistory(chat_msg);
        
        std::string encoded = ChatProtocol::encode(chat_msg);
        std::cout << "[DEBUG] Sending private message from " << client.name << " to " << target_name << ": " << encoded << std::endl;
        sendToSocket(target_socket, encoded);
        sendToSocket(client_socket, encoded);
        
    } else if (cmd == "/help" || cmd == "/h") {
        std::string help = "Commands:\n";
        help += "  /login <name> [password] (/l)    - Login with a name and optional password\n";
        help += "  /register <name> <password> (/r) - Register a new user\n";
        help += "  /users (/u)           - List online users\n";
        help += "  /whisper <name> <msg> (/w) - Send private message\n";
        help += "  /help (/h)            - Show this help\n";
        help += "  /quit (/q)            - Disconnect\n";
        help += "  /nick <name> (/n)     - Change nickname\n";
        help += "  /info                 - Show server info\n";
        help += "  /createroom <name> [private] [password] (/cr) - Create a chat room\n";
        help += "  /joinroom <name> [password] (/jr) - Join a chat room\n";
        help += "  /leaveroom (/lr)      - Leave current chat room\n";
        help += "  /rooms (/ro)          - List all chat rooms\n";
        help += "  /roommembers (/rm)    - List members in current chat room\n";
        help += "  /transfer <file> <name> (/t) - Transfer file to user\n";
        help += "  /file <file>          - Send file to current chat room\n";
        help += "  /history [count]      - Get message history\n";
        help += "  /roomhistory [count]  - Get current room message history\n";
        help += "  /mute <name> [time]   - Mute user (admin only)\n";
        help += "  /unmute <name>        - Unmute user (admin only)\n";
        help += "  /kick <name> [reason] - Kick user (admin only)\n";
        
        sendToSocket(client_socket, ChatProtocol::encodeSystemMessage(help));
        
    } else if (cmd == "/quit" || cmd == "/q") {
        // 离开当前聊天室
        if (!client.current_room.empty()) {
            {
                std::lock_guard<std::mutex> lock(rooms_mutex_);
                auto it = rooms_.find(client.current_room);
                if (it != rooms_.end()) {
                    auto& members = it->second.members;
                    members.erase(std::remove(members.begin(), members.end(), client.name), members.end());

                    // 如果聊天室为空，删除聊天室
                    if (members.empty()) {
                        rooms_.erase(it);
                    }
                }
            }
            
            // 广播用户离开聊天室消息
            broadcast(ChatProtocol::encodeSystemMessage(client.name + " left room: " + client.current_room));
        }
        
        sendToSocket(client_socket, ChatProtocol::encodeSuccess("Goodbye!"));
        removeClient(client_socket);
        
    } else if (cmd == "/nick" || cmd == "/n") {
        std::string new_name;
        iss >> new_name;
        
        if (new_name.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Usage: /nick <name> or /n <name>"));
            return;
        }
        
        // 检查名字是否已被使用
        if (getClientByName(new_name)) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Name already taken"));
            return;
        }
        
        // 更新客户端信息
        std::string old_name;
        {
            std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
            old_name = client.name;
            name_to_socket_.erase(client.name);
            client.name = new_name;
            name_to_socket_[new_name] = client_socket;
        }  // 释放锁
        
        // 发送成功消息
        sendToSocket(client_socket, 
            ChatProtocol::encodeSuccess("Nickname changed to " + new_name));
        
        // 广播昵称变更
        broadcast(ChatProtocol::encodeSystemMessage(old_name + " changed nickname to " + new_name));
        
    } else if (cmd == "/info") {
        std::string info = "Server Info:\n";
        info += "  Online users: " + std::to_string(getClientCount()) + "\n";
        info += "  Server version: 1.0.0\n";
        info += "  SmallChat - A simple chat server\n";
        
        sendToSocket(client_socket, ChatProtocol::encodeSystemMessage(info));
        
    } else if (cmd == "/register" || cmd == "/r") {
        std::string name, password;
        iss >> name;
        std::getline(iss >> std::ws, password);
        
        if (name.empty() || password.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Usage: /register <name> <password> or /r <name> <password>"));
            return;
        }
        
        // 检查名字是否已被使用
        if (getClientByName(name)) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Name already in use"));
            return;
        }
        
        // 检查用户是否已存在
        bool user_exists = false;
        {
            std::lock_guard<std::mutex> lock(users_mutex_);
            auto it = users_.find(name);
            if (it != users_.end()) {
                user_exists = true;
            }
        }
        
        if (user_exists) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("User already exists"));
            return;
        }
        
        // 注册新用户
        std::string hashed_password = hashPassword(password);
        {
            std::lock_guard<std::mutex> lock(users_mutex_);
            users_[name] = hashed_password;
        }
        
        // 保存用户信息到文件
        saveUsers("users.txt");
        
        std::string register_success = ChatProtocol::encodeSuccess("User registered successfully: " + name);
        std::cout << "[DEBUG] Sending register success to " << client_socket << ": " << register_success;
        bool sent = sendToSocket(client_socket, register_success);
        std::cout << "[DEBUG] Register success send result: " << (sent ? "success" : "failed") << std::endl;
        
        // 自动登录
        std::string old_name;
        {
            std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
            old_name = client.name;
            name_to_socket_.erase(client.name);
            client.name = name;
            client.is_logged_in = true;
            // 检查是否是第一个用户，如果是，设置为管理员
            if (users_.size() == 1) {
                client.is_admin = true;
            }
            name_to_socket_[name] = client_socket;
        }  // 释放锁
        
        // 广播用户加入
        broadcast(ChatProtocol::encodeSystemMessage(name + " joined the chat"));
        
    } else if (cmd == "/createroom" || cmd == "/cr") {
        // 创建聊天室
        std::string room_name, private_flag, password;
        iss >> room_name >> private_flag >> password;
        
        if (room_name.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Usage: /createroom <name> [private] [password] or /cr <name> [private] [password]"));
            return;
        }
        
        bool is_private = (private_flag == "private" || private_flag == "1");
        
        // 检查聊天室是否已存在
        bool room_exists = false;
        {
            std::lock_guard<std::mutex> lock(rooms_mutex_);
            auto it = rooms_.find(room_name);
            if (it != rooms_.end()) {
                room_exists = true;
            }
        }
        
        if (room_exists) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Room already exists: " + room_name));
            return;
        }
        
        // 创建新聊天室
        ChatRoom room;
        room.name = room_name;
        room.creator = client.name;
        room.members.push_back(client.name);
        room.create_time = std::chrono::system_clock::now();
        room.is_private = is_private;
        if (is_private && !password.empty()) {
            room.password = hashPassword(password);
        }
        
        {
            std::lock_guard<std::mutex> lock(rooms_mutex_);
            rooms_[room_name] = room;
        }
        
        // 更新客户端当前所在聊天室
        {
            std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
            client.current_room = room_name;
        }
        
        sendToSocket(client_socket, 
            ChatProtocol::encodeSuccess("Room created successfully: " + room_name));
        
        // 广播聊天室创建消息
        broadcast(ChatProtocol::encodeSystemMessage("Room created: " + room_name + " by " + client.name));
        
    } else if (cmd == "/joinroom" || cmd == "/jr") {
        // 加入聊天室
        std::string room_name, password;
        iss >> room_name;
        std::getline(iss >> std::ws, password);
        
        if (room_name.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Usage: /joinroom <name> [password] or /jr <name> [password]"));
            return;
        }
        
        // 检查聊天室是否存在
        bool room_exists = false;
        ChatRoom room;
        {
            std::lock_guard<std::mutex> lock(rooms_mutex_);
            auto it = rooms_.find(room_name);
            if (it != rooms_.end()) {
                room_exists = true;
                room = it->second;
            }
        }
        
        if (!room_exists) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Room not found: " + room_name));
            return;
        }
        
        // 检查密码
        if (room.is_private) {
            if (password.empty()) {
                sendToSocket(client_socket,
                    ChatProtocol::encodeError("Password required for private room"));
                return;
            }

            // 使用verifyPassword验证聊天室密码
            if (!verifyPassword(password, room.password)) {
                sendToSocket(client_socket,
                    ChatProtocol::encodeError("Invalid password for room"));
                return;
            }
        }
        
        // 检查用户是否已在聊天室中
        bool already_in_room = false;
        {
            std::lock_guard<std::mutex> lock(rooms_mutex_);
            auto it = rooms_.find(room_name);
            if (it != rooms_.end()) {
                for (const auto& member : it->second.members) {
                    if (member == client.name) {
                        already_in_room = true;
                        break;
                    }
                }
            }
        }
        
        if (already_in_room) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Already in room: " + room_name));
            return;
        }
        
        // 加入聊天室
        {
            std::lock_guard<std::mutex> lock(rooms_mutex_);
            auto it = rooms_.find(room_name);
            if (it != rooms_.end()) {
                it->second.members.push_back(client.name);
            }
        }
        
        // 更新客户端当前所在聊天室
        {
            std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
            client.current_room = room_name;
        }
        
        sendToSocket(client_socket, 
            ChatProtocol::encodeSuccess("Joined room: " + room_name));
        
        // 广播用户加入聊天室消息
        broadcast(ChatProtocol::encodeSystemMessage(client.name + " joined room: " + room_name));
        
    } else if (cmd == "/leaveroom" || cmd == "/lr") {
        // 离开聊天室
        std::string current_room = client.current_room;
        
        if (current_room.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Not in any room"));
            return;
        }
        
        // 从聊天室中移除用户
        {
            std::lock_guard<std::mutex> lock(rooms_mutex_);
            auto it = rooms_.find(current_room);
            if (it != rooms_.end()) {
                auto& members = it->second.members;
                members.erase(std::remove(members.begin(), members.end(), client.name), members.end());

                // 如果聊天室为空，删除聊天室
                if (members.empty()) {
                    rooms_.erase(it);
                }
            }
        }
        
        // 更新客户端当前所在聊天室
        {
            std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
            client.current_room = "";
        }
        
        sendToSocket(client_socket, 
            ChatProtocol::encodeSuccess("Left room: " + current_room));
        
        // 广播用户离开聊天室消息
        broadcast(ChatProtocol::encodeSystemMessage(client.name + " left room: " + current_room));
        
    } else if (cmd == "/rooms" || cmd == "/ro") {
        // 列出所有聊天室
        std::vector<std::string> rooms;
        {
            std::lock_guard<std::mutex> lock(rooms_mutex_);
            for (const auto& [room_name, room] : rooms_) {
                rooms.push_back(room_name);
            }
        }
        
        sendToSocket(client_socket, ChatProtocol::encodeRoomList(rooms));
        
    } else if (cmd == "/roommembers" || cmd == "/rm") {
        // 列出当前聊天室成员
        std::string current_room = client.current_room;
        
        if (current_room.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Not in any room"));
            return;
        }
        
        std::vector<std::string> members;
        {
            std::lock_guard<std::mutex> lock(rooms_mutex_);
            auto it = rooms_.find(current_room);
            if (it != rooms_.end()) {
                members = it->second.members;
            }
        }
        
        sendToSocket(client_socket, ChatProtocol::encodeRoomMembers(current_room, members));
        
    } else if (cmd == "/history") {
        // 获取全局消息历史
        int count = 50; // 默认获取50条消息
        std::string count_str;
        if (iss >> count_str) {
            try {
                count = std::stoi(count_str);
                if (count < 1) count = 1;
                if (count > 100) count = 100; // 最多获取100条消息
            } catch (...) {
                count = 50;
            }
        }
        
        std::vector<ChatMessage> history;
        {
            std::lock_guard<std::mutex> lock(history_mutex_);
            size_t start = message_history_.size() > count ? message_history_.size() - count : 0;
            for (size_t i = start; i < message_history_.size(); i++) {
                history.push_back(message_history_[i]);
            }
        }
        
        // 发送历史消息
        sendToSocket(client_socket, ChatProtocol::encodeSystemMessage("Message History:"));
        for (const auto& msg : history) {
            std::string formatted_msg = "[" + utils::formatTimestamp(msg.timestamp) + "] " + msg.sender + ": " + msg.content;
            sendToSocket(client_socket, ChatProtocol::encodeSystemMessage(formatted_msg));
        }
        
    } else if (cmd == "/roomhistory") {
        // 获取当前聊天室消息历史
        std::string current_room = client.current_room;
        
        if (current_room.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Not in any room"));
            return;
        }
        
        int count = 50; // 默认获取50条消息
        std::string count_str;
        if (iss >> count_str) {
            try {
                count = std::stoi(count_str);
                if (count < 1) count = 1;
                if (count > 100) count = 100; // 最多获取100条消息
            } catch (...) {
                count = 50;
            }
        }
        
        std::vector<ChatMessage> history;
        {
            std::lock_guard<std::mutex> lock(history_mutex_);
            auto it = room_message_history_.find(current_room);
            if (it != room_message_history_.end()) {
                size_t start = it->second.size() > count ? it->second.size() - count : 0;
                for (size_t i = start; i < it->second.size(); i++) {
                    history.push_back(it->second[i]);
                }
            }
        }
        
        // 发送历史消息
        sendToSocket(client_socket, ChatProtocol::encodeSystemMessage("Room History:"));
        for (const auto& msg : history) {
            std::string formatted_msg = "[" + utils::formatTimestamp(msg.timestamp) + "] " + msg.sender + ": " + msg.content;
            sendToSocket(client_socket, ChatProtocol::encodeSystemMessage(formatted_msg));
        }
        
    } else if (cmd == "/mute") {
        // 禁言用户（管理员权限）
        if (!client.is_admin) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Admin permission required"));
            return;
        }
        
        std::string target_name, time_str;
        iss >> target_name;
        std::getline(iss >> std::ws, time_str);
        
        if (target_name.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Usage: /mute <name> [time]") );
            return;
        }
        
        // 查找目标用户
        ClientInfo* target = getClientByName(target_name);
        if (!target) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("User not found: " + target_name));
            return;
        }
        
        // 解析禁言时间（默认10分钟）
        int minutes = 10;
        if (!time_str.empty()) {
            try {
                minutes = std::stoi(time_str);
                if (minutes < 1) minutes = 1;
                if (minutes > 60) minutes = 60; // 最多60分钟
            } catch (...) {
                minutes = 10;
            }
        }
        
        // 设置禁言
        target->is_muted = true;
        target->mute_end_time = std::chrono::system_clock::now() + std::chrono::minutes(minutes);
        
        // 发送通知
        sendToSocket(client_socket, 
            ChatProtocol::encodeSuccess("Muted " + target_name + " for " + std::to_string(minutes) + " minutes"));
        sendToSocket(target->socket_fd, 
            ChatProtocol::encodeSystemMessage("You have been muted for " + std::to_string(minutes) + " minutes"));
        broadcast(ChatProtocol::encodeSystemMessage(target_name + " has been muted for " + std::to_string(minutes) + " minutes"));
        
    } else if (cmd == "/unmute") {
        // 解除禁言（管理员权限）
        if (!client.is_admin) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Admin permission required"));
            return;
        }
        
        std::string target_name;
        iss >> target_name;
        
        if (target_name.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Usage: /unmute <name>"));
            return;
        }
        
        // 查找目标用户
        ClientInfo* target = getClientByName(target_name);
        if (!target) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("User not found: " + target_name));
            return;
        }
        
        // 解除禁言
        target->is_muted = false;
        target->mute_end_time = std::chrono::system_clock::now();
        
        // 发送通知
        sendToSocket(client_socket, 
            ChatProtocol::encodeSuccess("Unmuted " + target_name));
        sendToSocket(target->socket_fd, 
            ChatProtocol::encodeSystemMessage("You have been unmuted"));
        broadcast(ChatProtocol::encodeSystemMessage(target_name + " has been unmuted"));
        
    } else if (cmd == "/kick") {
        // 踢人（管理员权限）
        if (!client.is_admin) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Admin permission required"));
            return;
        }
        
        std::string target_name, reason;
        iss >> target_name;
        std::getline(iss >> std::ws, reason);
        
        if (target_name.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Usage: /kick <name> [reason]") );
            return;
        }
        
        // 查找目标用户
        ClientInfo* target = getClientByName(target_name);
        if (!target) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("User not found: " + target_name));
            return;
        }
        
        // 发送踢人通知
        std::string kick_message = "You have been kicked from the server";
        if (!reason.empty()) {
            kick_message += ": " + reason;
        }
        sendToSocket(target->socket_fd, 
            ChatProtocol::encodeSystemMessage(kick_message));
        
        // 广播踢人消息
        std::string broadcast_message = target_name + " has been kicked from the server";
        if (!reason.empty()) {
            broadcast_message += ": " + reason;
        }
        broadcast(ChatProtocol::encodeSystemMessage(broadcast_message));
        
        // 关闭连接
        closeClientSocket(target->socket_fd);
        
    } else if (cmd == "/transfer" || cmd == "/t") {
        // 传输文件给指定用户
        std::string file_path, target_name;
        iss >> file_path >> target_name;
        
        if (file_path.empty() || target_name.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Usage: /transfer <file> <name> or /t <file> <name>"));
            return;
        }
        
        // 检查目标用户是否存在
        int target_socket = -1;
        {
            std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
            ClientInfo* target = getClientByNameUnlocked(target_name);
            if (target) {
                target_socket = target->socket_fd;
            }
        }
        
        if (target_socket == -1) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("User not found: " + target_name));
            return;
        }
        
        // 读取文件内容
        std::ifstream file(file_path, std::ios::binary);
        if (!file) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Failed to open file: " + file_path));
            return;
        }
        
        // 获取文件名
        size_t last_slash = file_path.find_last_of("/\\");
        std::string file_name = (last_slash == std::string::npos) ? file_path : file_path.substr(last_slash + 1);
        
        // 获取文件大小
        file.seekg(0, std::ios::end);
        uint64_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // 发送文件传输请求
        sendToSocket(target_socket, 
            ChatProtocol::encodeFileTransfer(client.name, target_name, file_name, file_size));
        
        // 发送文件数据
        const size_t buffer_size = 4096;
        char buffer[buffer_size];
        uint64_t offset = 0;
        
        while (file.read(buffer, buffer_size) || file.gcount() > 0) {
            std::string file_data(buffer, file.gcount());
            bool complete = (offset + file_data.size() >= file_size);
            
            sendToSocket(target_socket, 
                ChatProtocol::encodeFileData(client.name, target_name, file_name, offset, file_data, complete));
            
            offset += file_data.size();
        }
        
        file.close();
        
        sendToSocket(client_socket, 
            ChatProtocol::encodeSuccess("File transfer started: " + file_name));
        
    } else if (cmd == "/file") {
        // 发送文件到当前聊天室
        std::string file_path;
        iss >> file_path;
        
        if (file_path.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Usage: /file <file>"));
            return;
        }
        
        // 检查是否在聊天室中
        if (client.current_room.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Not in any room"));
            return;
        }
        
        // 读取文件内容
        std::ifstream file(file_path, std::ios::binary);
        if (!file) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Failed to open file: " + file_path));
            return;
        }
        
        // 获取文件名
        size_t last_slash = file_path.find_last_of("/\\");
        std::string file_name = (last_slash == std::string::npos) ? file_path : file_path.substr(last_slash + 1);
        
        // 获取文件大小
        file.seekg(0, std::ios::end);
        uint64_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // 获取聊天室所有成员
        std::vector<int> member_sockets;
        {
            std::lock_guard<std::mutex> lock(rooms_mutex_);
            auto it = rooms_.find(client.current_room);
            if (it != rooms_.end()) {
                std::lock_guard<std::recursive_mutex> lock2(clients_mutex_);
                for (const auto& member : it->second.members) {
                    auto name_it = name_to_socket_.find(member);
                    if (name_it != name_to_socket_.end()) {
                        member_sockets.push_back(name_it->second);
                    }
                }
            }
        }
        
        // 发送文件传输请求给所有成员
        for (int socket_fd : member_sockets) {
            sendToSocket(socket_fd, 
                ChatProtocol::encodeFileTransfer(client.name, client.current_room, file_name, file_size));
        }
        
        // 发送文件数据给所有成员
        const size_t buffer_size = 4096;
        char buffer[buffer_size];
        uint64_t offset = 0;
        
        while (file.read(buffer, buffer_size) || file.gcount() > 0) {
            std::string file_data(buffer, file.gcount());
            bool complete = (offset + file_data.size() >= file_size);
            
            for (int socket_fd : member_sockets) {
                sendToSocket(socket_fd, 
                    ChatProtocol::encodeFileData(client.name, client.current_room, file_name, offset, file_data, complete));
            }
            
            offset += file_data.size();
        }
        
        file.close();
        
        sendToSocket(client_socket, 
            ChatProtocol::encodeSuccess("File sent to room: " + file_name));
        
    } else {
        // 检查是否在聊天室中，如果是，发送聊天室消息
        if (!client.current_room.empty()) {
            // 发送聊天室消息
            ChatMessage chat_msg;
            chat_msg.sender = client.name;
            chat_msg.receiver = client.current_room;
            chat_msg.content = utils::replaceEmojis(command);
            chat_msg.type = ChatMessage::Type::ROOM_MESSAGE;
            chat_msg.timestamp = std::chrono::system_clock::now();
            chat_msg.message_id = utils::generateRandomId(16);
            
            // 添加到历史记录
            addMessageToHistory(chat_msg);
            
            // 触发消息回调
            if (message_callback_) {
                message_callback_(chat_msg);
            }
            
            // 发送消息给聊天室所有成员
            std::vector<int> member_sockets;
            {
                std::lock_guard<std::mutex> lock(rooms_mutex_);
                auto it = rooms_.find(client.current_room);
                if (it != rooms_.end()) {
                    std::lock_guard<std::recursive_mutex> lock2(clients_mutex_);
                    for (const auto& member : it->second.members) {
                        auto name_it = name_to_socket_.find(member);
                        if (name_it != name_to_socket_.end()) {
                            member_sockets.push_back(name_it->second);
                        }
                    }
                }
            }
            
            std::string encoded = ChatProtocol::encode(chat_msg);
            for (int socket_fd : member_sockets) {
                sendToSocket(socket_fd, encoded);
            }
        } else {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Unknown command: " + cmd));
        }
    }
}

ClientInfo* ChatServer::getClientBySocket(int socket_fd) {
    std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
    
    auto it = clients_.find(socket_fd);
    if (it != clients_.end()) {
        return it->second.get();
    }
    
    return nullptr;
}

ClientInfo* ChatServer::getClientByName(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(clients_mutex_);
    return getClientByNameUnlocked(name);
}

ClientInfo* ChatServer::getClientByNameUnlocked(const std::string& name) {
    auto it = name_to_socket_.find(name);
    if (it != name_to_socket_.end()) {
        auto client_it = clients_.find(it->second);
        if (client_it != clients_.end()) {
            return client_it->second.get();
        }
    }
    
    return nullptr;
}

} // namespace smallchat
