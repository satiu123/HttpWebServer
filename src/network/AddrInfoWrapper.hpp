#pragma once

#include <netdb.h>
#include "NetworkOperation.hpp"

// RAII包装的addrinfo资源
class AddrInfoWrapper {
private:
    struct addrinfo* info;
public:
    AddrInfoWrapper(const char* host, const char* service, 
                  const struct addrinfo* hints = nullptr) : info(nullptr) {
        struct addrinfo* result;
        int res = getaddrinfo(host, service, hints, &result);
        NetworkOperation::checkGetAddrInfo(res, "getaddrinfo");
        info = result;
    }
    
    ~AddrInfoWrapper() {
        if (info) freeaddrinfo(info);
    }
    
    // 不允许复制
    AddrInfoWrapper(const AddrInfoWrapper&) = delete;
    AddrInfoWrapper& operator=(const AddrInfoWrapper&) = delete;
    
    // 允许移动
    AddrInfoWrapper(AddrInfoWrapper&& other) noexcept : info(other.info) {
        other.info = nullptr;
    }
    
    AddrInfoWrapper& operator=(AddrInfoWrapper&& other) noexcept {
        if (this != &other) {
            if (info) freeaddrinfo(info);
            info = other.info;
            other.info = nullptr;
        }
        return *this;
    }
    
    // 访问底层addrinfo
    struct addrinfo* get() const { return info; }
};