#pragma once
#include "SocketAddressStorage.hpp"
#include <coroutine>
#include <fmt/format.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cerrno>

// IO操作的等待器
class IoAwaiter {
public:
    IoAwaiter(int fd, int epollFd, uint32_t events) 
        : m_fd(fd), m_epollFd(epollFd), m_events(events) {}
    
    bool await_ready() const { return false; } // 总是等待IO事件
    
    void await_suspend(std::coroutine_handle<> handle) {
        // 将协程句柄存储在epoll事件的数据中
        epoll_event ev;
        ev.events = m_events;
        ev.data.ptr = handle.address();
        
        // 尝试添加，如果已存在则修改
        if (epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_fd, &ev) == -1) {
            if (errno == EEXIST) {
                // 如果文件描述符已存在，则修改它
                epoll_ctl(m_epollFd, EPOLL_CTL_MOD, m_fd, &ev);
            } else {
                // 真正的错误
                throw std::runtime_error(fmt::format("epoll_ctl 失败: {}", strerror(errno)));
            }
        }
    }
    
    void await_resume() {} // 只需要恢复协程执行即可
    
private:
    int m_fd;
    int m_epollFd;
    uint32_t m_events;
};

// 用于读取操作的awaiter
class ReadAwaiter : public IoAwaiter {
public:
    ReadAwaiter(int fd, int epollFd) : IoAwaiter(fd, epollFd, EPOLLIN | EPOLLET) {}
};

// 用于写入操作的awaiter
class WriteAwaiter : public IoAwaiter {
public:
    WriteAwaiter(int fd, int epollFd) : IoAwaiter(fd, epollFd, EPOLLOUT | EPOLLET) {}
};


// 用于异步 accept 的 awaiter
class AcceptAwaiter {
private:
    int serverFd;
    int epollFd;

public:
    AcceptAwaiter(int serverFd, int epollFd) : serverFd(serverFd), epollFd(epollFd) {}

    bool await_ready() const noexcept {
        // 尝试非阻塞 accept，如果立即成功则返回 true
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) {
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.ptr = h.address();
        
        if (epoll_ctl(epollFd, EPOLL_CTL_ADD, serverFd, &ev) == -1) {
            if (errno == EEXIST) {
                // 如果文件描述符已存在，则修改它
                if (epoll_ctl(epollFd, EPOLL_CTL_MOD, serverFd, &ev) == -1) {
                    throw std::runtime_error(fmt::format("epoll_ctl MOD failed in AcceptAwaiter: {}", strerror(errno)));
                }
            } else {
                // 真正的错误
                throw std::runtime_error(fmt::format("epoll_ctl failed in AcceptAwaiter: {}", strerror(errno)));
            }
        }
    }

    int await_resume() {
        SocketAddressStorage clientAddr;
        
        int clientFd = accept(serverFd, clientAddr.get_addr(), &clientAddr.get_length());
        if (clientFd == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                throw std::runtime_error(fmt::format("accept failed: {}", strerror(errno)));
            }
            return -1;  // 没有新连接
        }
        
        return clientFd;  // 返回新客户端连接的文件描述符
    }
};

