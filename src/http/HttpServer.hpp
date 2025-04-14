#pragma once
#include "HttpParser.hpp"
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>
#include <unordered_map>
#include <netinet/tcp.h>
#include <cerrno>
#include <coroutine>
#include <sys/epoll.h>
#include <vector>

class HttpServer {
public:
    HttpServer() = default;
    ~HttpServer() = default;
    class HttpRequest {
    private:
        RequestParser parser;
        std::unordered_map<std::string, std::string> queryParams;
        void parseQueryParams() {
            std::string url = parser.url();
            size_t pos = url.find('?');
            if (pos != std::string::npos) {
                std::string query = url.substr(pos + 1);
                std::string path = url.substr(0, pos);
                
                size_t start = 0;
                size_t end;
                while ((end = query.find('&', start)) != std::string::npos) {
                    parseQueryParam(query.substr(start, end - start));
                    start = end + 1;
                }
                parseQueryParam(query.substr(start));
            }
        }

        void parseQueryParam(const std::string& param) {
            size_t pos = param.find('=');
            if (pos != std::string::npos) {
                std::string key = param.substr(0, pos);
                std::string value = param.substr(pos + 1);
                queryParams[key] = value;
            }
        }

    public:
        HttpRequest() = default;
        ~HttpRequest() = default;
        bool readComplete = false;
        void reset() {
            parser.reset();
            queryParams.clear();
            readComplete = false;
        }
        // 新增读取方法，返回是否读取完成
        bool read(int fd, std::vector<char>& buffer) {
            if (readComplete) {
                return true;
            }
            
            ssize_t bytesRead = ::read(fd, buffer.data(), buffer.size());
            
            if (bytesRead > 0) {
                // 解析请求
                parseRequest(std::string_view(buffer.data(), bytesRead));
                
                // 如果请求解析完成
                if (isComplete()) {
                    readComplete = true;
                    return true;
                }
                
                // 需要继续读取
                return false;
            } else if (bytesRead == 0) {
                // 连接关闭
                throw std::runtime_error("Connection closed by peer");
            } else if (bytesRead == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 暂时没有数据可读
                    return false;
                } else {
                    // 其他错误
                    throw std::runtime_error("read error: " + std::string(strerror(errno)));
                }
            }
            return false;
        }
        void parseRequest(std::string_view request) {
            parser.parse(request);
            if (parser.isComplete()) {
                parseQueryParams();
            }
        }
        
        bool isComplete() const {
            return parser.isComplete();
        }
        
        std::string method() const {
            return parser.method();
        }
        
        std::string url() const {
            return parser.url();
        }
        
        std::string path() const {
            std::string url = parser.url();
            size_t pos = url.find('?');
            return (pos != std::string::npos) ? url.substr(0, pos) : url;
        }
        
        std::string version() const {
            return parser.version();
        }
        
        std::string getHeader(const std::string& key) const {
            auto it = parser.headerMap.find(key);
            return (it != parser.headerMap.end()) ? it->second : "";
        }
        
        const std::unordered_map<std::string, std::string>& headers() const {
            return parser.headerMap;
        }
        
        std::string&& body()  {
            return std::move(parser.body);
        }
        
        std::string getParam(const std::string& key) const {
            auto it = queryParams.find(key);
            return (it != queryParams.end()) ? it->second : "";
        }
        
