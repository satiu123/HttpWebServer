#pragma once

#include <sys/socket.h>
#include <netdb.h>
#include <string>
#include <cstring>
#include <fmt/format.h>

class SocketAddressStorage {
public:
    struct sockaddr_storage addr;
    socklen_t addrlen;
    
    SocketAddressStorage() : addrlen(sizeof(addr)) {
        memset(&addr, 0, sizeof(addr));
    }
    
    // 获取指向地址结构的指针（用于传递给socket API）
    struct sockaddr* get_addr() {
        return reinterpret_cast<struct sockaddr*>(&addr);
    }
    
    // 获取指向地址结构的指针（常量版本）
    const struct sockaddr* get_addr() const {
        return reinterpret_cast<const struct sockaddr*>(&addr);
    }
    
    // 获取地址长度的引用（用于传递给accept等函数）
    socklen_t& get_length() {
        return addrlen;
    }
    
    // 获取地址长度（常量版本）
    socklen_t get_length() const {
        return addrlen;
    }
    
    // 转换为人类可读的地址字符串
    std::string to_string() const {
        char host[NI_MAXHOST];
        char serv[NI_MAXSERV];
        
        int res = getnameinfo(
            get_addr(), addrlen,
            host, sizeof(host),
            serv, sizeof(serv),
            NI_NUMERICHOST | NI_NUMERICSERV
        );
        
        if (res != 0) {
            return "未知地址";
        }
        
        return fmt::format("{}:{}", host, serv);
    }
};