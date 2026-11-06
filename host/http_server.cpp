#include "http_server.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace fc {
namespace host {
namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalid = INVALID_SOCKET;

void closeSocket(SocketHandle socket) {
    if (socket != kInvalid) {
        closesocket(socket);
    }
}

int lastSocketError() { return WSAGetLastError(); }
#else
#error "fc_host currently targets Windows (winsock2)."
#endif

std::string statusText(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

bool parseRequestLine(const std::string& line, HttpRequest& request) {
    const std::size_t first = line.find(' ');
    if (first == std::string::npos) {
        return false;
    }
    const std::size_t second = line.find(' ', first + 1);
    if (second == std::string::npos) {
        return false;
    }
    request.method = line.substr(0, first);
    std::string target = line.substr(first + 1, second - first - 1);
    const std::size_t question = target.find('?');
    if (question == std::string::npos) {
        request.path = target;
    } else {
        request.path = target.substr(0, question);
        request.query = target.substr(question + 1);
    }
    return true;
}

std::size_t contentLength(const std::string& headers) {
    std::string lower = headers;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const std::string key = "content-length:";
    const std::size_t at = lower.find(key);
    if (at == std::string::npos) {
        return 0;
    }
    std::size_t cursor = at + key.size();
    while (cursor < lower.size() && (lower[cursor] == ' ' || lower[cursor] == '\t')) {
        ++cursor;
    }
    std::size_t value = 0;
    while (cursor < lower.size() && lower[cursor] >= '0' && lower[cursor] <= '9') {
        value = value * 10 + static_cast<std::size_t>(lower[cursor] - '0');
        ++cursor;
    }
    return value;
}

}  // namespace

HttpServer::HttpServer() = default;

HttpServer::~HttpServer() { stop(); }

bool HttpServer::start(const std::string& bindAddress, int port, std::string& error) {
#if defined(_WIN32)
    if (!winsockReady_) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            error = "WSAStartup failed";
            return false;
        }
        winsockReady_ = true;
    }
    SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) {
        error = "socket() failed";
        return false;
    }
    BOOL reuse = TRUE;
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    if (bindAddress.empty() || bindAddress == "127.0.0.1") {
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else if (inet_pton(AF_INET, bindAddress.c_str(), &address.sin_addr) != 1) {
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    if (::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error = "bind failed on port " + std::to_string(port) + " (error " + std::to_string(lastSocketError()) +
                ")";
        closeSocket(socket);
        return false;
    }
    if (::listen(socket, 16) != 0) {
        error = "listen failed";
        closeSocket(socket);
        return false;
    }
    u_long nonBlocking = 1;
    ioctlsocket(socket, FIONBIO, &nonBlocking);
    listenSocket_ = static_cast<std::uintptr_t>(socket);
    port_ = port;
    return true;
#else
    (void)bindAddress;
    (void)port;
    error = "unsupported platform";
    return false;
#endif
}

void HttpServer::stop() {
    for (std::size_t i = 0; i < connections_.size(); ++i) {
        closeConnection(i);
    }
    connections_.clear();
    if (listenSocket_ != 0) {
        closeSocket(static_cast<SocketHandle>(listenSocket_));
        listenSocket_ = 0;
    }
#if defined(_WIN32)
    if (winsockReady_) {
        WSACleanup();
        winsockReady_ = false;
    }
#endif
}

void HttpServer::setHandler(Handler handler) { handler_ = std::move(handler); }

void HttpServer::closeConnection(std::size_t index) {
    if (index >= connections_.size()) {
        return;
    }
    closeSocket(static_cast<SocketHandle>(connections_[index].socket));
    connections_.erase(connections_.begin() + static_cast<std::ptrdiff_t>(index));
}

void HttpServer::process(Connection& connection) {
    if (connection.responded) {
        return;
    }
    const std::size_t headerEnd = connection.input.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return;
    }
    const std::string headers = connection.input.substr(0, headerEnd);
    const std::size_t bodyStart = headerEnd + 4;
    const std::size_t expected = contentLength(headers);
    if (connection.input.size() < bodyStart + expected) {
        return;
    }

    HttpRequest request;
    const std::size_t lineEnd = headers.find("\r\n");
    const std::string requestLine = headers.substr(0, lineEnd == std::string::npos ? headers.size() : lineEnd);
    if (!parseRequestLine(requestLine, request)) {
        connection.output = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        connection.responded = true;
        connection.closing = true;
        return;
    }
    request.body = connection.input.substr(bodyStart, expected);

    HttpResponse response;
    if (request.method == "OPTIONS") {
        response.status = 204;
        response.body.clear();
    } else if (handler_) {
        response = handler_(request);
    } else {
        response.status = 404;
        response.body = "{\"ok\":false}";
    }

    std::string head = "HTTP/1.1 " + std::to_string(response.status) + " " + statusText(response.status) + "\r\n";
    head += "Content-Type: " + response.contentType + "\r\n";
    head += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
    head += "Connection: close\r\n";
    head += "Cache-Control: no-store\r\n";
    head += "Access-Control-Allow-Origin: *\r\n";
    head += "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n";
    head += "Access-Control-Allow-Headers: Content-Type\r\n";
    head += "\r\n";
    connection.output = head + response.body;
    connection.responded = true;
    connection.closing = true;
}

