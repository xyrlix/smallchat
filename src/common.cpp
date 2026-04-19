#include "common.h"
#include <sstream>
#include <iomanip>
#include <random>
#include <algorithm>
#include <cctype>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace smallchat {

// 协议常量定义
const std::string ChatProtocol::MSG_SEPARATOR = "\n";
const std::string ChatProtocol::FIELD_SEPARATOR = "|";

// Base64编码函数
std::string base64Encode(const std::string& input) {
    const std::string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    std::string result;
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    
    for (char c : input) {
        char_array_3[i++] = c;
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for (i = 0; i < 4; i++) {
                result += base64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }
    
    if (i > 0) {
        for (int j = i; j < 3; j++) {
            char_array_3[j] = '\0';
        }
        
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;
        
        for (int j = 0; j < i + 1; j++) {
            result += base64_chars[char_array_4[j]];
        }
        
        while (i++ < 3) {
            result += '=';
        }
    }
    
    return result;
}

// Base64解码函数
std::string base64Decode(const std::string& input) {
    const std::string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    std::string result;
    int i = 0;
    unsigned char char_array_4[4];
    unsigned char char_array_3[3];
    
    for (char c : input) {
        if (c == '=') {
            break;
        }
        
        char_array_4[i++] = base64_chars.find(c);
        if (i == 4) {
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            
            for (i = 0; i < 3; i++) {
                result += char_array_3[i];
            }
            i = 0;
        }
    }
    
    if (i > 0) {
        for (int j = i; j < 4; j++) {
            char_array_4[j] = 0;
        }
        
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
        
        for (int j = 0; j < i - 1; j++) {
            result += char_array_3[j];
        }
    }
    
    return result;
}

// 编码消息 - 对内容进行Base64编码以防止特殊字符干扰
std::string ChatProtocol::encode(const ChatMessage& message) {
    std::ostringstream oss;
    // sender和receiver使用原始值，content进行Base64编码
    oss << "MSG|" << message.sender << "|" << message.receiver << "|"
        << static_cast<int>(message.type) << "|"
        << base64Encode(message.content) << "|" << message.message_id << "\n";
    return oss.str();
}

// 解码消息 - 对content进行Base64解码
bool ChatProtocol::decode(const std::string& data, ChatMessage& message) {
    size_t pos = 0;

    // 跳过MSG前缀
    if (data.substr(0, 3) != "MSG") {
        return false;
    }
    pos += 3;

    // 跳过FIELD_SEPARATOR
    if (pos < data.size() && data[pos] != FIELD_SEPARATOR[0]) {
        return false;
    }
    pos++;

    // 解析sender
    size_t end = data.find(FIELD_SEPARATOR, pos);
    if (end == std::string::npos) {
        return false;
    }
    message.sender = data.substr(pos, end - pos);
    pos = end + 1;

    // 解析receiver
    end = data.find(FIELD_SEPARATOR, pos);
    if (end == std::string::npos) {
        return false;
    }
    message.receiver = data.substr(pos, end - pos);
    pos = end + 1;

    // 解析type
    end = data.find(FIELD_SEPARATOR, pos);
    if (end == std::string::npos) {
        return false;
    }
    std::string type_str = data.substr(pos, end - pos);
    try {
        int type_int = std::stoi(type_str);
        message.type = static_cast<ChatMessage::Type>(type_int);
    } catch (...) {
        return false;
    }
    pos = end + 1;

    // 解析content（Base64编码过的内容）
    end = data.find(FIELD_SEPARATOR, pos);
    if (end == std::string::npos) {
        return false;
    }
    {
        std::string encoded_content = data.substr(pos, end - pos);
        message.content = base64Decode(encoded_content);
    }
    pos = end + 1;

    // 解析message_id（读取到字符串末尾，去掉末尾的换行符）
    message.message_id = data.substr(pos);
    if (!message.message_id.empty() && message.message_id.back() == '\n') {
        message.message_id.pop_back();
    }

    // 设置时间戳
    message.timestamp = std::chrono::system_clock::now();

    return true;
}

// 获取消息长度
uint32_t ChatProtocol::getMessageLength(const std::string& data) {
    if (data.size() < HEADER_SIZE) {
        return 0;
    }
    uint32_t length = 0;
    memcpy(&length, data.data(), 4);
    return ntohl(length);
}

// 获取消息类型
int ChatProtocol::getMessageType(const std::string& data) {
    if (data.size() < HEADER_SIZE) {
        return -1;
    }
    int type = 0;
    memcpy(&type, data.data() + 4, 4);
    return ntohl(type);
}

// 编码命令消息
std::string ChatProtocol::encodeCommand(const std::string& command) {
    return command + MSG_SEPARATOR;
}

// 编码系统消息
std::string ChatProtocol::encodeSystemMessage(const std::string& message) {
    return "SYS" + FIELD_SEPARATOR + message + MSG_SEPARATOR;
}

// 编码欢迎消息
std::string ChatProtocol::encodeWelcome() {
    return "WELCOME" + FIELD_SEPARATOR + 
           "Welcome to SmallChat Server!" + FIELD_SEPARATOR + 
           "Commands: /login <name>, /users, /whisper <name> <msg>, /help, /quit" + MSG_SEPARATOR;
}

// 编码用户列表
std::string ChatProtocol::encodeUserList(const std::vector<std::string>& users) {
    std::ostringstream oss;
    oss << "USERS" << FIELD_SEPARATOR << users.size();
    for (const auto& user : users) {
        oss << FIELD_SEPARATOR << user;
    }
    oss << MSG_SEPARATOR;
    return oss.str();
}

// 编码错误消息
std::string ChatProtocol::encodeError(const std::string& error) {
    return "ERROR" + FIELD_SEPARATOR + error + MSG_SEPARATOR;
}

// 编码成功消息
std::string ChatProtocol::encodeSuccess(const std::string& message) {
    return "OK" + FIELD_SEPARATOR + message + MSG_SEPARATOR;
}

// 编码带密码的登录消息
std::string ChatProtocol::encodeLoginWithPass(const std::string& username, const std::string& password) {
    std::ostringstream oss;
    oss << "LOGIN_WITH_PASS" << FIELD_SEPARATOR 
        << username << FIELD_SEPARATOR 
        << password << MSG_SEPARATOR;
    return oss.str();
}

// 编码注册消息
std::string ChatProtocol::encodeRegister(const std::string& username, const std::string& password) {
    std::ostringstream oss;
    oss << "REGISTER" << FIELD_SEPARATOR 
        << username << FIELD_SEPARATOR 
        << password << MSG_SEPARATOR;
    return oss.str();
}

// 编码创建聊天室消息
std::string ChatProtocol::encodeCreateRoom(const std::string& room_name, bool is_private, const std::string& password) {
    std::ostringstream oss;
    oss << "CREATE_ROOM" << FIELD_SEPARATOR 
        << room_name << FIELD_SEPARATOR 
        << (is_private ? "1" : "0") << FIELD_SEPARATOR 
        << password << MSG_SEPARATOR;
    return oss.str();
}

// 编码加入聊天室消息
std::string ChatProtocol::encodeJoinRoom(const std::string& room_name, const std::string& password) {
    std::ostringstream oss;
    oss << "JOIN_ROOM" << FIELD_SEPARATOR 
        << room_name << FIELD_SEPARATOR 
        << password << MSG_SEPARATOR;
    return oss.str();
}

// 编码离开聊天室消息
std::string ChatProtocol::encodeLeaveRoom(const std::string& room_name) {
    std::ostringstream oss;
    oss << "LEAVE_ROOM" << FIELD_SEPARATOR 
        << room_name << MSG_SEPARATOR;
    return oss.str();
}

// 编码聊天室列表消息
std::string ChatProtocol::encodeRoomList(const std::vector<std::string>& rooms) {
    std::ostringstream oss;
    oss << "ROOM_LIST" << FIELD_SEPARATOR << rooms.size() << MSG_SEPARATOR;
    for (const auto& room : rooms) {
        oss << room << MSG_SEPARATOR;
    }
    return oss.str();
}

// 编码聊天室成员消息
std::string ChatProtocol::encodeRoomMembers(const std::string& room_name, const std::vector<std::string>& members) {
    std::ostringstream oss;
    oss << "ROOM_MEMBERS" << FIELD_SEPARATOR << room_name << FIELD_SEPARATOR << members.size() << MSG_SEPARATOR;
    for (const auto& member : members) {
        oss << member << MSG_SEPARATOR;
    }
    return oss.str();
}

// 编码文件传输消息
std::string ChatProtocol::encodeFileTransfer(const std::string& sender, const std::string& receiver, const std::string& file_name, uint64_t file_size) {
    std::ostringstream oss;
    oss << "FILE_TRANSFER" << FIELD_SEPARATOR 
        << sender << FIELD_SEPARATOR 
        << receiver << FIELD_SEPARATOR 
        << file_name << FIELD_SEPARATOR 
        << file_size << MSG_SEPARATOR;
    return oss.str();
}

// 编码文件请求消息
std::string ChatProtocol::encodeFileRequest(const std::string& sender, const std::string& receiver, const std::string& file_name) {
    std::ostringstream oss;
    oss << "FILE_REQUEST" << FIELD_SEPARATOR 
        << sender << FIELD_SEPARATOR 
        << receiver << FIELD_SEPARATOR 
        << file_name << MSG_SEPARATOR;
    return oss.str();
}

// 编码文件响应消息
std::string ChatProtocol::encodeFileResponse(const std::string& sender, const std::string& receiver, const std::string& file_name, bool accepted) {
    std::ostringstream oss;
    oss << "FILE_RESPONSE" << FIELD_SEPARATOR 
        << sender << FIELD_SEPARATOR 
        << receiver << FIELD_SEPARATOR 
        << file_name << FIELD_SEPARATOR 
        << (accepted ? "1" : "0") << MSG_SEPARATOR;
    return oss.str();
}

// 编码文件数据消息
std::string ChatProtocol::encodeFileData(const std::string& sender, const std::string& receiver, const std::string& file_name, uint64_t file_offset, const std::string& file_data, bool complete) {
    std::ostringstream oss;
    oss << "FILE_DATA" << FIELD_SEPARATOR 
        << sender << FIELD_SEPARATOR 
        << receiver << FIELD_SEPARATOR 
        << file_name << FIELD_SEPARATOR 
        << file_offset << FIELD_SEPARATOR 
        << base64Encode(file_data) << FIELD_SEPARATOR 
        << (complete ? "1" : "0") << MSG_SEPARATOR;
    return oss.str();
}

// 编码已读回执消息
std::string ChatProtocol::encodeReadReceipt(const std::string& message_id, const std::string& user) {
    std::ostringstream oss;
    oss << "READ_RECEIPT" << FIELD_SEPARATOR 
        << message_id << FIELD_SEPARATOR 
        << user << MSG_SEPARATOR;
    return oss.str();
}

namespace utils {
    // 格式化时间戳
    std::string formatTimestamp(const std::chrono::system_clock::time_point& timestamp) {
        auto time_t_time = std::chrono::system_clock::to_time_t(timestamp);
        std::tm tm_time;
        #ifdef _WIN32
        localtime_s(&tm_time, &time_t_time);
        #else
        localtime_r(&time_t_time, &tm_time);
        #endif
        std::ostringstream oss;
        oss << std::put_time(&tm_time, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }
    
    // 生成随机ID
    std::string generateRandomId(int length) {
        const std::string charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, charset.size() - 1);
        
        std::string id;
        for (int i = 0; i < length; ++i) {
            id += charset[dis(gen)];
        }
        return id;
    }
    
    // 字符串工具
    bool startsWith(const std::string& str, const std::string& prefix) {
        return str.size() >= prefix.size() && str.substr(0, prefix.size()) == prefix;
    }
    
    std::string trim(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) {
            return "";
        }
        size_t end = str.find_last_not_of(" \t\n\r");
        return str.substr(start, end - start + 1);
    }
    
    // 表情映射
    static std::unordered_map<std::string, std::string> emoji_map = {
        {":)", "😊"},
        {":(", "😢"},
        {":D", "😄"},
        {";)", "😉"},
        {":P", "😛"},
        {":O", "😮"},
        {":S", "😕"},
        {":|" , "😐"},
        {":-)", "😊"},
        {":-(", "😢"},
        {":-D", "😄"},
        {":-)" , "😉"},
        {":-P", "😛"},
        {":-O", "😮"},
        {":-S", "😕"},
        {":-|" , "😐"},
        {"<3", "❤️"},
        {"<\\3", "💔"},
        {":-*", "😘"},
        {":*", "😘"},
        {"B)" , "😎"},
        {"B-)" , "😎"}
    };
    
    // 替换文本中的表情符号
    std::string replaceEmojis(const std::string& text) {
        std::string result = text;
        
        for (const auto& [emoji_code, emoji] : emoji_map) {
            size_t pos = 0;
            while ((pos = result.find(emoji_code, pos)) != std::string::npos) {
                result.replace(pos, emoji_code.length(), emoji);
                pos += emoji.length();
            }
        }
        
        return result;
    }
    
    // 获取可用的表情符号列表
    std::vector<std::string> getAvailableEmojis() {
        std::vector<std::string> emojis;
        for (const auto& [emoji_code, emoji] : emoji_map) {
            emojis.push_back(emoji_code);
        }
        return emojis;
    }
}

} // namespace smallchat
