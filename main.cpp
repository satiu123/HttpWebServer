#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <fmt/format.h>
#include <string>
#include <cstring>
#include <unordered_map>
#include <memory>
#include <cerrno>


#include "HttpServer.hpp"
#include "NetworkOperation.hpp"
#include "AddrInfoWrapper.hpp"
#include "SocketWrapper.hpp"
#include "SocketAddressStorage.hpp"
#include "Connection.hpp"

// 存储所有活动连接
std::unordered_map<int, std::shared_ptr<Connection>> connections;

// 设置非阻塞套接字
void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        throw std::runtime_error("fcntl F_GETFL failed");
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::runtime_error("fcntl F_SETFL O_NONBLOCK failed");
    }
}

// 创建epoll实例
int createEpoll() {
    int epollFd = epoll_create1(0);
    if (epollFd == -1) {
        throw std::runtime_error("epoll_create1 failed");
    }
    return epollFd;
}

// 初始化HTTP服务器
SocketWrapper initializeServer(const char* host, const char* port) {
    // 设置addrinfo提示
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // 允许IPv4或IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP连接
    
    // 使用RAII封装获取地址信息
    AddrInfoWrapper addrInfo(host, port, &hints);
    fmt::print("getaddrinfo succeeded\n");
    
    // 创建socket
    SocketWrapper sock(
        addrInfo.get()->ai_family,
        addrInfo.get()->ai_socktype,
        addrInfo.get()->ai_protocol
    );
    fmt::print("Socket created with fd: {}\n", sock.get());
    
    // 绑定
    NetworkOperation::execute(
        bind(sock.get(), addrInfo.get()->ai_addr, addrInfo.get()->ai_addrlen),
        "bind"
    );
    
    // 监听
    NetworkOperation::execute(
        listen(sock.get(), SOMAXCONN),
        "listen"
    );
    int reuseaddr = 1;
    if (setsockopt(sock.get(), SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr)) == -1) {
        throw std::runtime_error("setsockopt failed");
    }
    return sock;
}

// 关闭连接
void closeConnection(int fd,int epollFd) {
    connections.erase(fd);
    close(fd);
    struct epoll_event ev;
    ev.events = EPOLL_CTL_DEL;
    ev.data.fd = fd;
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, &ev);
    fmt::print("连接已关闭: {}\n", fd);
}

// 接受新连接
// void acceptConnection(int serverFd, int epollFd) {
//     SocketAddressStorage clientAddr;
    
//     int clientFd = accept(serverFd, clientAddr.get_addr(), &clientAddr.get_length());
//     if (clientFd == -1) {
//         if (errno != EAGAIN && errno != EWOULDBLOCK) {
//             fmt::print("accept failed: {}\n", strerror(errno));
//         }
//         return;
//     }
    
//     // 设置为非阻塞
//     setNonBlocking(clientFd);
    
//     // 创建新连接对象
//     auto conn = std::make_shared<Connection>(clientFd);
//     connections[clientFd] = conn;
    
//     // 添加到 epoll
//     struct epoll_event ev;
//     ev.events = EPOLLIN | EPOLLET; // 边缘触发
//     ev.data.fd = clientFd;
//     if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &ev) == -1) {
//         throw std::runtime_error("epoll_ctl: add client socket");
//     }
    
//     fmt::print("新连接: {}\n", clientFd);
// }
// 修改事件循环部分
// 接受新连接
void acceptConnection(int serverFd, int epollFd) {
    SocketAddressStorage clientAddr;
    
    int clientFd = accept(serverFd, clientAddr.get_addr(), &clientAddr.get_length());
    if (clientFd == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            fmt::print("accept failed: {}\n", strerror(errno));
        }
        return;
    }
    
    // 设置为非阻塞
    setNonBlocking(clientFd);
    
    // 创建新连接并添加到管理器
    auto conn = std::make_shared<Connection>(clientFd);
    ConnectionManager::getInstance().addConnection(conn);
    
    fmt::print("新连接: {}\n", clientFd);
    
    // 启动协程处理连接
    conn->task=conn->startHandleConnection(epollFd);
}
//事件循环
void eventLoop(int serverFd, int epollFd) {
    const int MAX_EVENTS = 64;
    struct epoll_event events[MAX_EVENTS];
    
    while (true) {
        int nfds = epoll_wait(epollFd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            throw std::runtime_error("epoll_wait failed");
        }
        
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            
            // 如果是服务器套接字，接受新连接
            if (fd == serverFd) {
                acceptConnection(serverFd, epollFd);
            }
            // 如果是协程恢复
            else if (events[i].data.ptr != nullptr) {
                // 恢复协程
                std::coroutine_handle<>::from_address(events[i].data.ptr).resume();
            }
        }
    }
}

// //事件循环
// void eventLoop(int serverFd, int epollFd) {
//     const int MAX_EVENTS = 64;
//     struct epoll_event events[MAX_EVENTS];
    
//     while (true) {
//         int nfds = epoll_wait(epollFd, events, MAX_EVENTS, -1);
//         if (nfds == -1) {
//             if (errno == EINTR) continue;
//             throw std::runtime_error("epoll_wait failed");
//         }
        
//         for (int i = 0; i < nfds; ++i) {
//             int fd = events[i].data.fd;
            
//             // 如果是服务器套接字，接受新连接
//             if (fd == serverFd) {
//                 acceptConnection(serverFd, epollFd);
//             }
//             // 否则处理客户端连接事件
//             else {
//                 if (events[i].events & EPOLLIN) {
//                     handleRead(fd, epollFd);
//                 }
//                 if (events[i].events & EPOLLOUT) {
//                     handleWrite(fd, epollFd);
//                 }
//                 if (events[i].events & (EPOLLERR | EPOLLHUP)) {
//                     closeConnection(fd,epollFd);
//                 }
//             }
//         }
//     }
// }

int main() {
    try {
        // 设置中文
        setlocale(LC_ALL, "zh_CN.UTF-8");
        
        // 初始化服务器
        SocketWrapper serverSocket = initializeServer("localhost", "8080");
        setNonBlocking(serverSocket.get());
        
        // 创建 epoll 实例
        int epollFd = createEpoll();
        
        // 注册服务器套接字
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = serverSocket.get();
        if (epoll_ctl(epollFd, EPOLL_CTL_ADD, serverSocket.get(), &ev) == -1) {
            throw std::runtime_error("epoll_ctl: server socket");
        }
        
        fmt::print("服务器启动在 localhost:8080\n");
        
        // 开始事件循环
        eventLoop(serverSocket.get(), epollFd);
        
        // 关闭服务器
        close(epollFd);
        close(serverSocket.get());
        
    } catch (const std::exception& e) {
        fmt::print("错误: {}\n", e.what());
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}