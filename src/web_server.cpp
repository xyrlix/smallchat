#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <libwebsockets.h>

#include "web_server.hpp"
#include "server.h"

namespace smallchat {

static ChatServer* g_chat_server = nullptr;
static std::unordered_map<std::string, struct lws*> g_ws_sessions;
static std::mutex g_ws_mutex;

static void websocket_broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_ws_mutex);
    for (auto& pair : g_ws_sessions) {
        lws* wsi = pair.second;
        std::string msg = message;
        unsigned char* buf = new unsigned char[LWS_PRE + msg.size()];
        memcpy(buf + LWS_PRE, msg.c_str(), msg.size());
        lws_write(wsi, buf + LWS_PRE, msg.size(), LWS_WRITE_TEXT);
        delete[] buf;
    }
}

static void websocket_send_to(const std::string& client_name, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_ws_mutex);
    auto it = g_ws_sessions.find(client_name);
    if (it != g_ws_sessions.end()) {
        lws* wsi = it->second;
        std::string msg = message;
        unsigned char* buf = new unsigned char[LWS_PRE + msg.size()];
        memcpy(buf + LWS_PRE, msg.c_str(), msg.size());
        lws_write(wsi, buf + LWS_PRE, msg.size(), LWS_WRITE_TEXT);
        delete[] buf;
    }
}

struct SessionData {
    std::string username;
};

static const char* html_page = 
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"    <title>SmallChat</title>\n"
"    <style>\n"
"        body { font-family: Arial, sans-serif; max-width: 800px; margin: 0 auto; padding: 20px; }\n"
"        #login-panel { display: block; }\n"
"        #chat-panel { display: none; }\n"
"        #messages { height: 300px; overflow-y: scroll; border: 1px solid #ccc; padding: 10px; margin: 10px 0; }\n"
"        .message { margin: 5px 0; padding: 5px; border-radius: 5px; }\n"
"        .system { color: #888; font-style: italic; }\n"
"        .private { background: #e6f2ff; }\n"
"        .broadcast { background: #f0f0f0; }\n"
"        .my-message { background: #e6ffe6; text-align: right; }\n"
"        input[type=text] { width: 70%; padding: 8px; }\n"
"        button { padding: 8px 16px; }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <div id=\"login-panel\">\n"
"        <h1>SmallChat</h1>\n"
"        <input type=\"text\" id=\"username\" placeholder=\"Enter username\">\n"
"        <button onclick=\"login()\">Login</button>\n"
"    </div>\n"
"    \n"
"    <div id=\"chat-panel\">\n"
"        <h2>Chat Room</h2>\n"
"        <div id=\"messages\"></div>\n"
"        <input type=\"text\" id=\"message-input\" placeholder=\"Enter message...\">\n"
"        <button onclick=\"sendMessage()\">Send</button>\n"
"        <button onclick=\"logout()\">Logout</button>\n"
"    </div>\n"
"    <script>\n"
"        var ws;\n"
"        var username = \"\";\n"
"        function login() {\n"
"            username = document.getElementById(\"username\").value.trim();\n"
"            if (!username) return;\n"
"            ws = new WebSocket(\"ws://\" + window.location.host + \"/ws\");\n"
"            ws.onopen = function() {\n"
"                ws.send(\"/login \" + username);\n"
"                document.getElementById(\"login-panel\").style.display = \"none\";\n"
"                document.getElementById(\"chat-panel\").style.display = \"block\";\n"
"                document.getElementById(\"message-input\").focus();\n"
"            };\n"
"            ws.onmessage = function(event) {\n"
"                addMessage(event.data);\n"
"            };\n"
"            ws.onclose = function() {\n"
"                addMessage(\"[SYS] Disconnected\");\n"
"                document.getElementById(\"chat-panel\").style.display = \"none\";\n"
"                document.getElementById(\"login-panel\").style.display = \"block\";\n"
"            };\n"
"        }\n"
"        function logout() {\n"
"            if (ws) {\n"
"                ws.send(\"/quit\");\n"
"                ws.close();\n"
"            }\n"
"        }\n"
"        function sendMessage() {\n"
"            var input = document.getElementById(\"message-input\");\n"
"            var message = input.value.trim();\n"
"            if (message && ws) {\n"
"                ws.send(message);\n"
"                addMessage(\"Me: \" + message, \"my-message\");\n"
"                input.value = \"\";\n"
"            }\n"
"        }\n"
"        function addMessage(text, className) {\n"
"            if (!className) className = \"broadcast\";\n"
"            var div = document.createElement(\"div\");\n"
"            div.className = \"message \" + className;\n"
"            if (text.indexOf(\"[SYS]\") == 0) {\n"
"                div.className = \"message system\";\n"
"            } else if (text.indexOf(\"->\") != -1) {\n"
"                div.className = \"message private\";\n"
"            }\n"
"            div.textContent = text;\n"
"            var messagesDiv = document.getElementById(\"messages\");\n"
"            messagesDiv.appendChild(div);\n"
"            messagesDiv.scrollTop = messagesDiv.scrollHeight;\n"
"        }\n"
"        document.getElementById(\"message-input\").addEventListener(\"keypress\", function(e) {\n"
"            if (e.keyCode == 13) sendMessage();\n"
"        });\n"
"        document.getElementById(\"username\").addEventListener(\"keypress\", function(e) {\n"
"            if (e.keyCode == 13) login();\n"
"        });\n"
"    </script>\n"
"</body>\n"
"</html>\n";

