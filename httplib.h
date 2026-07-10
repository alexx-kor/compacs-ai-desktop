// Minimal HTTP/1.1 helper for COMPACS Desktop (Windows, winsock).
// Subset of cpp-httplib API used by main.cpp — local UI server + llama-server client.

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace httplib {

struct Request {
    std::string method;
    std::string path;
    std::string body;
};

struct DataSink {
    std::function<bool(const char *, std::size_t)> write;
};

struct Response {
    int status = 200;
    std::string body;
    std::map<std::string, std::string> headers;

    void set_content(std::string content, const std::string &content_type) {
        body = std::move(content);
        headers["Content-Type"] = content_type;
    }

    void set_header(const std::string &key, const std::string &value) {
        headers[key] = value;
    }

    void set_content_provider(
        const std::string &content_type,
        std::function<bool(std::size_t, DataSink &)> provider) {
        headers["Content-Type"] = content_type;
        stream_provider_ = std::move(provider);
    }

    std::function<bool(std::size_t, DataSink &)> stream_provider_;
};

struct Result {
    int status = 0;
    std::string body;
    explicit operator bool() const { return status != 0; }
};

namespace detail {

inline bool winsock_ready() {
    static bool ready = []() {
        WSADATA wsa{};
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    return ready;
}

inline bool send_all(SOCKET sock, const std::string &data) {
    const char *ptr = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        const int sent = send(sock, ptr, static_cast<int>(remaining), 0);
        if (sent <= 0) {
            return false;
        }
        ptr += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

inline std::string read_headers(SOCKET sock) {
    std::string buffer;
    char chunk[1024];
    while (buffer.find("\r\n\r\n") == std::string::npos) {
        const int read = recv(sock, chunk, sizeof(chunk), 0);
        if (read <= 0) {
            break;
        }
        buffer.append(chunk, read);
        if (buffer.size() > 65536) {
            break;
        }
    }
    return buffer;
}

inline std::size_t content_length_from_headers(const std::string &headers) {
    const auto pos = headers.find("Content-Length:");
    if (pos == std::string::npos) {
        return 0;
    }
    std::size_t i = pos + 15;
    while (i < headers.size() && (headers[i] == ' ' || headers[i] == '\t')) {
        ++i;
    }
    return static_cast<std::size_t>(std::stoul(headers.substr(i)));
}

inline Request parse_request(const std::string &raw_headers, const std::string &body) {
    Request req;
    req.body = body;
    std::istringstream line(raw_headers.substr(0, raw_headers.find("\r\n")));
    line >> req.method >> req.path;
    return req;
}

inline SOCKET connect_host(const std::string &host, int port, int timeout_sec) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo *result = nullptr;
    const std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result) != 0 || !result) {
        return INVALID_SOCKET;
    }
    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(result);
        return INVALID_SOCKET;
    }
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
    connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen));
    freeaddrinfo(result);

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);
    timeval tv{};
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    if (select(0, nullptr, &wfds, nullptr, &tv) <= 0) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    mode = 0;
    ioctlsocket(sock, FIONBIO, &mode);
    return sock;
}

