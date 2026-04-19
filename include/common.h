#ifndef SMALLCHAT_COMMON_H
#define SMALLCHAT_COMMON_H

#include <string>
#include <chrono>
#include <vector>
#include <functional>

namespace smallchat {

// 聊天消息结构
struct ChatMessage {
    std::string sender;        // 发送者
    std::string receiver;      // 接收者
    std::string content;       // 消息内容
    std::chrono::system_clock::time_point timestamp;  // 时间戳
    std::string password;      // 密码（登录和注册时使用）
    
    // 文件传输相关字段
    std::string file_name;     // 文件名
    uint64_t file_size;        // 文件大小
    std::string file_data;     // 文件数据（二进制数据的Base64编码）
    uint64_t file_offset;      // 文件偏移量
    bool file_transfer_complete; // 文件传输是否完成
    
    // 消息类型枚举
    enum class Type {
        BROADCAST = 0,  // 广播消息
        PRIVATE = 1,    // 私聊消息
        TEXT = 2,       // 文本消息
        SYSTEM = 3,     // 系统消息
        LOGIN = 4,      // 登录消息
        LOGIN_WITH_PASS = 5, // 带密码的登录消息
        REGISTER = 6,   // 注册消息
        LOGOUT = 7,     // 退出消息
        ERROR = 8,      // 错误消息
        OK = 9,         // 成功消息
        WELCOME = 10,   // 欢迎消息
        INFO = 11,      // 服务器信息
        USER_LIST = 12, // 用户列表
        CREATE_ROOM = 13, // 创建聊天室
        JOIN_ROOM = 14, // 加入聊天室
        LEAVE_ROOM = 15, // 离开聊天室
        ROOM_LIST = 16, // 聊天室列表
        ROOM_MEMBERS = 17, // 聊天室成员
        ROOM_MESSAGE = 18, // 聊天室消息
        FILE_TRANSFER = 19, // 文件传输
        FILE_REQUEST = 20, // 文件请求
        FILE_RESPONSE = 21, // 文件响应
        FILE_DATA = 22, // 文件数据
        READ_RECEIPT = 23 // 已读回执
    };
    Type type;          // 消息类型
    std::string message_id; // 消息ID
    std::unordered_map<std::string, bool> read_status; // 已读状态，键为用户名，值为是否已读
};

// 聊天室结构
struct ChatRoom {
    std::string name;                         // 聊天室名称
    std::string creator;                      // 创建者
    std::vector<std::string> members;         // 成员列表
    std::chrono::system_clock::time_point create_time;  // 创建时间
    bool is_private;                          // 是否为私有聊天室
    std::string password;                     // 私有聊天室密码（加密存储）
};

// 客户端信息结构
struct ClientInfo {
    int socket_fd;                             // 客户端socket文件描述符
    std::string name;                         // 客户端名称
    std::string password;                     // 客户端密码（加密存储）
    std::string ip_address;                   // IP地址
    uint16_t port;                            // 端口
    std::chrono::system_clock::time_point connect_time;  // 连接时间
    bool is_logged_in;                        // 是否已登录
    bool is_admin;                            // 是否为管理员
    bool is_muted;                            // 是否被禁言
    std::chrono::system_clock::time_point mute_end_time; // 禁言结束时间
    std::string current_room;                 // 当前所在聊天室
};

// 协议相关常量
class ChatProtocol {
public:
    static const std::string MSG_SEPARATOR;   // 消息分隔符
    static const std::string FIELD_SEPARATOR; // 字段分隔符
    static const size_t HEADER_SIZE = 8;      // 消息头部大小：4字节长度 + 4字节消息类型

    // 编码解码方法（在server.cpp中实现）
    static std::string encode(const ChatMessage& message);
    static bool decode(const std::string& data, ChatMessage& message);
    static std::string encodeSystemMessage(const std::string& message);
    static std::string encodeWelcome();
    static std::string encodeUserList(const std::vector<std::string>& users);
    static std::string encodeError(const std::string& error);
    static std::string encodeSuccess(const std::string& message);
    static std::string encodeLoginWithPass(const std::string& username, const std::string& password);
    static std::string encodeRegister(const std::string& username, const std::string& password);
    static std::string encodeCreateRoom(const std::string& room_name, bool is_private, const std::string& password);
    static std::string encodeJoinRoom(const std::string& room_name, const std::string& password);
    static std::string encodeLeaveRoom(const std::string& room_name);
    static std::string encodeRoomList(const std::vector<std::string>& rooms);
    static std::string encodeRoomMembers(const std::string& room_name, const std::vector<std::string>& members);
    static std::string encodeFileTransfer(const std::string& sender, const std::string& receiver, const std::string& file_name, uint64_t file_size);
    static std::string encodeFileRequest(const std::string& sender, const std::string& receiver, const std::string& file_name);
    static std::string encodeFileResponse(const std::string& sender, const std::string& receiver, const std::string& file_name, bool accepted);
    static std::string encodeFileData(const std::string& sender, const std::string& receiver, const std::string& file_name, uint64_t file_offset, const std::string& file_data, bool complete);
    static std::string encodeReadReceipt(const std::string& message_id, const std::string& user);
    
    // 工具方法
    static uint32_t getMessageLength(const std::string& data);
    static int getMessageType(const std::string& data);
    static std::string encodeCommand(const std::string& command);
};

// 回调类型定义
typedef std::function<void(const ChatMessage&)> MessageCallback;
typedef std::function<void(const ClientInfo&)> ClientCallback;
typedef std::function<void(const std::string&)> SystemMessageCallback;
typedef std::function<void(const std::vector<std::string>&)> UserListCallback;
typedef std::function<void(const std::string&)> ErrorCallback;

// 工具函数
namespace utils {
    // 格式化时间戳
    std::string formatTimestamp(const std::chrono::system_clock::time_point& timestamp);
    
    // 生成随机ID
    std::string generateRandomId(int length = 8);
    
    // 字符串工具
    bool startsWith(const std::string& str, const std::string& prefix);
    std::string trim(const std::string& str);
    
    // 表情处理
    std::string replaceEmojis(const std::string& text);
    std::vector<std::string> getAvailableEmojis();
}

} // namespace smallchat

#endif // SMALLCHAT_COMMON_H
