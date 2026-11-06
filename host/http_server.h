// 最小化的单线程 HTTP/1.1 服务器，绑定到 127.0.0.1（无外部依赖）。
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fc {
namespace host {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
};

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    HttpServer();
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    bool start(const std::string& bindAddress, int port, std::string& error);
    void stop();
    void setHandler(Handler handler);

    // 处理待处理的 socket。阻塞时间不会超过 timeoutMs。
    void poll(int timeoutMs);

    bool listening() const { return listenSocket_ != 0; }
    int boundPort() const { return port_; }

private:
    struct Connection {
        std::uintptr_t socket = 0;
        std::string input;
        std::string output;
        bool closing = false;
        bool responded = false;
    };

    void closeConnection(std::size_t index);
    void process(Connection& connection);

    std::uintptr_t listenSocket_ = 0;
    int port_ = 0;
    bool winsockReady_ = false;
    std::vector<Connection> connections_;
    Handler handler_;
};

}  // namespace host
}  // namespace fc