inline std::optional<Result> http_request(
    const std::string &host,
    int port,
    const std::string &method,
    const std::string &path,
    const std::string &body,
    const std::string &content_type,
    int connect_timeout_sec,
    int read_timeout_sec,
    const std::function<bool(const char *, std::size_t)> &on_chunk) {
    if (!detail::winsock_ready()) {
        return std::nullopt;
    }
    SOCKET sock = detail::connect_host(host, port, connect_timeout_sec);
    if (sock == INVALID_SOCKET) {
        return std::nullopt;
    }

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n";
    req << "Host: " << host << "\r\n";
    req << "Connection: close\r\n";
    if (!body.empty()) {
        req << "Content-Type: " << content_type << "\r\n";
        req << "Content-Length: " << body.size() << "\r\n";
    }
    req << "\r\n";
    req << body;
    if (!detail::send_all(sock, req.str())) {
        closesocket(sock);
        return std::nullopt;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(read_timeout_sec);
    std::string raw;
    char chunk[4096];
    while (std::chrono::steady_clock::now() < deadline) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        timeval tv{0, 200000};
        if (select(0, &rfds, nullptr, nullptr, &tv) <= 0) {
            continue;
        }
        const int read = recv(sock, chunk, sizeof(chunk), 0);
        if (read <= 0) {
            break;
        }
        raw.append(chunk, read);
        if (raw.find("\r\n\r\n") != std::string::npos) {
            const auto header_end = raw.find("\r\n\r\n");
            const std::size_t expected = detail::content_length_from_headers(raw.substr(0, header_end));
            const std::size_t have_body = raw.size() - header_end - 4;
            if (expected == 0 || have_body >= expected) {
                break;
            }
        }
    }
    closesocket(sock);

    if (raw.empty()) {
        return std::nullopt;
    }

    const auto header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return std::nullopt;
    }
    const std::string headers = raw.substr(0, header_end);
    std::string response_body = raw.substr(header_end + 4);

    if (on_chunk && !response_body.empty()) {
        on_chunk(response_body.data(), response_body.size());
    }

    Result result;
    const auto status_pos = headers.find(' ');
    if (status_pos != std::string::npos) {
        try {
            result.status = std::stoi(headers.substr(status_pos + 1));
        } catch (...) {
            result.status = 0;
        }
    }
    result.body = std::move(response_body);
    return result;
}

}  // namespace detail

class Client {
public:
    Client(std::string host, int port) : host_(std::move(host)), port_(port) {}

    void set_connection_timeout(int sec, int) { connect_timeout_sec_ = sec; }
    void set_read_timeout(int sec, int) { read_timeout_sec_ = sec; }

    std::optional<Result> Get(const std::string &path) {
        return detail::http_request(
            host_, port_, "GET", path, "", "", connect_timeout_sec_, read_timeout_sec_, nullptr);
    }

    std::optional<Result> Post(const std::string &path, const std::string &body, const std::string &content_type) {
        return detail::http_request(
            host_, port_, "POST", path, body, content_type, connect_timeout_sec_, read_timeout_sec_, nullptr);
    }

    std::optional<Result> Post(
        const std::string &path,
        const std::string &body,
        const std::string &content_type,
        const std::function<bool(const char *, std::size_t)> &on_chunk) {
        return detail::http_request(
            host_, port_, "POST", path, body, content_type, connect_timeout_sec_, read_timeout_sec_, on_chunk);
    }

private:
    std::string host_;
    int port_;
    int connect_timeout_sec_ = 5;
    int read_timeout_sec_ = 300;
};

class Server {
public:
    enum class HandlerResponse { Handled, Unhandled };

    using Handler = std::function<void(const Request &, Response &)>;

    void set_pre_routing_handler(std::function<HandlerResponse(const Request &, Response &)> handler) {
        pre_handler_ = std::move(handler);
    }

    void Get(const std::string &pattern, Handler handler) { add_route("GET", pattern, std::move(handler)); }
    void Post(const std::string &pattern, Handler handler) { add_route("POST", pattern, std::move(handler)); }
    void Options(const std::string &pattern, Handler handler) { add_route("OPTIONS", pattern, std::move(handler)); }

