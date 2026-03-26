#include "client.h"
#include <iostream>
#include <thread>
#include <csignal>

using namespace smallchat;

std::unique_ptr<ChatClient> g_client;

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nDisconnecting..." << std::endl;
        if (g_client) {
            g_client->disconnect();
        }
    }
}

void printHelp() {
    std::cout << "\nCommands:\n";
    std::cout << "  /login <name>     - Login with a name\n";
    std::cout << "  /users            - List online users\n";
    std::cout << "  /whisper <name>    - Send private message\n";
    std::cout << "  /help             - Show this help\n";
    std::cout << "  /quit             - Disconnect and exit\n";
    std::cout << "  <message>         - Send broadcast message\n";
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 获取参数
    std::string host = "127.0.0.1";
    uint16_t port = 8888;
    
    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }
    
    // 创建客户端
    g_client = std::make_unique<ChatClient>();
    
    // 设置消息回调
    g_client->setMessageCallback([](const ChatMessage& message) {
        if (message.type == ChatMessage::Type::PRIVATE) {
            std::cout << "\r[Private] " << message.sender << " -> " << message.receiver 
                      << ": " << message.content << "\n> " << std::flush;
        } else {
            std::cout << "\r[" << message.sender << "]: " << message.content 
                      << "\n> " << std::flush;
        }
    });
    
    // 设置系统消息回调
    g_client->setSystemMessageCallback([](const std::string& message) {
        std::cout << "\r[SYS] " << message << "\n> " << std::flush;
    });
    
    // 设置用户列表回调
    g_client->setUserListCallback([](const std::vector<std::string>& users) {
        std::cout << "\r[Users] Online: ";
        for (size_t i = 0; i < users.size(); ++i) {
            std::cout << users[i];
            if (i < users.size() - 1) std::cout << ", ";
        }
        std::cout << "\n> " << std::flush;
    });
    
    // 设置错误回调
    g_client->setErrorCallback([](const std::string& error) {
        std::cout << "\r[Error] " << error << "\n> " << std::flush;
    });
    
    // 连接服务器
    std::cout << "Connecting to " << host << ":" << port << "..." << std::endl;
    
    if (!g_client->connect(host, port)) {
        std::cerr << "Failed to connect to server" << std::endl;
        return 1;
    }
    
    std::cout << "Connected!" << std::endl;
    
    // 启动接收线程
    g_client->start();
    
    printHelp();
    std::cout << "\n> " << std::flush;
    
    // 主循环 - 读取用户输入
    std::string input;
    while (g_client->isConnected()) {
        std::getline(std::cin, input);
        
        if (input.empty()) {
            continue;
        }
        
        if (input == "/quit") {
            break;
        } else if (input == "/help") {
            printHelp();
        } else if (input == "/users") {
            g_client->requestUserList();
        } else if (input.substr(0, 6) == "/login") {
            g_client->sendMessage(input);
        } else if (input.substr(0, 9) == "/whisper") {
            g_client->sendMessage(input);
        } else {
            g_client->sendMessage(input);
        }
        
        std::cout << "> " << std::flush;
    }
    
    // 断开连接
    g_client->stop();
    g_client->disconnect();
    
    std::cout << "Disconnected." << std::endl;
    
    return 0;
}