static int callback_http(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_HTTP: {
            const char* page = html_page;
            size_t page_len = strlen(page);
            unsigned char buf[LWS_PRE + 16384];
            memcpy(buf + LWS_PRE, page, page_len);
            lws_write(wsi, buf + LWS_PRE, page_len, LWS_WRITE_HTTP);
            lws_http_transaction_completed(wsi);
            break;
        }
        default:
            break;
    }
    return 0;
}

static int callback_websocket(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    SessionData* data = reinterpret_cast<SessionData*>(user);
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            std::cout << "[WebSocket] Client connected" << std::endl;
            break;
        }
        case LWS_CALLBACK_RECEIVE: {
            if (!g_chat_server) break;
            
            std::string message(static_cast<char*>(in), len);
            std::cout << "[WebSocket] Received: " << message << std::endl;
            
            if (!message.empty() && message[0] == '/') {
                std::string cmd = message;
                size_t space_pos = message.find(' ');
                if (space_pos != std::string::npos) {
                    cmd = message.substr(0, space_pos);
                }
                
                if (cmd == "/login") {
                    std::string username = message.substr(7);
                    size_t end_pos = username.find(' ');
                    if (end_pos != std::string::npos) {
                        username = username.substr(0, end_pos);
                    }
                    
                    if (g_chat_server->getClientByName(username)) {
                        std::string err = "[SYS] Error: Name already taken";
                        unsigned char* buf = new unsigned char[LWS_PRE + err.size()];
                        memcpy(buf + LWS_PRE, err.c_str(), err.size());
                        lws_write(wsi, buf + LWS_PRE, err.size(), LWS_WRITE_TEXT);
                        delete[] buf;
                        return 0;
                    }
                    
                    data->username = username;
                    {
                        std::lock_guard<std::mutex> lock(g_ws_mutex);
                        g_ws_sessions[username] = wsi;
                    }
                    
                    std::string welcome = "[SYS] Welcome, " + username + "! You are logged in.";
                    unsigned char* buf = new unsigned char[LWS_PRE + welcome.size()];
                    memcpy(buf + LWS_PRE, welcome.c_str(), welcome.size());
                    lws_write(wsi, buf + LWS_PRE, welcome.size(), LWS_WRITE_TEXT);
                    delete[] buf;
                    
                    g_chat_server->broadcast("[SYS] " + username + " joined the chat via WebSocket");
                    
                } else if (cmd == "/quit") {
                    if (!data->username.empty()) {
                        g_chat_server->broadcast("[SYS] " + data->username + " left the chat");
                        {
                            std::lock_guard<std::mutex> lock(g_ws_mutex);
                            g_ws_sessions.erase(data->username);
                        }
                        data->username.clear();
                    }
                    lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, nullptr, 0);
                    break;
                } else if (cmd == "/users") {
                    std::vector<ClientInfo> clients = g_chat_server->getClients();
                    std::string user_list = "[Users] Online: ";
                    for (size_t i = 0; i < clients.size(); ++i) {
                        user_list += clients[i].name;
                        if (i < clients.size() - 1) user_list += ", ";
                    }
                    unsigned char* buf = new unsigned char[LWS_PRE + user_list.size()];
                    memcpy(buf + LWS_PRE, user_list.c_str(), user_list.size());
                    lws_write(wsi, buf + LWS_PRE, user_list.size(), LWS_WRITE_TEXT);
                    delete[] buf;
                } else {
                    g_chat_server->broadcast(message);
                }
            } else {
                if (!data->username.empty()) {
                    std::string formatted_msg = "[" + data->username + "]: " + message;
                    std::cout << "[WebSocket] Broadcasting: " << formatted_msg << std::endl;
                    g_chat_server->broadcast(formatted_msg);
                } else {
                    std::string err = "[SYS] Please login first using /login <name>";
                    unsigned char* buf = new unsigned char[LWS_PRE + err.size()];
                    memcpy(buf + LWS_PRE, err.c_str(), err.size());
                    lws_write(wsi, buf + LWS_PRE, err.size(), LWS_WRITE_TEXT);
                    delete[] buf;
                }
            }
            break;
        }
        case LWS_CALLBACK_CLOSED: {
            std::cout << "[WebSocket] Client disconnected" << std::endl;
            if (data && !data->username.empty()) {
                g_chat_server->broadcast("[SYS] " + data->username + " left the chat");
                {
                    std::lock_guard<std::mutex> lock(g_ws_mutex);
                    g_ws_sessions.erase(data->username);
                }
            }
            break;
        }
        default:
            break;
    }
    return 0;
}

