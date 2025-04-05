#pragma once

#include <netdb.h>
#include <string>
#include "NetworkException.hpp"

// 自定义网络操作类
class NetworkOperation {
public:
    // 执行可能失败的网络操作并自动检查错误
    static int execute(int result, const std::string& operation) {
        if (result == -1) {
            throw NetworkException(operation);
        }
        return result;
    }
    
    // 处理getaddrinfo特殊情况，因为它返回的不是-1而是各种错误代码
    static void checkGetAddrInfo(int result, const std::string& operation) {
        if (result != 0) {
            throw std::runtime_error(fmt::format("{}: {}", operation, gai_strerror(result)));
        }
    }
};