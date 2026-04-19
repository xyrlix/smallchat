#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <mutex>
#include <string>
#include <memory>
#include <thread>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include "web_server.hpp"
#include "server.h"
#include "common.h"

namespace smallchat {
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

static ChatServer* g_chat_server = nullptr;
static std::unordered_map<std::string, std::weak_ptr<class WebSocketSession>> g_ws_sessions;
static std::mutex g_ws_mutex;
static std::string g_html_content;

static std::string load_html_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[HTTP] Failed to open HTML file: " << path << std::endl;
        return "<html><body><h1>File not found</h1></body></html>";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static void websocket_broadcast(const std::string& message);
static void websocket_send_to(const std::string& client_name, const std::string& message);

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
public:
    explicit WebSocketSession(tcp::socket socket) : ws_(std::move(socket)) {}

    void run() {
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws_.set_option(websocket::stream_base::decorator(
            [](websocket::response_type& res) {
                res.set(http::field::server, std::string(BOOST_BEAST_VERSION_STRING) + " smallchat-websocket");
            }));
        ws_.async_accept(beast::bind_front_handler(&WebSocketSession::on_accept, shared_from_this()));
    }

private:
    void on_accept(beast::error_code ec) {
        if (ec) {
            std::cerr << "[WebSocket] Accept error: " << ec.message() << std::endl;
            return;
        }
        std::cout << "[WebSocket] Client connected" << std::endl;
        do_read();
    }

    void do_read() {
        ws_.async_read(buffer_, beast::bind_front_handler(&WebSocketSession::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if (ec == websocket::error::closed) {
            std::cout << "[WebSocket] Client disconnected" << std::endl;
            if (!username_.empty()) {
                g_chat_server->removeWebSocketClient(username_);
                {
                    std::lock_guard<std::mutex> lock(g_ws_mutex);
                    g_ws_sessions.erase(username_);
                }
                g_chat_server->broadcast("[SYS] " + username_ + " left the chat");
            }
            return;
        }

        if (ec) {
            std::cerr << "[WebSocket] Read error: " << ec.message() << std::endl;
            return;
        }

        std::string message(beast::buffers_to_string(buffer_.data()));
        buffer_.consume(buffer_.size());
        std::cout << "[WebSocket] Received: " << message << std::endl;

        process_message(message);
        do_read();
    }

    void process_message(const std::string& message) {
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
                    send_message("[SYS] Error: Name already taken");
                    return;
                }

                username_ = username;
                {
                    std::lock_guard<std::mutex> lock(g_ws_mutex);
                    g_ws_sessions[username_] = shared_from_this();
                }
                g_chat_server->addWebSocketClient(username_, this);

                send_message("[SYS] Welcome, " + username_ + "! You are logged in.");
                g_chat_server->broadcast("[SYS] " + username_ + " joined the chat via WebSocket");
            } else if (cmd == "/quit") {
                if (!username_.empty()) {
                    g_chat_server->removeWebSocketClient(username_);
                    {
                        std::lock_guard<std::mutex> lock(g_ws_mutex);
                        g_ws_sessions.erase(username_);
                    }
                    g_chat_server->broadcast("[SYS] " + username_ + " left the chat");
                    username_.clear();
                }
                ws_.async_close(websocket::close_code::normal,
                    beast::bind_front_handler(&WebSocketSession::on_close, shared_from_this()));
            } else if (cmd == "/users") {
                std::vector<ClientInfo> clients = g_chat_server->getClients();
                std::string user_list = "[Users] Online: ";
                for (size_t i = 0; i < clients.size(); ++i) {
                    user_list += clients[i].name;
                    if (i < clients.size() - 1) user_list += ", ";
                }
                send_message(user_list);
            } else {
                g_chat_server->broadcast(message);
            }
        } else {
            if (!username_.empty()) {
                std::string formatted_msg = "[" + username_ + "]: " + message;
                std::cout << "[WebSocket] Broadcasting: " << formatted_msg << std::endl;
                g_chat_server->broadcast(formatted_msg);
            } else {
                send_message("[SYS] Please login first using /login <name>");
            }
        }
    }

    void send_message(const std::string& message) {
        std::lock_guard<std::mutex> lock(write_mutex_);
        bool write_in_progress = !write_queue_.empty();
        write_queue_.push(message);
        if (!write_in_progress) {
            do_write();
        }
    }

    void do_write() {
        if (write_queue_.empty()) return;
        ws_.async_write(net::buffer(write_queue_.front()),
            beast::bind_front_handler(&WebSocketSession::on_write, shared_from_this()));
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        std::lock_guard<std::mutex> lock(write_mutex_);
        write_queue_.pop();

        if (ec) {
            std::cerr << "[WebSocket] Write error: " << ec.message() << std::endl;
            return;
        }

        do_write();
    }

    void on_close(beast::error_code ec) {
        if (ec) {
            std::cerr << "[WebSocket] Close error: " << ec.message() << std::endl;
        }
    }

    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    std::string username_;
    std::queue<std::string> write_queue_;
    std::mutex write_mutex_;

    friend void websocket_broadcast(const std::string& message);
    friend void websocket_send_to(const std::string& client_name, const std::string& message);
};

