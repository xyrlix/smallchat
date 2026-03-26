#include "server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace smallchat {

const std::string ChatProtocol::MSG_SEPARATOR = "\n";
const std::string ChatProtocol::FIELD_SEPARATOR = "|";

//==============================================================================
// ChatProtocol 实现
//==============================================================================

std::string ChatProtocol::encode(const ChatMessage& message) {
    std::stringstream ss;
    
    ss << "MSG" << FIELD_SEPARATOR;
    ss << message.sender << FIELD_SEPARATOR;
    ss << message.receiver << FIELD_SEPARATOR;
    ss << static_cast<int>(message.type) << FIELD_SEPARATOR;
    ss << message.content << MSG_SEPARATOR;
    
    return ss.str();
}

bool ChatProtocol::decode(const std::string& data, ChatMessage& message) {
    size_t pos = 0;
    
    // 查找分隔符
    auto findField = [&](size_t start) -> std::string {
        size_t end = data.find(FIELD_SEPARATOR, start);
        if (end == std::string::npos) return "";
        pos = end + 1;
        return data.substr(start, end - start);
    };
    
    std::string type = findField(0);
    if (type != "MSG") return false;
    
    message.sender = findField(pos);
    message.receiver = findField(pos);
    message.type = static_cast<ChatMessage::Type>(std::stoi(findField(pos)));
    message.content = findField(pos);
    
    message.timestamp = std::chrono::system_clock::now();
    
    return true;
}

std::string ChatProtocol::encodeSystemMessage(const std::string& message) {
    std::stringstream ss;
    ss << "SYS" << MSG_SEPARATOR;
    ss << message << MSG_SEPARATOR;
    return ss.str();
}

std::string ChatProtocol::encodeWelcome() {
    std::stringstream ss;
    ss << "WELCOME" << MSG_SEPARATOR;
    ss << "Welcome to SmallChat Server!" << MSG_SEPARATOR;
    ss << "Commands: /login <name>, /users, /whisper <name> <msg>, /help, /quit" << MSG_SEPARATOR;
    return ss.str();
}

std::string ChatProtocol::encodeUserList(const std::vector<std::string>& users) {
    std::stringstream ss;
    ss << "USERS" << MSG_SEPARATOR;
    ss << users.size() << MSG_SEPARATOR;
    for (const auto& user : users) {
        ss << user << MSG_SEPARATOR;
    }
    return ss.str();
}

std::string ChatProtocol::encodeError(const std::string& error) {
    std::stringstream ss;
    ss << "ERROR" << MSG_SEPARATOR;
    ss << error << MSG_SEPARATOR;
    return ss.str();
}

std::string ChatProtocol::encodeSuccess(const std::string& message) {
    std::stringstream ss;
    ss << "OK" << MSG_SEPARATOR;
    ss << message << MSG_SEPARATOR;
    return ss.str();
}

//==============================================================================
// ChatServer 实现
//==============================================================================

ChatServer::ChatServer()
    : server_fd_(-1), port_(0), running_(false) {
}

ChatServer::~ChatServer() {
    stop();
}

bool ChatServer::start(uint16_t port, const std::string& host) {
    // 创建socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        return false;
    }
    
    // 设置socket选项
    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(server_fd_);
        return false;
    }
    
    // 绑定地址
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    
    if (host == "0.0.0.0") {
        address.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) <= 0) {
            close(server_fd_);
            return false;
        }
    }
    
    if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(server_fd_);
        return false;
    }
    
    // 监听
    if (listen(server_fd_, 10) < 0) {
        close(server_fd_);
        return false;
    }
    
    // 设置非阻塞
    int flags = fcntl(server_fd_, F_GETFL, 0);
    fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);
    
    port_ = port;
    running_ = true;
    
    // 启动接受连接线程
    accept_thread_ = std::thread(&ChatServer::acceptConnections, this);
    
    return true;
}

void ChatServer::stop() {
    if (!running_) return;
    
    running_ = false;
    
    // 关闭所有客户端连接
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& [socket_fd, client] : clients_) {
        close(socket_fd);
    }
    clients_.clear();
    name_to_socket_.clear();
    
    // 关闭服务器socket
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
    
    // 等待线程结束
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock2(threads_mutex_);
    for (auto& [socket_fd, thread] : client_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    client_threads_.clear();
}

bool ChatServer::isRunning() const {
    return running_;
}

size_t ChatServer::getClientCount() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_.size();
}

std::vector<ClientInfo> ChatServer::getClients() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    std::vector<ClientInfo> result;
    result.reserve(clients_.size());
    
    for (const auto& [socket_fd, client] : clients_) {
        result.push_back(*client);
    }
    
    return result;
}

void ChatServer::sendSystemMessage(const std::string& message) {
    std::string encoded = ChatProtocol::encodeSystemMessage(message);
    
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (const auto& [socket_fd, client] : clients_) {
        sendToSocket(socket_fd, encoded);
    }
}

void ChatServer::sendToClient(const std::string& client_name, const std::string& message) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = name_to_socket_.find(client_name);
    if (it != name_to_socket_.end()) {
        sendToSocket(it->second, message);
    }
}

