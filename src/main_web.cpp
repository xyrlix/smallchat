#include <iostream>
#include "web_server.hpp"
#include "server.h"

int main(int argc, char* argv[]) {
    try {
        smallchat::ChatServer chat_server;
        chat_server.start(8888);
        
        smallchat::WebServer web_server(8080, chat_server);
        if (!web_server.start()) {
            return 1;
        }
        
        std::cout << "Press Ctrl+C to stop..." << std::endl;
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}