        const std::unordered_map<std::string, std::string>& params() const {
            return queryParams;
        }
    };
    
    class HttpResponse {
    public:
        std::string version;
        std::string statusCode;
        std::string statusMessage;
        std::unordered_map<std::string, std::string> headers;
        std::string responseBody;
        
        // 添加写入状态追踪
        std::string responseText;
        size_t bytesSent = 0;
        bool writePending = false;
        
    public:
        HttpResponse() : version("HTTP/1.1"), statusCode("200"), statusMessage("OK") {
            headers["Server"] = "C++ HttpServer";
            headers["Content-Type"] = "text/html; charset=UTF-8";
            // headers["Connection"] = "keep-alive";
        }
        void reset(){
            version = "HTTP/1.1";
            statusCode = "200";
            statusMessage = "OK";
            headers.clear();
            responseBody.clear();
            responseText.clear();
            bytesSent = 0;
            writePending = false;
        }
        bool isWritePending() const {
            return writePending;
        }
        // 初始化响应文本
        void init(){
            responseText = toString();
            bytesSent = 0;
            writePending = true;
        }
        // 重置写入状态
        void resetWriteState() {
            bytesSent = 0;
            writePending = false;
        }
        
        // 查询写入是否完成
        bool isWriteComplete() const {
            return !writePending || bytesSent >= responseText.size();
        }
        
        void setStatus(const std::string_view code, const std::string_view message) {
            statusCode = code;
            statusMessage = message;
        }
        
        void setHeader(const std::string& key, const std::string& value) {
            headers[key] = value;
        }
        
        void setBody(const std::string&& body) {
            responseBody = std::move(body);
            headers["Content-Length"] = std::to_string(responseBody.length());
        }
        
        void setContentType(const std::string& contentType) {
            headers["Content-Type"] = contentType;
        }
        int bodyLength() const {
            return responseBody.length();
        }
        // 优化toString方法，减少内存分配
        std::string toString() const {
            // 预估响应大小
            size_t estimatedSize = 
                version.size() + statusCode.size() + statusMessage.size() + 
                responseBody.size() + headers.size() * 30 + 20;
            
            std::string result;
            result.reserve(estimatedSize);
            
            // 直接拼接字符串，避免stringstream开销
            result.append(version).append(" ").append(statusCode).append(" ")
                  .append(statusMessage).append("\r\n");
            
            // 添加headers
            for (const auto& [key, value] : headers) {
                result.append(key).append(": ").append(value).append("\r\n");
            }
            
            // 空行和响应体
            result.append("\r\n").append(responseBody);
            
            return result;
        }
    };
    class HttpRequestAwaiter {
    private:
        HttpRequest& request;
        int clientFd;
        int epollFd;
        std::vector<char>buffer;
    public:
        HttpRequestAwaiter(HttpRequest& req, int clientFd, int epollFd)
            : request(req), clientFd(clientFd), epollFd(epollFd){
                buffer.resize(1024); // 初始化缓冲区大小
            }
        
        bool await_ready() {
            return request.read(clientFd, buffer);
        }
        
        void await_suspend(std::coroutine_handle<> handle) {
            // 注册epoll事件
            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
            ev.data.ptr = handle.address();
            
            if (epoll_ctl(epollFd, EPOLL_CTL_MOD, clientFd, &ev) == -1) {
                if (errno == ENOENT) {
                    // 如果事件不存在，则添加
                    epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &ev);
                } else {
                    throw std::runtime_error("epoll_ctl error: " + std::string(strerror(errno)));
                }
            }
        }
        
        void await_resume() {
            // 继续处理请求
            while(!request.isComplete()) {
                try {
                    if (request.read(clientFd, buffer)) {
                        // 如果读取完成，退出循环
                        break;
                    }
                } catch (const std::exception& e) {
                    // 处理异常
                    request.readComplete = false;
                    throw; // 重新抛出异常
                }
            }         
        }
    };
    
    class HttpResponseAwaiter {
    private:
        HttpServer::HttpResponse& response;
        int clientFd;
        int epollFd;
        
    public:
        HttpResponseAwaiter(HttpServer::HttpResponse& resp, int clientFd, int epollFd)
            : response(resp), clientFd(clientFd), epollFd(epollFd) {}
        
        bool await_ready() { 
            // 准备响应文本(如果尚未准备)
            if (!response.isWritePending()) {
                response.init();
            }
            
            // 尝试无等待写入
            bool writeComplete = tryWrite();
            
            // 如果写入完成，则不需要挂起
            return writeComplete;
        }
        
        void await_suspend(std::coroutine_handle<> handle) {
            // 只有在需要等待时才注册epoll事件
            struct epoll_event ev;
            ev.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;
            ev.data.ptr = handle.address();
            
            if (epoll_ctl(epollFd, EPOLL_CTL_MOD, clientFd, &ev) == -1) {
                if (errno == ENOENT) {
                    // 如果事件不存在，则添加
                    epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &ev);
                } else {
                    throw std::runtime_error("epoll_ctl error: " + std::string(strerror(errno)));
                }
            }
        }
        
        void await_resume() {
            //epoll事件触发后，继续尝试写入
            while(!response.isWriteComplete()) {
                try {
                    if (tryWrite()) {
                        // 如果写入完成，退出循环
                        break;
                    }
                } catch (const std::exception& e) {
                    // 处理异常
                    response.writePending = false;
                    throw; // 重新抛出异常
                }
            }
        }
        
    private:
        // 尝试写入响应，返回是否完成
        bool tryWrite() {
            constexpr size_t MAX_WRITE_SIZE = 65536; // 64KB
            size_t remaining = response.responseText.size() - response.bytesSent;
            size_t toWrite = std::min(remaining, MAX_WRITE_SIZE);
            
            // 使用send代替write，添加MSG_NOSIGNAL避免SIGPIPE
            ssize_t sent = send(clientFd, 
                                response.responseText.data() + response.bytesSent, 
                                toWrite, 
                                MSG_NOSIGNAL);
            
            if (sent > 0) {
                response.bytesSent += sent;
                // 检查是否全部发送完毕
                if (response.bytesSent >= response.responseText.size()) {
                    response.writePending = false;
                    return true;
                }
            } else if (sent == 0) {
                // 连接已关闭
                throw std::runtime_error("Connection closed");
            } else if (sent == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 写缓冲区已满，需要等待
                    return false;
                } else if (errno == EPIPE || errno == ECONNRESET) {
                    // 连接已被客户端关闭
                    throw std::runtime_error("连接被客户端关闭");
                } else {
                    // 其他错误
                    throw std::runtime_error("write error: " + std::string(strerror(errno)));
                }
            }
        return true; // 写入完成
        }
    };
};
