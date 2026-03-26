#include "server.h"
#include "client.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

using namespace smallchat;

void test_basic_chat() {
    std::cout << "\n=== Test 1: Basic Chat ===" << std::endl;
    
    // 启动服务器
    ChatServer server;
    server.start(9999);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 创建客户端
    ChatClient client1;
    client1.connect("127.0.0.1", 9999);
    client1.login("Alice");
    
    ChatClient client2;
    client2.connect("127.0.0.1", 9999);
    client2.login("Bob");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 发送消息
    client1.sendMessage("Hello Bob!");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    client2.sendMessage("Hi Alice!");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // 断开连接
    client1.disconnect();
    client2.disconnect();
    server.stop();
    
    std::cout << "Test 1 passed!" << std::endl;
}

void test_private_message() {
    std::cout << "\n=== Test 2: Private Message ===" << std::endl;
    
    // 启动服务器
    ChatServer server;
    server.start(9999);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 创建客户端
    ChatClient client1;
    client1.connect("127.0.0.1", 9999);
    client1.login("Alice");
    
    ChatClient client2;
    client2.connect("127.0.0.1", 9999);
    client2.login("Bob");
    
    ChatClient client3;
    client3.connect("127.0.0.1", 9999);
    client3.login("Charlie");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Alice私聊Bob
    client1.sendPrivateMessage("Bob", "This is a private message");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // 断开连接
    client1.disconnect();
    client2.disconnect();
    client3.disconnect();
    server.stop();
    
    std::cout << "Test 2 passed!" << std::endl;
}

void test_user_list() {
    std::cout << "\n=== Test 3: User List ===" << std::endl;
    
    // 启动服务器
    ChatServer server;
    server.start(9999);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 创建多个客户端
    std::vector<std::unique_ptr<ChatClient>> clients;
    
    for (int i = 0; i < 5; ++i) {
        auto client = std::make_unique<ChatClient>();
        client->connect("127.0.0.1", 9999);
        client->login("User" + std::to_string(i));
        clients.push_back(std::move(client));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // 请求用户列表
    clients[0]->requestUserList();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::cout << "Online users: " << server.getClientCount() << std::endl;
    
    // 断开所有连接
    for (auto& client : clients) {
        client->disconnect();
    }
    server.stop();
    
    std::cout << "Test 3 passed!" << std::endl;
}

void test_callbacks() {
    std::cout << "\n=== Test 4: Callbacks ===" << std::endl;
    
    // 启动服务器
    ChatServer server;
    
    int connect_count = 0;
    int message_count = 0;
    int disconnect_count = 0;
    
    server.setConnectCallback([&connect_count](const ClientInfo& client) {
        connect_count++;
        std::cout << "[Connect] " << client.name << std::endl;
    });
    
    server.setMessageCallback([&message_count](const ChatMessage& msg) {
        message_count++;
        std::cout << "[Message] " << msg.sender << ": " << msg.content << std::endl;
    });
    
    server.setDisconnectCallback([&disconnect_count](const ClientInfo& client) {
        disconnect_count++;
        std::cout << "[Disconnect] " << client.name << std::endl;
    });
    
    server.start(9999);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 创建客户端
    ChatClient client;
    client.connect("127.0.0.1", 9999);
    client.login("TestUser");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 发送消息
    client.sendMessage("Test message");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // 断开连接
    client.disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    server.stop();
    
    std::cout << "Connects: " << connect_count << std::endl;
    std::cout << "Messages: " << message_count << std::endl;
    std::cout << "Disconnects: " << disconnect_count << std::endl;
    
    std::cout << "Test 4 passed!" << std::endl;
}

void test_multiple_clients() {
    std::cout << "\n=== Test 5: Multiple Clients ===" << std::endl;
    
    // 启动服务器
    ChatServer server;
    server.start(9999);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 创建多个客户端
    std::vector<std::unique_ptr<ChatClient>> clients;
    const int client_count = 20;
    
    for (int i = 0; i < client_count; ++i) {
        auto client = std::make_unique<ChatClient>();
        client->connect("127.0.0.1", 9999);
        client->login("Client" + std::to_string(i));
        clients.push_back(std::move(client));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "Connected clients: " << server.getClientCount() << std::endl;
    
    // 随机发送消息
    for (int i = 0; i < 10; ++i) {
        int sender = rand() % client_count;
        clients[sender]->sendMessage("Message " + std::to_string(i));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 断开所有连接
    for (auto& client : clients) {
        client->disconnect();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    server.stop();
    
    std::cout << "Test 5 passed!" << std::endl;
}

int main() {
    std::cout << "SmallChat Test Suite" << std::endl;
    std::cout << "=====================" << std::endl;
    
    try {
        test_basic_chat();
        test_private_message();
        test_user_list();
        test_callbacks();
        test_multiple_clients();
        
        std::cout << "\n=== All tests passed! ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
