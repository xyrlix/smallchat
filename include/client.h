#ifndef SMALLCHAT_CLIENT_H
#define SMALLCHAT_CLIENT_H

#include "common.h"
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <sys/select.h>

#ifdef OPENSSL_FOUND
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

namespace smallchat {

/**
 * @brief 聊天客户端
 */
class ChatClient {
public:
    ChatClient();
    ~ChatClient();
    
    /**
     * @brief 连接到服务器
     */
    bool connect(const std::string& host, uint16_t port);
    
    /**
     * @brief 断开连接
     */
    void disconnect();
    
    /**
     * @brief 是否已连接
     */
    bool isConnected() const;
    
    /**
     * @brief 登录
     */
    bool login(const std::string& name, const std::string& password = "");
    
    /**
     * @brief 发送消息
     */
    bool sendMessage(const std::string& message);
    
    /**
     * @brief 发送私聊消息
     */
    bool sendPrivateMessage(const std::string& receiver, const std::string& message);
    
    /**
     * @brief 请求用户列表
     */
    bool requestUserList();
    
    /**
     * @brief 获取在线用户数
     */
    size_t getUserCount() const;
    
    /**
     * @brief 设置消息回调
     */
    void setMessageCallback(MessageCallback callback);
    
    /**
     * @brief 设置系统消息回调
     */
    void setSystemMessageCallback(SystemMessageCallback callback);
    
    /**
     * @brief 设置用户列表回调
     */
    void setUserListCallback(UserListCallback callback);
    
    /**
     * @brief 设置错误回调
     */
    void setErrorCallback(ErrorCallback callback);
    
    /**
     * @brief 设置成功消息回调
     */
    void setSuccessCallback(SystemMessageCallback callback);
    
    /**
     * @brief 启动消息接收线程
     */
    void start();
    
    /**
     * @brief 停止消息接收线程
     */
    void stop();
    
    /**
     * @brief 启用SSL/TLS
     */
    void enableSSL(bool enable = true);
    
    /**
     * @brief 检查是否启用了SSL/TLS
     */
    bool isSSLEnabled() const;
    
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
     * @brief 接收消息线程
     */
    void receiveThread();
    
    /**
     * @brief 处理接收到的消息
     */
    void processMessage(const std::string& data);
    
    /**
     * @brief 发送数据
     */
    bool send(const std::string& data);
    
    /**
     * @brief 接收数据
     */
    bool receive(std::string& data);
    
    int socket_fd_;
    std::string host_;
    uint16_t port_;
    std::string username_;
    std::atomic<bool> connected_;
    std::atomic<bool> running_;
    std::thread receive_thread_;
    
    MessageCallback message_callback_;
    SystemMessageCallback system_callback_;
    UserListCallback userlist_callback_;
    ErrorCallback error_callback_;
    SystemMessageCallback success_callback_;
    
    std::string receive_buffer_;
    std::mutex buffer_mutex_;
    
    // SSL相关
#ifdef OPENSSL_FOUND
    bool ssl_enabled_;
    SSL_CTX* ssl_ctx_;
    SSL* ssl_;
#endif
};

} // namespace smallchat

#endif // SMALLCHAT_CLIENT_H
