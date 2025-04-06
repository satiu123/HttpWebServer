#pragma once
#include "HttpServer.hpp"
#include <array>
#include <unordered_map>
#include <memory>

// 连接类，表示一个HTTP连接
class Connection {
public:
    int fd;
    std::array<char, 8192> buffer;  // 8KB缓冲区
    HttpServer::HttpRequest request;
    HttpServer::HttpResponse response;
    
    explicit Connection(int fd) : fd(fd) {
        buffer.fill(0);
    }
    
    ~Connection() {
        // 不在这里关闭fd，由协程处理
    }
};

// 声明外部变量，在main.cpp中定义
extern std::unordered_map<int, std::shared_ptr<Connection>> connections;