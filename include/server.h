#ifndef SMALLCHAT_SERVER_H
#define SMALLCHAT_SERVER_H

#include "common.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <functional>

#ifdef OPENSSL_FOUND
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

namespace smallchat {

/**
 * @brief WebSocket客户端回调函数类型
 */
using WebSocketSendCallback = std::function<void(const std::string& message)>;
using WebSocketSendToCallback = std::function<void(const std::string& client_name, const std::string& message)>;

/**
 * @brief 聊天服务器
 */
class ChatServer {
public:
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
     * @brief 广播消息给所有客户端（包括TCP和WebSocket）
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
    
    /**
     * @brief 设置WebSocket广播回调
     */
    void setWebSocketBroadcastCallback(WebSocketSendCallback callback);
    
    /**
     * @brief 设置WebSocket定向发送回调
     */
    void setWebSocketSendToCallback(WebSocketSendToCallback callback);
    
    /**
     * @brief 添加WebSocket客户端
     */
    void addWebSocketClient(const std::string& client_name, void* wsi);
    
    /**
     * @brief 移除WebSocket客户端
     */
    void removeWebSocketClient(const std::string& client_name);
    
    /**
     * @brief 检查WebSocket客户端是否存在
     */
    bool hasWebSocketClient(const std::string& client_name) const;
    
    /**
     * @brief 获取WebSocket客户端数量
     */
    size_t getWebSocketClientCount() const;
    
    /**
     * @brief 获取客户端信息（公共接口）
     */
    ClientInfo* getClientByName(const std::string& name);
    
    /**
     * @brief 启用SSL/TLS
     */
    bool enableSSL(const std::string& cert_file, const std::string& key_file);
    
    /**
     * @brief 检查是否启用了SSL/TLS
     */
    bool isSSLEnabled() const;
    
    /**
     * @brief 加载用户信息
     */
    bool loadUsers(const std::string& filename);
    
    /**
     * @brief 保存用户信息
     */
    bool saveUsers(const std::string& filename);
    
    /**
     * @brief 加载消息历史
     */
    bool loadMessageHistory(const std::string& filename);
    
    /**
     * @brief 保存消息历史
     */
    bool saveMessageHistory(const std::string& filename);
    
    /**
     * @brief 添加消息到历史记录
     */
    void addMessageToHistory(const ChatMessage& message);
    
    /**
     * @brief 关闭客户端socket
     */
    void closeClientSocket(int socket_fd);
    
private:
    /**
     * @brief 初始化SSL
     */
    bool initSSL();
    
    /**
     * @brief 清理SSL
     */
    void cleanupSSL();
    
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
     * @brief 获取客户端信息（内部使用）
     */
    ClientInfo* getClientBySocket(int socket_fd);
    
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
    
    // 用户存储
    std::unordered_map<std::string, std::string> users_; // 用户名 -> 加密后的密码
    std::mutex users_mutex_;
    
    // 聊天室存储
    std::unordered_map<std::string, ChatRoom> rooms_; // 聊天室名称 -> 聊天室信息
    std::mutex rooms_mutex_;
    
    // 消息历史存储
    std::vector<ChatMessage> message_history_; // 全局消息历史
    std::unordered_map<std::string, std::vector<ChatMessage>> room_message_history_; // 聊天室消息历史
    std::mutex history_mutex_;
    size_t max_history_size_; // 最大历史消息数量
    
    // WebSocket相关
    std::unordered_map<std::string, void*> websocket_clients_; // 用户名 -> wsi指针
    mutable std::mutex websocket_mutex_;
    WebSocketSendCallback websocket_broadcast_callback_;
    WebSocketSendToCallback websocket_sendto_callback_;
    
    // SSL相关
#ifdef OPENSSL_FOUND
    bool ssl_enabled_;
    SSL_CTX* ssl_ctx_;
    std::unordered_map<int, SSL*> client_ssl_;
#endif
};

} // namespace smallchat

#endif // SMALLCHAT_SERVER_H
