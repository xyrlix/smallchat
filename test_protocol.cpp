#include "common.h"
#include <iostream>

int main() {
    // 创建一个测试消息
    smallchat::ChatMessage msg;
    msg.sender = "test_sender";
    msg.receiver = "test_receiver";
    msg.content = "Hello, World!";
    msg.type = smallchat::ChatMessage::Type::PRIVATE;
    msg.message_id = "1234567890";
    
    // 编码消息
    std::string encoded = smallchat::ChatProtocol::encode(msg);
    std::cout << "Encoded message: " << encoded << std::endl;
    
    // 解码消息
    smallchat::ChatMessage decoded_msg;
    if (smallchat::ChatProtocol::decode(encoded, decoded_msg)) {
        std::cout << "Decoded message:" << std::endl;
        std::cout << "  Sender: " << decoded_msg.sender << std::endl;
        std::cout << "  Receiver: " << decoded_msg.receiver << std::endl;
        std::cout << "  Content: " << decoded_msg.content << std::endl;
        std::cout << "  Type: " << static_cast<int>(decoded_msg.type) << std::endl;
        std::cout << "  Message ID: " << decoded_msg.message_id << std::endl;
    } else {
        std::cerr << "Failed to decode message" << std::endl;
        return 1;
    }
    
    return 0;
}