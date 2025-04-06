#pragma once
#include "HttpParser.hpp"
#include <cstring>
#include <string>
#include <string_view>
#include <unistd.h>
#include <unordered_map>
#include <sstream>

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
        
        std::string body() const {
            return parser.body;
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
    private:
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
            headers["Connection"] = "keep-alive";
        }
        // 非阻塞写入方法
        bool writeResponse(int clientFd) {
            // 首次调用时，生成响应文本
            if (!writePending) {
                responseText = toString();
                bytesSent = 0;
                writePending = true;
            }
            
            // 继续发送剩余数据
            while (bytesSent < responseText.size()) {
                ssize_t sent = write(clientFd, responseText.data() + bytesSent, 
                                    responseText.size() - bytesSent);
                                    
                if (sent > 0) {
                    bytesSent += sent;
                } else if (sent == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // 写缓冲区已满，需要等待下一次EPOLLOUT事件
                        return false;
                    } else {
                        // 发生错误
                        throw std::runtime_error("write error: " + std::string(strerror(errno)));
                    }
                }
            }
            
            // 全部数据已发送
            writePending = false;
            return true;
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
        
        void setStatus(const std::string& code, const std::string& message) {
            statusCode = code;
            statusMessage = message;
        }
        
        void setHeader(const std::string& key, const std::string& value) {
            headers[key] = value;
        }
        
        void setBody(const std::string& body) {
            responseBody = body;
            headers["Content-Length"] = std::to_string(responseBody.length());
        }
        
        void setContentType(const std::string& contentType) {
            headers["Content-Type"] = contentType;
        }
        int bodyLength() const {
            return responseBody.length();
        }
        std::string toString() const {
            std::ostringstream response;
            
            // 状态行
            response << version << " " << statusCode << " " << statusMessage << "\r\n";
            
            // 响应头
            for (const auto& [key, value] : headers) {
                response << key << ": " << value << "\r\n";
            }
            
            // 空行分隔
            response << "\r\n";
            
            // 响应体
            response << responseBody;
            
            return response.str();
        }
        
        // 常见状态码的便捷方法
        void ok(const std::string& body = "", const std::string& contentType = "text/html; charset=UTF-8") {
            setStatus("200", "OK");
            setContentType(contentType);
            setBody(body);
        }
        
        void notFound(const std::string& body = "404 Not Found") {
            setStatus("404", "Not Found");
            setBody(body);
        }
        
        void serverError(const std::string& body = "500 Internal Server Error") {
            setStatus("500", "Internal Server Error");
            setBody(body);
        }
        
        void badRequest(const std::string& body = "400 Bad Request") {
            setStatus("400", "Bad Request");
            setBody(body);
        }
        
        void redirect(const std::string& url, bool permanent = false) {
            if (permanent) {
                setStatus("301", "Moved Permanently");
            } else {
                setStatus("302", "Found");
            }
            setHeader("Location", url);
            setBody("");
        }
        
        void json(const std::string& jsonBody) {
            setContentType("application/json; charset=UTF-8");
            setBody(jsonBody);
        }
    };

};