#include "server.h"
#include <iostream>
#include <csignal>
#include <memory>

using namespace smallchat;

std::unique_ptr<ChatServer> g_server;

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nShutting down server..." << std::endl;
        if (g_server) {
            g_server->stop();
        }
    }
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 获取端口
    uint16_t port = 8888;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }
    
    // 创建服务器
    g_server = std::make_unique<ChatServer>();
    
    // 设置消息回调
    g_server->setMessageCallback([](const ChatMessage& message) {
        std::string timestamp = std::to_string(
            std::chrono::system_clock::to_time_t(message.timestamp));
        
        std::cout << "[" << timestamp << "] ";
        if (message.type == ChatMessage::Type::PRIVATE) {
            std::cout << "[Private] " << message.sender << " -> " << message.receiver;
        } else {
            std::cout << "[Broadcast] " << message.sender;
        }
        std::cout << ": " << message.content << std::endl;
    });
    
    // 设置连接回调
    g_server->setConnectCallback([](const ClientInfo& client) {
        std::cout << "Client connected: " << client.ip_address << ":" << client.port
                  << " (fd: " << client.socket_fd << ")" << std::endl;
    });
    
    // 设置断开回调
    g_server->setDisconnectCallback([](const ClientInfo& client) {
        std::cout << "Client disconnected: " << client.name << std::endl;
    });
    
    // 启动服务器
    std::cout << "Starting SmallChat Server on port " << port << "..." << std::endl;
    
    if (!g_server->start(port)) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    std::cout << "Server is running. Press Ctrl+C to stop." << std::endl;
    
    // 主循环
    while (g_server->isRunning()) {
        sleep(1);
        
        // 显示状态
        size_t client_count = g_server->getClientCount();
        if (client_count > 0) {
            std::cout << "\rOnline clients: " << client_count << "          " << std::flush;
        }
    }
    
    std::cout << "\nServer stopped." << std::endl;
    
    return 0;
}
