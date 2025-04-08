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
                bool requestComplete = false;
                
                // 循环直到请求完整读取
                while (!requestComplete) {
                    // 读取数据
                    ssize_t n = read(fd, buffer.data(), buffer.size());
                    
                    if (n == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // 需要等待更多数据
                            co_await ReadAwaiter(fd, epollFd);
                            continue; // 等待后继续读取
                        } else {
                            // 其他读取错误
                            markForDeletion(epollFd);
                            co_return;
                        }
                    } else if (n == 0) {
                        // 连接已关闭
                        markForDeletion(epollFd);
                        co_return;
                    } else {
                        // 解析收到的数据
                        request.parseRequest(std::string_view(buffer.data(), n));
                        
                        // 检查请求是否完整
                        if (request.isComplete()) {
                            requestComplete = true;
                            /*
                            fmt::print("请求完整接收: method={}, body_length={}\n", 
                                    request.method(), request.body().length());
                                    */
                        } else {
                            // 请求不完整，等待更多数据
                            co_await ReadAwaiter(fd, epollFd);
                        }
                    }
                }
                
                // 请求处理完成，准备响应
                response.setStatus("200", "OK");
                
                // 根据请求体设置响应
                if (!request.body().empty()) {
                    response.setBody(request.body());
                    response.setHeader("Content-Type", "text/plain");
                } else {
                    response.setBody("<html><body><h1>Hello, World!</h1></body></html>");
                    response.setHeader("Content-Type", "text/html");
                }                
                // 写入响应
                try {
                    co_await HttpServer::HttpResponseAwaiter(response, fd, epollFd);
                } catch (const std::exception& e) {
                    markForDeletion(epollFd);
                    co_return;
                }
                request.reset();
                response.reset();
            }
        } catch (const std::exception& e) {
            fmt::print("连接处理错误: {}\n", e.what());
        }
        
        markForDeletion(epollFd);
        co_return;
    }
    explicit Connection(int fd) : fd(fd), task(nullptr) {
        // buffer.resize(1024); // 初始化缓冲区大小
        buffer.reserve(8192); // 初始化缓冲区大小
        buffer.resize(4096);
        // fmt::print("新连接: {}\n", fd);
    }
    
    ~Connection() {
        // 不在这里关闭fd，由协程处理
    }
};

// 声明外部变量，在main.cpp中定义
extern std::unordered_map<int, std::shared_ptr<Connection>> connections;