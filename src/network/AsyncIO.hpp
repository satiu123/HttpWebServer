#pragma once
#include "SocketAddressStorage.hpp"
#include <cmath>
#include <coroutine>
#include <fmt/format.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cerrno>
// 用于异步 accept 的 awaiter
class AcceptAwaiter {
private:
    int serverFd;
    int epollFd;
    int clientFd = -1; // 客户端文件描述符

public:
    AcceptAwaiter(int serverFd, int epollFd) : serverFd(serverFd), epollFd(epollFd) {}
    bool await_ready() {
        // 尝试立即接受连接
        SocketAddressStorage clientAddr;
        clientFd = ::accept(serverFd, clientAddr.get_addr(), &clientAddr.get_length());
        // fmt::print("accept once\n");
        if (clientFd != -1) {
            // 成功接受连接，不需要挂起
            return true;
        }
        
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // 发生真正的错误
            throw std::runtime_error(fmt::format("accept failed: {}", strerror(errno)));
        }
        
        // 没有连接可接受，需要挂起
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) {
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
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
        if (clientFd != -1) {
            // 如果在await_ready中已经接受了连接，直接返回
            return clientFd;
        }
        // fmt::print("accept twice\n");
        // 否则尝试再次接受连接
        SocketAddressStorage clientAddr;
        clientFd = ::accept(serverFd, clientAddr.get_addr(), &clientAddr.get_length());

        if (clientFd == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                throw std::runtime_error(fmt::format("accept failed: {}", strerror(errno)));
            }
            return -1;  // 仍然没有连接可接受
        }
        return clientFd;
    }
};

