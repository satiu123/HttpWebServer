#pragma once

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <fmt/format.h>
#include "NetworkOperation.hpp"

class SocketWrapper {
private:
    int fd;
public:
    SocketWrapper(int family, int type, int protocol) 
        : fd(NetworkOperation::execute(socket(family, type, protocol), "socket")) {}
    
    explicit SocketWrapper(int fd) : fd(fd) {
        if (fd == -1) throw NetworkException("socket");
    }
    
    ~SocketWrapper() {
        if (fd != -1) close(fd);
    }
    
    // 不允许复制
    SocketWrapper(const SocketWrapper&) = delete;
    SocketWrapper& operator=(const SocketWrapper&) = delete;
    
    // 允许移动
    SocketWrapper(SocketWrapper&& other) noexcept : fd(other.fd) {
        other.fd = -1;
    }
    
    SocketWrapper& operator=(SocketWrapper&& other) noexcept {
        if (this != &other) {
            if (fd != -1) close(fd);
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }
    
    int get() const { return fd; }
};