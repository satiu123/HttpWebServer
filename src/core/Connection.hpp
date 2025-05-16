#pragma once
#include "../http/HttpServer.hpp"
#include "../http/FileService.hpp"
#include "../utils/PerformanceMonitor.hpp"
#include "Task.hpp"
#include "ConnectionManager.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include <fmt/base.h>
#include <fmt/format.h>

// 连接类，表示一个HTTP连接
class Connection {
public:
    int fd;
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
            LOG_INFO(fmt::format("连接已成功移除: {}", connectionFd));
        });
    }
    
    Task handleConnection(int epollFd) {
        try {
            // 记录连接
            PerformanceMonitor::getInstance().connectionEstablished();
            
            while (true) {  // 循环处理请求
                // 重置状态
                request.reset();
                response.reset();
                
                try {
                    co_await HttpServer::HttpRequestAwaiter(request, fd, epollFd);
                } catch (const std::exception& e) {
                    LOG_ERROR(fmt::format("请求解析错误: {}", e.what()));
                    break;  // 出错时退出循环
                }
                
                // 获取HTTP方法
                std::string method = request.method();
                std::string path = request.path();
                
                LOG_INFO(fmt::format("处理请求: {} {}", method, path));
                
                // 设置Connection头
                bool keepAlive = (request.getHeader("Connection") != "close");
                if (keepAlive) {
                    response.setHeader("Connection", "keep-alive");
                    response.setHeader("Keep-Alive", "timeout=5, max=100");
                } else {
                    response.setHeader("Connection", "close");
                }
                
                // 生成唯一请求ID用于追踪
                std::string requestId = fmt::format("{:x}", 
                    reinterpret_cast<uintptr_t>(this) ^ 
                    std::hash<std::string>{}(path) ^ 
                    std::chrono::high_resolution_clock::now().time_since_epoch().count());
                
                // 开始性能监控
                PerformanceMonitor::getInstance().startRequest(requestId, method, path);
                
                // 根据HTTP方法处理请求
                std::string statusCode = "200"; // 默认状态码
                
                try {
                    // 检查特殊请求路径
                    if (path == "/server-status") {
                        // 显示服务器状态
                        response.setStatus("200", "OK");
                        response.setContentType("text/plain; charset=UTF-8");
                        response.setBody(PerformanceMonitor::getInstance().getStatsSummary());
                    } else if (path == "/server-info") {
                        // 服务器信息
                        std::string info = "C++20 HTTP服务器\n";
                        info += "版本: 1.0.0\n";
                        info += fmt::format("配置文件: {}\n", Config::getInstance().getString("config_file", "server.conf"));
                        info += fmt::format("根目录: {}\n", Config::getInstance().getString("root_dir", "./www"));
                        info += fmt::format("监听地址: {}:{}\n", Config::getInstance().getString("host", "127.0.0.1"), Config::getInstance().getString("port", "8080"));
                        info += fmt::format("允许目录列表: {}\n", Config::getInstance().getBool("allow_directory_listing", false) ? "是" : "否");
                        
                        response.setStatus("200", "OK");
                        response.setContentType("text/plain; charset=UTF-8");
                        response.setBody(info);
                    } else if (method == "GET" || method == "HEAD") {
                        // 静态文件服务
                        auto [fileStatusCode, content] = FileService::getInstance().getFileContent(path);
                        statusCode = fileStatusCode; // 更新状态码
                        response.setStatus(statusCode, "");
                        
                        if (statusCode == "200") {
                            // 获取MIME类型并设置Content-Type
                            std::string mimeType = FileService::getInstance().getMimeType(path);
                            LOG_DEBUG(fmt::format("文件 {} 的MIME类型: {}", path, mimeType));
                            response.setContentType(mimeType);
                            
                            // 如果是HEAD请求，不返回响应体
                            if (method == "GET") {
                                response.setBody(content);
                            } else {
                                // 对于HEAD请求，设置Content-Length但不发送正文
                                response.setHeader("Content-Length", std::to_string(content.length()));
                            }
                        } else if (statusCode == "404") {
                            response.setContentType("text/html; charset=UTF-8");
                            response.setBody("<html><body><h1>404 Not Found</h1><p>您请求的资源在此服务器上未找到。</p></body></html>");
                        } else if (statusCode == "403") {
                            response.setContentType("text/html; charset=UTF-8");
                            response.setBody("<html><body><h1>403 Forbidden</h1><p>您没有权限访问此资源。</p></body></html>");
                        } else {
                            response.setContentType("text/html; charset=UTF-8");
                            response.setBody("<html><body><h1>500 Internal Server Error</h1><p>服务器遇到意外条件，无法完成请求。</p></body></html>");
                        }
                    } else if (method == "POST") {
                        // 简单的POST请求处理
                        response.setStatus("200", "OK");
                        statusCode = "200";
                        response.setContentType("text/plain; charset=UTF-8");
                        response.setBody("收到POST请求，请求体内容: " + request.body());
                    } else {
                        // 不支持的方法
                        response.setStatus("501", "Not Implemented");
                        statusCode = "501";
                        response.setContentType("text/html; charset=UTF-8");
                        response.setBody("<html><body><h1>501 未实现</h1><p>服务器不支持此请求方法。</p></body></html>");
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR(fmt::format("处理请求时发生异常: {}", e.what()));
                    response.setStatus("500", "Internal Server Error");
                    statusCode = "500";
                    response.setContentType("text/html; charset=UTF-8");
                    response.setBody("<html><body><h1>500 Internal Server Error</h1><p>服务器遇到意外错误。</p></body></html>");
                }
                
                // 发送响应
                try {
                    co_await HttpServer::HttpResponseAwaiter(response, fd, epollFd);
                    
                    // 更新性能监控
                    PerformanceMonitor::getInstance().endRequest(requestId, std::stoi(statusCode));
                } catch (const std::exception& e) {
                    LOG_ERROR(fmt::format("响应发送错误: {}", e.what()));
                    
                    // 更新性能监控（失败状态）
                    PerformanceMonitor::getInstance().endRequest(requestId, 500);
                    break;  // 出错时退出循环
                }
                
                // 如果不是keep-alive，退出循环
                if (!keepAlive) {
                    break;
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR(fmt::format("连接处理错误: {}", e.what()));
        }
        
        // 记录连接关闭
        PerformanceMonitor::getInstance().connectionClosed();
        
        // 处理完成或发生错误，关闭连接
        markForDeletion(epollFd);
        co_return;
    }
    
    explicit Connection(int fd) : fd(fd), task(nullptr) {
        LOG_DEBUG(fmt::format("新连接建立: {}", fd));
    }
    
    ~Connection() {
        // 不在这里关闭fd，由协程处理
    }
};