static struct lws_protocols protocols[] = {
    {
        "http-only",
        callback_http,
        0,
        0,
        0,
        nullptr,
        0
    },
    {
        "smallchat-protocol",
        callback_websocket,
        sizeof(SessionData),
        1024,
        0,
        nullptr,
        0
    },
    { nullptr, nullptr, 0, 0, 0, nullptr, 0 }
};

WebServer::WebServer(uint16_t port, ChatServer& chat_server) 
    : chat_server_(chat_server), port_(port), context_(nullptr) {}

WebServer::~WebServer() { stop(); }

bool WebServer::start() {
    g_chat_server = &chat_server_;
    g_chat_server->setWebSocketBroadcastCallback(websocket_broadcast);
    g_chat_server->setWebSocketSendToCallback(websocket_send_to);
    
    struct lws_context_creation_info info = {0};
    info.port = port_;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    
    context_ = lws_create_context(&info);
    if (!context_) {
        std::cerr << "[WebSocket] Failed to create context" << std::endl;
        return false;
    }
    
    std::cout << "[WebSocket] Server started on port " << port_ << std::endl;
    std::cout << "[HTTP] Web interface available at http://localhost:" << port_ << std::endl;
    std::cout << "[WebSocket] Connected to ChatServer, TCP and WebSocket clients can now communicate" << std::endl;
    
    service_thread_ = std::thread(&WebServer::run, this);
    return true;
}

void WebServer::stop() {
    if (context_) {
        lws_cancel_service(context_);
        if (service_thread_.joinable()) {
            service_thread_.join();
        }
        lws_context_destroy(context_);
        context_ = nullptr;
    }
}

void WebServer::run() {
    while (context_) {
        lws_service(context_, 100);
    }
}

} // namespace smallchat