    bool listen(const std::string &host, int port) {
        if (!detail::winsock_ready()) {
            return false;
        }
        stop_ = false;
        thread_ = std::thread([this, host, port]() { serve_loop(host, port); });
        for (int i = 0; i < 100; ++i) {
            if (running_) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return running_;
    }

    void stop() {
        stop_ = true;
        if (listen_socket_ != INVALID_SOCKET) {
            closesocket(listen_socket_);
            listen_socket_ = INVALID_SOCKET;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        running_ = false;
    }

private:
    struct Route {
        std::string method;
        std::regex pattern;
        Handler handler;
    };

    void add_route(const std::string &method, const std::string &pattern, Handler handler) {
        std::string regex = pattern;
        if (regex.size() >= 4 && regex.rfind(R"(/.*)", 0) == 0) {
            regex = ".*";
        }
        routes_.push_back(Route{method, std::regex("^" + regex + "$"), std::move(handler)});
    }

    void serve_loop(const std::string &host, int port) {
        SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server == INVALID_SOCKET) {
            return;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port));
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
        const BOOL yes = 1;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));
        if (::bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
            ::listen(server, 8) != 0) {
            closesocket(server);
            return;
        }
        listen_socket_ = server;
        running_ = true;

        while (!stop_) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(server, &readfds);
            timeval timeout{0, 200000};
            if (select(0, &readfds, nullptr, nullptr, &timeout) <= 0) {
                continue;
            }
            SOCKET client = accept(server, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                continue;
            }
            handle_client(client);
        }
        closesocket(server);
        listen_socket_ = INVALID_SOCKET;
        running_ = false;
    }

    void handle_client(SOCKET client) {
        const std::string headers = detail::read_headers(client);
        if (headers.empty()) {
            closesocket(client);
            return;
        }
        const std::size_t body_len = detail::content_length_from_headers(headers);
        std::string body(body_len, '\0');
        std::size_t got = 0;
        while (got < body_len) {
            const int read = recv(client, body.data() + got, static_cast<int>(body_len - got), 0);
            if (read <= 0) {
                break;
            }
            got += static_cast<std::size_t>(read);
        }
        body.resize(got);

        Request request = detail::parse_request(headers, body);
        Response response;
        response.status = 404;
        response.set_content(R"({"error":"not found"})", "application/json");

        if (pre_handler_) {
            pre_handler_(request, response);
        }

        bool matched = false;
        for (const auto &route : routes_) {
            if (route.method != request.method) {
                continue;
            }
            if (std::regex_match(request.path, route.pattern)) {
                matched = true;
                response.status = 200;
                response.body.clear();
                response.headers.erase("Content-Type");
                route.handler(request, response);
                break;
            }
        }
        if (!matched && request.method == "OPTIONS") {
            response.status = 204;
            response.body.clear();
            response.headers.clear();
        }

        std::ostringstream out;
        out << "HTTP/1.1 " << response.status;
        if (response.status == 204) {
            out << " No Content";
        } else {
            out << " OK";
        }
        out << "\r\n";
        for (const auto &[key, value] : response.headers) {
            out << key << ": " << value << "\r\n";
        }
        out << "Access-Control-Allow-Origin: *\r\n";
        out << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        out << "Access-Control-Allow-Headers: Content-Type\r\n";

        if (response.stream_provider_) {
            out << "Transfer-Encoding: chunked\r\n";
            out << "Cache-Control: no-cache\r\n";
            out << "Connection: close\r\n\r\n";
            detail::send_all(client, out.str());
            DataSink sink;
            sink.write = [&](const char *data, std::size_t length) {
                std::ostringstream chunk;
                chunk << std::hex << length << "\r\n";
                chunk.write(data, static_cast<std::streamsize>(length));
                chunk << "\r\n";
                return detail::send_all(client, chunk.str());
            };
            response.stream_provider_(0, sink);
            detail::send_all(client, "0\r\n\r\n");
        } else {
            out << "Content-Length: " << response.body.size() << "\r\n";
            out << "Connection: close\r\n\r\n";
            out << response.body;
            detail::send_all(client, out.str());
        }
        closesocket(client);
    }

    std::vector<Route> routes_;
    std::function<HandlerResponse(const Request &, Response &)> pre_handler_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    SOCKET listen_socket_ = INVALID_SOCKET;
};

}  // namespace httplib
