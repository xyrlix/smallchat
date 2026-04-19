#ifndef SMALLCHAT_WEB_SERVER_HPP
#define SMALLCHAT_WEB_SERVER_HPP

#include <libwebsockets.h>
#include <string>
#include <thread>

namespace smallchat {

class ChatServer;

class WebServer {
public:
    WebServer(uint16_t port, ChatServer& chat_server);
    ~WebServer();
    
    bool start();
    void stop();
    
private:
    void run();
    
    ChatServer& chat_server_;
    uint16_t port_;
    struct lws_context* context_;
    std::thread service_thread_;
};

} // namespace smallchat

#endif // SMALLCHAT_WEB_SERVER_HPP