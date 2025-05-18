#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <algorithm>

class RequestParser {
public:
    RequestParser() : contentLength(0), headerComplete(false), complete(false) {}

    void reset() {
        buffer.clear();
        method.clear();
        url.clear();
        path.clear();
        httpVersion.clear();
        headers.clear();
        bodyData.clear();
        contentLength = 0;
        headerComplete = false;
        complete = false;
    }

    void parse(std::string_view data) {
        // 追加数据到缓冲区
        buffer.append(data);

        // 如果头部尚未解析完成
        if (!headerComplete) {
            parseHeader();
        }

        // 如果头部已解析完成，但整个请求尚未完成
        if (headerComplete && !complete) {
            parseBody();
        }
    }

    bool isComplete() const {
        return complete;
    }

    const std::string& getMethod() const {
        return method;
    }

    const std::string& getUrl() const {
        return url;
    }

    const std::string& getPath() const {
        return path;
    }

    const std::string& getHttpVersion() const {
        return httpVersion;
    }

    std::string getHeader(const std::string& key) const {
        auto it = headers.find(key);
        if (it != headers.end()) {
            return it->second;
        }
        return "";
    }

    const std::unordered_map<std::string, std::string>& getHeaders() const {
        return headers;
    }

    const std::string& getBody() const {
        return bodyData;
    }

private:
    void parseHeader() {
        // 查找头部结束标记
        size_t headerEnd = buffer.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            return; // 头部尚未完整接收
        }

        // 提取并解析请求行
        size_t lineEnd = buffer.find("\r\n");
        if (lineEnd != std::string::npos) {
            std::string requestLine = buffer.substr(0, lineEnd);
            parseRequestLine(requestLine);
        }

        // 解析头部字段
        size_t pos = lineEnd + 2; // 跳过第一行的\r\n
        while (pos < headerEnd) {
            size_t nextLineEnd = buffer.find("\r\n", pos);
            if (nextLineEnd == std::string::npos || nextLineEnd > headerEnd) {
                break;
            }

            std::string headerLine = buffer.substr(pos, nextLineEnd - pos);
            parseHeaderLine(headerLine);
            pos = nextLineEnd + 2; // 跳过这一行的\r\n
        }

        // 获取Content-Length
        std::string contentLengthStr = getHeader("Content-Length");
        if (!contentLengthStr.empty()) {
            try {
                contentLength = std::stoul(contentLengthStr);
            } catch (...) {
                contentLength = 0;
            }
        }

        // 标记头部解析完成
        headerComplete = true;

        // 将headerEnd之后的数据作为消息体
        bodyData = buffer.substr(headerEnd + 4); // +4 跳过\r\n\r\n

        // 检查请求是否已完成
        parseBody();
    }

    void parseBody() {
        // 如果没有Content-Length，或已经接收到足够长度的消息体，则请求完成
        if (contentLength == 0 || bodyData.size() >= contentLength) {
            complete = true;
            
            // 如果消息体超长，截断它
            if (bodyData.size() > contentLength) {
                bodyData.resize(contentLength);
            }
        }
    }

    void parseRequestLine(const std::string& line) {
        // 解析请求方法
        size_t methodEnd = line.find(' ');
        if (methodEnd != std::string::npos) {
            method = line.substr(0, methodEnd);
            // 转换为大写
            std::transform(method.begin(), method.end(), method.begin(), ::toupper);
            
            // 解析URL
            size_t urlEnd = line.find(' ', methodEnd + 1);
            if (urlEnd != std::string::npos) {
                url = line.substr(methodEnd + 1, urlEnd - methodEnd - 1);
                
                // 解析HTTP版本
                httpVersion = line.substr(urlEnd + 1);
                
                // 解析path部分（移除查询参数）
                size_t queryStart = url.find('?');
                if (queryStart != std::string::npos) {
                    path = url.substr(0, queryStart);
                } else {
                    path = url;
                }
            }
        }
    }

    void parseHeaderLine(const std::string& line) {
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string key = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);
            
            // 移除前后空格
            key = trimString(key);
            value = trimString(value);
            
            // 存储头部字段（不区分大小写的键）
            headers[normalizeHeaderKey(key)] = value;
        }
    }

    std::string trimString(const std::string& str) {
        size_t first = str.find_first_not_of(" \t");
        if (first == std::string::npos) {
            return "";
        }
        size_t last = str.find_last_not_of(" \t");
        return str.substr(first, last - first + 1);
    }

    // 规范化头部键（首字母大写，其余小写）
    std::string normalizeHeaderKey(const std::string& key) {
        std::string normalized;
        bool nextUpper = true;
        
        for (char c : key) {
            if (c == '-') {
                normalized.push_back(c);
                nextUpper = true;
            } else if (nextUpper) {
                normalized.push_back(std::toupper(c));
                nextUpper = false;
            } else {
                normalized.push_back(std::tolower(c));
            }
        }
        
        return normalized;
    }

    std::string buffer;            // 请求数据缓冲区
    std::string method;            // 请求方法（GET、POST等）
    std::string url;               // 完整URL
    std::string path;              // URL路径部分
    std::string httpVersion;       // HTTP版本
    std::unordered_map<std::string, std::string> headers; // 头部字段
    std::string bodyData;          // 请求体数据
    size_t contentLength;          // Content-Length值
    bool headerComplete;           // 标记头部是否解析完成
    bool complete;                 // 标记整个请求是否解析完成
};
