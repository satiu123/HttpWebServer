#pragma once

#include <stdexcept>
#include <string>
#include <cstring>
#include <fmt/format.h>

// 自定义网络异常类
class NetworkException : public std::runtime_error {
public:
    NetworkException(const std::string& operation) 
        : std::runtime_error(fmt::format("{}: {}", operation, strerror(errno))) {}
};