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
    std::vector<char>buffer;  // 使用std::vector作为缓冲区
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
            while (true) { // 处理多个请求的循环
                // 尝试无等待读取一次，可能请求已经完全到达
                ssize_t n = read(fd, buffer.data(), buffer.size());
                bool needWait = false;
                
                if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // 真正需要等待
                needWait = true;
                } else if (n > 0) {
                    // 处理读取的数据
                    request.parseRequest(std::string_view(buffer.data(), n));
                    if (!request.isComplete()) {
                        needWait = true;
                    }
                } else {
                    // 处理连接关闭或错误
                    markForDeletion(epollFd);
                    co_return;
                }
            
                // 只有真正需要等待时才挂起协程
                if (needWait) {
                    co_await ReadAwaiter(fd, epollFd);
                }
                
                // 请求处理完成，准备响应
                response.setStatus("200", "OK");
                response.setBody(request.body());
                response.setHeader("Content-Length", std::to_string(response.bodyLength()));

                // 写入响应
                try {
                    co_await HttpServer::HttpResponseAwaiter(response, fd, epollFd);
                } catch (const std::exception& e) {
                    // fmt::print("发送响应错误: {}\n", e.what());
                    markForDeletion(epollFd);
                    co_return ;
                }                
                // 响应发送完成后，重置连接状态，准备接收新请求
                request.reset();
                response.reset();
            }
        } catch (const std::exception& e) {
            fmt::print("连接处理错误: {}\n", e.what());
        }
        // 处理完所有请求后，关闭连接
        markForDeletion(epollFd);
        co_return;
    }
    explicit Connection(int fd) : fd(fd), task(nullptr) {
        buffer.resize(1024); // 初始化缓冲区大小
        // fmt::print("新连接: {}\n", fd);
    }
    
    ~Connection() {
        // 不在这里关闭fd，由协程处理
    }
};

// 声明外部变量，在main.cpp中定义
extern std::unordered_map<int, std::shared_ptr<Connection>> connections;