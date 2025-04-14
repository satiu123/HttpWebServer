#pragma once
#include "../network/AsyncIO.hpp"
#include "../http/HttpServer.hpp"
#include "Task.hpp"
#include "ConnectionManager.hpp"
#include <fmt/base.h>
#include <unordered_map>
#include <memory>
#include <vector>

// 连接类，表示一个HTTP连接
class Connection {
public:
    int fd;
    // std::vector<char>buffer;  // 使用std::vector作为缓冲区
    HttpServer::HttpRequest request;
    HttpServer::HttpResponse response;
    Task task;  // 协程任务
    void startHandleConnection(int epollFd) {
        task = handleConnection(epollFd); // 存储协程任务
    }
    // 标记连接为关闭，供协程内部使用
    void markForDeletion(int epollFd) {
        // 从 epoll 中移除
        epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr);
        // 关闭文件描述符
        ::close(fd);
        // 将自身安排为在当前协程完成后删除
        int connectionFd = fd;
        ConnectionManager::getInstance().postTask([connectionFd]() {
            ConnectionManager::getInstance().removeConnection(connectionFd);
            fmt::print("连接已成功移除: {}\n", connectionFd);
        });
    }
        Task handleConnection(int epollFd) {
            try {
                while (true) {  // 循环处理请求
                    // 重置状态
                    request.reset();
                    response.reset();
                    
                    try {
                        co_await HttpServer::HttpRequestAwaiter(request, fd, epollFd);
                    } catch (const std::exception& e) {
                        // fmt::print("请求解析错误: {}\n", e.what());
                        break;  // 出错时退出循环
                    }
                    
                    // 处理请求
                    response.setStatus("200", "OK");
                    
                    // 设置Connection头
                    bool keepAlive = (request.getHeader("Connection") != "close");
                    if (keepAlive) {
                        response.setHeader("Connection", "keep-alive");
                        response.setHeader("Keep-Alive", "timeout=5, max=100");
                    } else {
                        response.setHeader("Connection", "close");
                    }
                    
                    //设置响应内容
                    if (!request.body().empty()) {
                        response.setBody(request.body());
                        response.setHeader("Content-Type", "text/plain");
                    } else {
                        response.setBody("<html><body><h1>Hello, World!</h1></body></html>");
                        response.setHeader("Content-Type", "text/html");
                    }
                    // response.setBody("hello world");
                    // 发送响应
                    try {
                        co_await HttpServer::HttpResponseAwaiter(response, fd, epollFd);
                    } catch (const std::exception& e) {
                        fmt::print("响应发送错误: {}\n", e.what());
                        break;  // 出错时退出循环
                    }
                    
                    // 如果不是keep-alive，退出循环
                    if (!keepAlive) {
                        break;
                    }
                }
            } catch (const std::exception& e) {
                fmt::print("连接处理错误: {}\n", e.what());
            }
            
            // 处理完成或发生错误，关闭连接
            markForDeletion(epollFd);
            co_return;
        }
    explicit Connection(int fd) : fd(fd), task(nullptr) {
        
        // fmt::print("新连接: {}\n", fd);
    }
    
    ~Connection() {
        // 不在这里关闭fd，由协程处理
    }
};

// 声明外部变量，在main.cpp中定义
extern std::unordered_map<int, std::shared_ptr<Connection>> connections;