void HttpServer::poll(int timeoutMs) {
    if (listenSocket_ == 0) {
        return;
    }
    fd_set readSet;
    fd_set writeSet;
    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);

    const SocketHandle listener = static_cast<SocketHandle>(listenSocket_);
    FD_SET(listener, &readSet);
    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;

    int maxFd = static_cast<int>(listener);
    for (const auto& connection : connections_) {
        const SocketHandle socket = static_cast<SocketHandle>(connection.socket);
        if (!connection.responded) {
            FD_SET(socket, &readSet);
        }
        if (!connection.output.empty()) {
            FD_SET(socket, &writeSet);
        }
        maxFd = std::max(maxFd, static_cast<int>(socket));
    }

    const int ready = select(maxFd + 1, &readSet, &writeSet, nullptr, &timeout);
    if (ready <= 0) {
        return;
    }

    if (FD_ISSET(listener, &readSet)) {
        for (int i = 0; i < 8; ++i) {
            sockaddr_in clientAddress{};
            int length = static_cast<int>(sizeof(clientAddress));
            const SocketHandle client =
                accept(listener, reinterpret_cast<sockaddr*>(&clientAddress), &length);
            if (client == INVALID_SOCKET) {
                break;
            }
            u_long nonBlocking = 1;
            ioctlsocket(client, FIONBIO, &nonBlocking);
            Connection connection;
            connection.socket = static_cast<std::uintptr_t>(client);
            connections_.push_back(connection);
        }
    }

    for (std::size_t index = 0; index < connections_.size();) {
        Connection& connection = connections_[index];
        const SocketHandle socket = static_cast<SocketHandle>(connection.socket);
        bool drop = false;

        if (!connection.responded && FD_ISSET(socket, &readSet)) {
            char buffer[4096];
            const int received = recv(socket, buffer, sizeof(buffer), 0);
            if (received > 0) {
                connection.input.append(buffer, static_cast<std::size_t>(received));
                if (connection.input.size() > 1024 * 1024) {
                    drop = true;
                } else {
                    process(connection);
                }
            } else if (received == 0) {
                drop = true;
            } else {
                const int error = lastSocketError();
                if (error != WSAEWOULDBLOCK) {
                    drop = true;
                }
            }
        }

        if (!drop && !connection.output.empty()) {
            const int sent = send(socket, connection.output.data(),
                                  static_cast<int>(connection.output.size()), 0);
            if (sent > 0) {
                connection.output.erase(0, static_cast<std::size_t>(sent));
            } else if (sent < 0) {
                const int error = lastSocketError();
                if (error != WSAEWOULDBLOCK) {
                    drop = true;
                }
            }
        }

        if (!drop && connection.closing && connection.output.empty()) {
            drop = true;
        }

        if (drop) {
            closeConnection(index);
        } else {
            ++index;
        }
    }
}

}  // namespace host
}  // namespace fc
