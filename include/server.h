#ifndef SMALLCHAT_SERVER_H
#define SMALLCHAT_SERVER_H

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <thread>

namespace smallchat {

/**
 * @brief 客户端信息
 */
struct ClientInfo {
    int socket_fd;
    std::string name;
    std::string ip_address;
    uint16_t port;
    std::chrono::system_clock::time_point connect_time;
    bool is_logged_in;
};

/**
 * @brief 聊天消息
 */
struct ChatMessage {
    std::string sender;
    std::string receiver;  // 空表示广播消息
    std::string content;
    std::chrono::system_clock::time_point timestamp;
    
    enum class Type {
        TEXT,
        SYSTEM,
        PRIVATE,
        BROADCAST
    };
    Type type;
};

/**
 * @brief 聊天服务器
 */
class ChatServer {
public:
    /**
     * @brief 消息回调函数类型
     */
    using MessageCallback = std::function<void(const ChatMessage&)>;
    
    /**
     * @brief 客户端连接回调函数类型
     */
    using ClientCallback = std::function<void(const ClientInfo&)>;
    
    ChatServer();
    ~ChatServer();
    
    /**
     * @brief 启动服务器
     */
    bool start(uint16_t port, const std::string& host = "0.0.0.0");
    
    /**
     * @brief 停止服务器
     */
    void stop();
    
    /**
     * @brief 是否正在运行
     */
    bool isRunning() const;
    
    /**
     * @brief 获取在线客户端数量
     */
    size_t getClientCount() const;
    
    /**
     * @brief 获取客户端列表
     */
    std::vector<ClientInfo> getClients() const;
    
    /**
     * @brief 发送系统消息
     */
    void sendSystemMessage(const std::string& message);
    
    /**
     * @brief 发送消息给指定客户端
     */
    void sendToClient(const std::string& client_name, const std::string& message);
    
    /**
     * @brief 广播消息给所有客户端
     */
    void broadcast(const std::string& message);
    
    /**
     * @brief 设置消息回调
     */
    void setMessageCallback(MessageCallback callback);
    
    /**
     * @brief 设置客户端连接回调
     */
    void setConnectCallback(ClientCallback callback);
    
    /**
     * @brief 设置客户端断开回调
     */
    void setDisconnectCallback(ClientCallback callback);
    
private:
    /**
     * @brief 接受客户端连接
     */
    void acceptConnections();
    
    /**
     * @brief 处理客户端消息
     */
    void handleClient(int client_socket, const std::string& ip, uint16_t port);
    
    /**
     * @brief 发送消息到客户端
     */
    bool sendToSocket(int socket_fd, const std::string& message);
    
    /**
     * @brief 从客户端接收消息
     */
    bool receiveFromSocket(int socket_fd, std::string& message);
    
    /**
     * @brief 移除客户端
     */
    void removeClient(int socket_fd);
    
    /**
     * @brief 处理客户端命令
     */
    void handleCommand(int client_socket, const std::string& command,
                       ClientInfo& client);
    
    /**
     * @brief 获取客户端信息
     */
    ClientInfo* getClientBySocket(int socket_fd);
    ClientInfo* getClientByName(const std::string& name);
    
    /**
     * @brief 获取客户端信息（不获取锁，仅在已持有锁的情况下使用）
     */
    ClientInfo* getClientByNameUnlocked(const std::string& name);
    
    int server_fd_;
    uint16_t port_;
    bool running_;
    std::thread accept_thread_;
    
    std::unordered_map<int, std::unique_ptr<ClientInfo>> clients_;
    std::unordered_map<std::string, int> name_to_socket_;
    mutable std::recursive_mutex clients_mutex_;
    
    MessageCallback message_callback_;
    ClientCallback connect_callback_;
    ClientCallback disconnect_callback_;
    
    std::unordered_map<int, std::thread> client_threads_;
    std::mutex threads_mutex_;
};

/**
 * @brief 聊天协议
 */
class ChatProtocol {
public:
    /**
     * @brief 编码消息
     */
    static std::string encode(const ChatMessage& message);
    
    /**
     * @brief 解码消息
     */
    static bool decode(const std::string& data, ChatMessage& message);
    
    /**
     * @brief 编码系统消息
     */
    static std::string encodeSystemMessage(const std::string& message);
    
    /**
     * @brief 编码欢迎消息
     */
    static std::string encodeWelcome();
    
    /**
     * @brief 编码用户列表
     */
    static std::string encodeUserList(const std::vector<std::string>& users);
    
    /**
     * @brief 编码错误消息
     */
    static std::string encodeError(const std::string& error);
    
    /**
     * @brief 编码成功消息
     */
    static std::string encodeSuccess(const std::string& message);
    
    static const std::string MSG_SEPARATOR;
    static const std::string FIELD_SEPARATOR;
};

} // namespace smallchat

#endif // SMALLCHAT_SERVER_H
