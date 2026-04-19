#include "client.h"
#include <iostream>
#include <thread>
#include <csignal>
#include <atomic>

using namespace smallchat;

std::unique_ptr<ChatClient> g_client;
std::atomic<bool> g_logged_in(false);
std::atomic<bool> g_quit(false);
std::string g_current_user;

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nDisconnecting..." << std::endl;
        g_quit = true;
        std::cin.clear();
        std::cin.ignore();
        if (g_client) {
            g_client->disconnect();
        }
    }
}

void printHelp() {
    std::cout << "\nCommands:\n";
    std::cout << "  /login <name> [password] (/l)    - Login with a name and optional password\n";
    std::cout << "  /register <name> <password> (/r) - Register a new user\n";
    std::cout << "  /users (/u)           - List online users\n";
    std::cout << "  /whisper <name> <msg> (/w) - Send private message\n";
    std::cout << "  /help (/h)            - Show this help\n";
    std::cout << "  /quit (/q)            - Disconnect and exit\n";
    std::cout << "  /nick <name> (/n)     - Change nickname\n";
    std::cout << "  /info                 - Show server info\n";
    std::cout << "  /createroom <name> [private] [password] (/cr) - Create a chat room\n";
    std::cout << "  /joinroom <name> [password] (/jr) - Join a chat room\n";
    std::cout << "  /leaveroom (/lr)      - Leave current chat room\n";
    std::cout << "  /rooms (/ro)          - List all chat rooms\n";
    std::cout << "  /roommembers (/rm)    - List members in current chat room\n";
    std::cout << "  /transfer <file> <name> (/t) - Transfer file to user\n";
    std::cout << "  /file <file>          - Send file to current chat room\n";
    std::cout << "  /history [count]      - Get message history\n";
    std::cout << "  /roomhistory [count]  - Get current room message history\n";
    std::cout << "  /mute <name> [time]   - Mute user (admin only)\n";
    std::cout << "  /unmute <name>        - Unmute user (admin only)\n";
    std::cout << "  /kick <name> [reason] - Kick user (admin only)\n";
    std::cout << "  <message>         - Send broadcast message\n";
}

std::string getPrompt() {
    if (g_logged_in) {
        return "[" + g_current_user + "] > ";
    }
    return "> ";
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
                      << ": " << message.content << "\n" << getPrompt() << std::flush;
        } else {
            std::cout << "\r[" << message.sender << "]: " << message.content 
                      << "\n" << getPrompt() << std::flush;
        }
    });
    
    // 设置系统消息回调
    g_client->setSystemMessageCallback([](const std::string& message) {
        std::cout << "\r[SYS] " << message << "\n" << getPrompt() << std::flush;
    });
    
    // 设置用户列表回调
    g_client->setUserListCallback([](const std::vector<std::string>& users) {
        std::cout << "\r[Users] Online: ";
        for (size_t i = 0; i < users.size(); ++i) {
            std::cout << users[i];
            if (i < users.size() - 1) std::cout << ", ";
        }
        std::cout << "\n" << getPrompt() << std::flush;
    });
    
    // 设置错误回调
    g_client->setErrorCallback([](const std::string& error) {
        std::cout << "\r[Error] " << error << "\n" << getPrompt() << std::flush;
    });
    
    // 设置成功消息回调（用于检测登录成功）
    g_client->setSuccessCallback([](const std::string& message) {
        std::cout << "\r[OK] " << message << "\n" << getPrompt() << std::flush;
        // 检查是否是登录成功消息
        size_t logged_in_pos = message.find("Logged in as ");
        if (logged_in_pos != std::string::npos) {
            g_logged_in = true;
            g_current_user = message.substr(logged_in_pos + 13);
        } else if (message.find("registered successfully") != std::string::npos) {
            g_logged_in = true;
            size_t colon_pos = message.find(": ");
            if (colon_pos != std::string::npos) {
                g_current_user = message.substr(colon_pos + 2);
            }
        }
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
    
    std::cout << "\nWelcome to SmallChat! Please login first.\n";
    std::cout << "Use: /login <name> or /register <name> <password>\n";
    std::cout << getPrompt() << std::flush;
    
    // 主循环 - 读取用户输入
    std::string input;
    while (g_client->isConnected() && !g_quit) {
        std::getline(std::cin, input);
        
        if (g_quit) {
            break;
        }
        
        if (input.empty()) {
            std::cout << getPrompt() << std::flush;
            continue;
        }
        
        // 检查是否是允许的命令（未登录时）
        bool is_allowed_cmd = (input == "/quit" || input == "/q" ||
                              input == "/help" || input == "/h" || 
                              input == "/info" || input == "/i" ||
                              input.substr(0, 7) == "/login " || input.substr(0, 3) == "/l " ||
                              input.substr(0, 10) == "/register " || input.substr(0, 3) == "/r ");
        
        // 如果未登录且不是允许的命令，提示登录
        if (!g_logged_in && !is_allowed_cmd) {
            std::cout << "[Error] Please login first. Use /login <name> or /register <name> <password>\n";
            std::cout << getPrompt() << std::flush;
            continue;
        }
        
        if (input == "/quit" || input == "/q" || input == "/Q") {
            std::cout << "\nDisconnecting..." << std::endl;
            g_client->disconnect();  // 先断开连接（关闭socket，唤醒recv）
            g_client->stop();        // 然后停止线程（等待线程结束）
            std::cout << "Disconnected." << std::endl;
            return 0;
        } else if (input == "/help" || input == "/h") {
            printHelp();
        } else if (input == "/users" || input == "/u") {
            g_client->requestUserList();
        } else if (input == "/info" || input == "/i") {
            g_client->sendMessage("/info");
        } else {
            g_client->sendMessage(input);
            
            // 检查是否是登录成功
            if ((input.substr(0, 7) == "/login " || input.substr(0, 3) == "/l ") && !g_logged_in) {
                // 提取用户名
                size_t pos = input.find(' ');
                if (pos != std::string::npos) {
                    std::string name = input.substr(pos + 1);
                    // 移除密码部分
                    pos = name.find(' ');
                    if (pos != std::string::npos) {
                        name = name.substr(0, pos);
                    }
                    g_current_user = name;
                }
            }
        }
        
        std::cout << getPrompt() << std::flush;
    }
    
    return 0;
}