void ChatServer::broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (const auto& [socket_fd, client] : clients_) {
        sendToSocket(socket_fd, message);
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

void ChatServer::acceptConnections() {
    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(100000); // 100ms
                continue;
            } else {
                break;
            }
        }
        
        // 获取客户端IP和端口
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
        uint16_t port = ntohs(client_addr.sin_port);
        
        // 创建客户端信息
        auto client = std::make_unique<ClientInfo>();
        client->socket_fd = client_socket;
        client->name = "Guest_" + std::to_string(client_socket);
        client->ip_address = ip_str;
        client->port = port;
        client->connect_time = std::chrono::system_clock::now();
        client->is_logged_in = false;
        
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_[client_socket] = std::move(client);
        }
        
        // 启动客户端处理线程
        std::thread([this, client_socket, ip_str, port]() {
            handleClient(client_socket, ip_str, port);
        }).detach();
        
        // 发送欢迎消息
        sendToSocket(client_socket, ChatProtocol::encodeWelcome());
        
        // 触发连接回调
        if (connect_callback_) {
            ClientInfo info;
            info.socket_fd = client_socket;
            info.name = client->name;
            info.ip_address = ip_str;
            info.port = port;
            info.connect_time = client->connect_time;
            info.is_logged_in = false;
            connect_callback_(info);
        }
    }
}

void ChatServer::handleClient(int client_socket, const std::string& ip, uint16_t port) {
    char buffer[4096];
    
    while (running_) {
        ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_read <= 0) {
            removeClient(client_socket);
            return;
        }
        
        buffer[bytes_read] = '\0';
        std::string message(buffer);
        
        // 检查是否是命令
        if (message[0] == '/') {
            ClientInfo* client = getClientBySocket(client_socket);
            if (client) {
                handleCommand(client_socket, message, *client);
            }
        } else {
            // 普通消息
            ClientInfo* client = getClientBySocket(client_socket);
            if (client && client->is_logged_in) {
                ChatMessage chat_msg;
                chat_msg.sender = client->name;
                chat_msg.receiver = "";
                chat_msg.content = message;
                chat_msg.type = ChatMessage::Type::BROADCAST;
                chat_msg.timestamp = std::chrono::system_clock::now();
                
                // 触发消息回调
                if (message_callback_) {
                    message_callback_(chat_msg);
                }
                
                // 广播消息
                std::string encoded = ChatProtocol::encode(chat_msg);
                broadcast(encoded);
            } else {
                sendToSocket(client_socket, 
                    ChatProtocol::encodeError("Please login first using /login <name>"));
            }
        }
    }
}

bool ChatServer::sendToSocket(int socket_fd, const std::string& message) {
    ssize_t sent = send(socket_fd, message.c_str(), message.length(), 0);
    return sent == static_cast<ssize_t>(message.length());
}

bool ChatServer::receiveFromSocket(int socket_fd, std::string& message) {
    char buffer[4096];
    ssize_t bytes_read = recv(socket_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read <= 0) {
        return false;
    }
    
    buffer[bytes_read] = '\0';
    message = buffer;
    return true;
}

void ChatServer::removeClient(int socket_fd) {
    std::string name;
    
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        
        auto it = clients_.find(socket_fd);
        if (it != clients_.end()) {
            name = it->second->name;
            name_to_socket_.erase(it->second->name);
            clients_.erase(it);
        }
    }
    
    close(socket_fd);
    
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
    
    if (cmd == "/login") {
        std::string name;
        iss >> name;
        
        if (name.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Usage: /login <name>"));
            return;
        }
        
        // 检查名字是否已被使用
        if (getClientByName(name)) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Name already taken"));
            return;
        }
        
        // 更新客户端信息
        std::lock_guard<std::mutex> lock(clients_mutex_);
        name_to_socket_.erase(client.name);
        client.name = name;
        client.is_logged_in = true;
        name_to_socket_[name] = client_socket;
        
        sendToSocket(client_socket, 
            ChatProtocol::encodeSuccess("Logged in as " + name));
        
        // 广播用户加入
        broadcast(ChatProtocol::encodeSystemMessage(name + " joined the chat"));
        
    } else if (cmd == "/users") {
        std::vector<std::string> users;
        
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            for (const auto& [socket_fd, c] : clients_) {
                if (c->is_logged_in) {
                    users.push_back(c->name);
                }
            }
        }
        
        sendToSocket(client_socket, ChatProtocol::encodeUserList(users));
        
    } else if (cmd == "/whisper") {
        std::string target_name, msg;
        iss >> target_name;
        std::getline(iss >> std::ws, msg);
        
        if (target_name.empty() || msg.empty()) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("Usage: /whisper <name> <message>"));
            return;
        }
        
        ClientInfo* target = getClientByName(target_name);
        if (!target) {
            sendToSocket(client_socket, 
                ChatProtocol::encodeError("User not found: " + target_name));
            return;
        }
        
        ChatMessage chat_msg;
        chat_msg.sender = client.name;
        chat_msg.receiver = target_name;
        chat_msg.content = msg;
        chat_msg.type = ChatMessage::Type::PRIVATE;
        chat_msg.timestamp = std::chrono::system_clock::now();
        
        std::string encoded = ChatProtocol::encode(chat_msg);
        sendToSocket(target->socket_fd, encoded);
        sendToSocket(client_socket, encoded);
        
    } else if (cmd == "/help") {
        std::string help = "Commands:\n";
        help += "  /login <name>    - Login with a name\n";
        help += "  /users           - List online users\n";
        help += "  /whisper <name> <msg> - Send private message\n";
        help += "  /help            - Show this help\n";
        help += "  /quit            - Disconnect\n";
        
        sendToSocket(client_socket, ChatProtocol::encodeSystemMessage(help));
        
    } else if (cmd == "/quit") {
        sendToSocket(client_socket, ChatProtocol::encodeSuccess("Goodbye!"));
        removeClient(client_socket);
        
    } else {
        sendToSocket(client_socket, 
            ChatProtocol::encodeError("Unknown command: " + cmd));
    }
}

ClientInfo* ChatServer::getClientBySocket(int socket_fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = clients_.find(socket_fd);
    if (it != clients_.end()) {
        return it->second.get();
    }
    
    return nullptr;
}

ClientInfo* ChatServer::getClientByName(const std::string& name) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
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