static void websocket_broadcast(const std::string& message) {
    std::cout << "[WebSocket] Broadcast to " << g_ws_sessions.size() << " clients: " << message << std::endl;
    std::lock_guard<std::mutex> lock(g_ws_mutex);
    for (auto it = g_ws_sessions.begin(); it != g_ws_sessions.end();) {
        if (auto session = it->second.lock()) {
            session->send_message(message);
            ++it;
        } else {
            it = g_ws_sessions.erase(it);
        }
    }
}

static void websocket_send_to(const std::string& client_name, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_ws_mutex);
    auto it = g_ws_sessions.find(client_name);
    if (it != g_ws_sessions.end()) {
        if (auto session = it->second.lock()) {
            session->send_message(message);
        }
    }
}

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    explicit HttpSession(tcp::socket socket) : socket_(std::move(socket)) {}

    void run() { do_read(); }

private:
    void do_read() {
        request_ = {};
        http::async_read(socket_, buffer_, request_,
            beast::bind_front_handler(&HttpSession::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if (ec == http::error::end_of_stream) {
            do_close();
            return;
        }

        if (ec) {
            std::cerr << "[HTTP] Read error: " << ec.message() << std::endl;
            return;
        }

        handle_request();
    }

    void handle_request() {
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "text/html");
        response_.set(http::field::content_length, g_html_content.size());
        response_.set(http::field::connection, "close");
        response_.body() = g_html_content;

        http::async_write(socket_, response_,
            beast::bind_front_handler(&HttpSession::on_write, shared_from_this()));
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        if (ec) {
            std::cerr << "[HTTP] Write error: " << ec.message() << std::endl;
            return;
        }
        do_close();
    }

    void do_close() {
        beast::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_send, ec);
    }

    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> request_;
    http::response<http::string_body> response_;
};

class Listener : public std::enable_shared_from_this<Listener> {
public:
    Listener(net::io_context& ioc, tcp::endpoint endpoint)
        : acceptor_(ioc), socket_(ioc) {
        beast::error_code ec;
        acceptor_.open(endpoint.protocol(), ec);
        if (ec) {
            std::cerr << "[WebSocket] Open error: " << ec.message() << std::endl;
            return;
        }

        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if (ec) {
            std::cerr << "[WebSocket] Set option error: " << ec.message() << std::endl;
            return;
        }

        acceptor_.bind(endpoint, ec);
        if (ec) {
            std::cerr << "[WebSocket] Bind error: " << ec.message() << std::endl;
            return;
        }

        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if (ec) {
            std::cerr << "[WebSocket] Listen error: " << ec.message() << std::endl;
            return;
        }
    }

    void run() { do_accept(); }

private:
    void do_accept() {
        acceptor_.async_accept(socket_, beast::bind_front_handler(&Listener::on_accept, shared_from_this()));
    }

    void on_accept(beast::error_code ec) {
        if (ec) {
            std::cerr << "[WebSocket] Accept error: " << ec.message() << std::endl;
        } else {
            auto remote_addr = socket_.remote_endpoint().address().to_string();
            auto remote_port = socket_.remote_endpoint().port();
            
            http::request<http::string_body> req;
            beast::flat_buffer buffer;
            beast::error_code req_ec;
            
            http::read(socket_, buffer, req, req_ec);
            
            if (!req_ec && req.target() == "/ws") {
                std::make_shared<WebSocketSession>(std::move(socket_))->run();
            } else {
                std::make_shared<HttpSession>(std::move(socket_))->run();
            }
        }
        do_accept();
    }

    tcp::acceptor acceptor_;
    tcp::socket socket_;
};

WebServer::WebServer(uint16_t port, ChatServer& chat_server) 
    : chat_server_(chat_server), port_(port), ioc_(1), running_(false) {}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start() {
    g_chat_server = &chat_server_;
    g_chat_server->setWebSocketBroadcastCallback(websocket_broadcast);
    g_chat_server->setWebSocketSendToCallback(websocket_send_to);
    
    g_html_content = load_html_file("web/index.html");

    try {
        listener_ = std::make_shared<Listener>(ioc_, tcp::endpoint(tcp::v4(), port_));
        listener_->run();
        running_ = true;
        
        std::cout << "[WebSocket] Server started on port " << port_ << std::endl;
        std::cout << "[HTTP] Web interface available at http://localhost:" << port_ << std::endl;
        std::cout << "[WebSocket] Connected to ChatServer, TCP and WebSocket clients can now communicate" << std::endl;

        service_thread_ = std::thread([this]() {
            ioc_.run();
        });
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[WebSocket] Failed to start: " << e.what() << std::endl;
        return false;
    }
}

void WebServer::stop() {
    running_ = false;
    ioc_.stop();
    if (service_thread_.joinable()) {
        service_thread_.join();
    }
}

void WebServer::run() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace smallchat