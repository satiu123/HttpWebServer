#pragma once
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