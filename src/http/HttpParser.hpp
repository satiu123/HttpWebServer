#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
class HttpParser {
public:
    std::string header;

    std::string headline;
    std::unordered_map<std::string, std::string>headerMap;

    std::string body;
    size_t contentLength;
    bool headerIsFinished = false;
public:
    HttpParser() : contentLength(0) {}

    void parse(std::string_view request) {
        // 解析请求头
        if (!headerIsFinished) {
            size_t pos = request.find("\r\n\r\n");
            if (pos != std::string::npos) {
                headerIsFinished = true;
                // 分割请求头和请求体
                // pos + 4 是为了跳过\r\n\r\n
                header = std::string_view(request.substr(0, pos));
                body = std::string_view(request.substr(pos + 4));
                headerIsFinished = true;
                getContentLength();
            } else {
                header += request;
            }
        }else{
            // 解析请求体
            body += request;
        }
        // 检查是否完成
        if (body.size() >= contentLength) {
            headerIsFinished = true;
        }
    }
    void extractHeader(){
        size_t pos = header.find("\r\n");
        // 解析请求头字段
        size_t start = pos + 2;
        while ((pos = header.find("\r\n", start)) != std::string::npos) {
            std::string line = header.substr(start, pos - start);
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string key = line.substr(0, colonPos);
                std::string value = line.substr(colonPos + 2); // 跳过": "
                headerMap[key] = value;
            }
            start = pos + 2;
        }
    }
    std::tuple<std::string, std::string, std::string> extractHeadLine() {
        // 解析请求行
        size_t pos = header.find("\r\n");
        if (pos != std::string::npos) {
            headline = header.substr(0, pos);
            size_t firstEnd = headline.find(' ');
            size_t secondEnd = headline.find(' ', firstEnd + 1);
            if (firstEnd != std::string::npos && secondEnd != std::string::npos) {
                return std::make_tuple(headline.substr(0, firstEnd), 
                                    headline.substr(firstEnd + 1, secondEnd - firstEnd - 1),
                                    headline.substr(secondEnd + 1));
            }
        }
        return std::make_tuple("", "", "");
    }
    void getContentLength() {
        // 解析Content-Length
        size_t pos = header.find("Content-Length: ");
        if (pos != std::string::npos) {
            pos += 16; // 跳过"Content-Length: "
            size_t end = header.find("\r\n", pos);
            contentLength = std::stoul(header.substr(pos, end - pos));
        } else {
            contentLength = 0;
        }
    }
    bool isComplete() const {
        return headerIsFinished && body.size() >= contentLength;
    }
    void reset() {
        header.clear();
        headline.clear();
        headerMap.clear();
        body.clear();
        contentLength = 0;
        headerIsFinished = false;
    }
};

class RequestParser : public HttpParser {
private:
    std::string _method;
    std::string _url;
    std::string _version;
public:
    RequestParser() : HttpParser() {}
    
    void parse(std::string_view request) {
        HttpParser::parse(request);
        if (isComplete()) {
            extractHeader();
            std::tie(_method, _url, _version) = extractHeadLine();
        }
    }
    
    std::string method() const {
        return _method;
    }
    std::string url() const {
        return _url;
    }
    std::string version() const {
        return _version;
    }
    void reset() {
        HttpParser::reset();
        _method.clear();
        _url.clear();
        _version.clear();
    }
};

class ResponseParser : public HttpParser {
private:
    std::string _statusCode;
public:
    ResponseParser() : HttpParser() {}
    
    void parse(std::string_view response) {
        HttpParser::parse(response);
        if (isComplete()) {
            extractHeader();
            std::tie(std::ignore, _statusCode, std::ignore) = extractHeadLine();
        }
    }
    std::string statusCode() const {
        return _statusCode;
    }
    void clear() {
        HttpParser::reset();
        _statusCode.clear();
    }
};