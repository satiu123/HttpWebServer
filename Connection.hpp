#pragma once
#include "AsyncIO.hpp"
#include "HttpServer.hpp"
#include "Task.hpp"
#include "ConnectionManager.hpp"
#include <array>
#include <fmt/base.h>
#include <unordered_map>
#include <memory>

// 连接类，表示一个HTTP连接
class Connection {
public:
    int fd;
    // std::vector<char> buffer;  // 缓冲区
    std::array<char, 4096> buffer;  // 使用std::array作为缓冲区
    HttpServer::HttpRequest request;
    HttpServer::HttpResponse response;
    Task task;  // 协程任务
    Task startHandleConnection(int epollFd) {
        task = handleConnection(epollFd); // 存储协程任务
        return std::move(task);
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
                // 等待可读事件
                co_await ReadAwaiter(fd, epollFd);
                
                bool connectionClosed = false;
                
                // 读取和处理请求
                while (true) {
                    ssize_t n = read(fd, buffer.data(), buffer.size());
                    
                    if (n == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // 没有更多数据可读
                            break;
                        } else {
                            // 错误发生
                            connectionClosed = true;
                            break;
                        }
                    } else if (n == 0) {
                        // 连接关闭
                        connectionClosed = true;
                        break;
                    }
                
                    // 处理接收到的数据
                    request.parseRequest(std::string_view(buffer.data(), n));
                    
                    if (request.isComplete()) {
                        break;
                    }
                }
                
                if (connectionClosed) {
                    // 连接已关闭，退出协程
                   markForDeletion(epollFd);
                    co_return;
                }
                
                // 请求处理完成，准备响应
                response.setStatus("200", "OK");
                response.setBody(request.body());
                response.setHeader("Content-Length", std::to_string(response.bodyLength()));

                // 等待可写事件
                // 尝试写入响应
                response.writeResponseCoro(fd, epollFd);
                // 如果需要等待可写，则等待
                if (response.isWritePending()) {
                    co_await WriteAwaiter(fd, epollFd);
                    // 继续写入，直到完成
                    while (response.isWritePending()) {
                        response.writeResponseCoro(fd, epollFd);
                        if (response.isWritePending()) {
                            co_await WriteAwaiter(fd, epollFd);
                        }
                    }
                }
                
                fmt::print("响应已发送: {}\n", fd);
                
                // 响应发送完成后，重置连接状态，准备接收新请求
                request = HttpServer::HttpRequest();
                response = HttpServer::HttpResponse();
            }
        } catch (const std::exception& e) {
            fmt::print("连接处理错误: {}\n", e.what());
            markForDeletion(epollFd);  // 处理异常情况，关闭连接
            // 异常情况，协程结束
        }
    co_return;
    }
    void close(int epollFd,std::unordered_map<int, std::shared_ptr<Connection>>& connections) {
        connections.erase(fd);  // 从连接映射中删除
        // 关闭连接
        ::close(fd);
        // 从epoll中删除fd
        epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr);
    }
    explicit Connection(int fd) : fd(fd), task(nullptr) {
        // buffer.resize(4096);  // 初始化缓冲区大小
    }
    
    ~Connection() {
        // 不在这里关闭fd，由协程处理
    }
};

// 声明外部变量，在main.cpp中定义
extern std::unordered_map<int, std::shared_ptr<Connection>> connections;