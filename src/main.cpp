#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <fmt/format.h>
#include <string>
#include <cstring>
#include <unordered_map>
#include <memory>
#include <cerrno>

#include "network/AsyncIO.hpp"
#include "network/NetworkOperation.hpp"
#include "network/AddrInfoWrapper.hpp"
#include "network/SocketWrapper.hpp"
#include "core/Connection.hpp"

// 存储所有活动连接
std::unordered_map<int, std::shared_ptr<Connection>> connections;

// 初始化连接协程
Task g_acceptTask=nullptr;


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
    int reuseaddr = 1;
    if (setsockopt(sock.get(), SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr)) == -1) {
        throw std::runtime_error("setsockopt failed");
    }
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
    fmt::print("Socket bound and listening on {}:{}\n", host, port);
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
Task acceptConnection(int serverFd, int epollFd) {
    while(true){
        int clientFd = co_await AcceptAwaiter(serverFd, epollFd);
        if (clientFd == -1) {
            continue;
        }
        try{
            // 设置为非阻塞
            setNonBlocking(clientFd);
            
            // 创建新连接并添加到管理器
            auto conn = std::make_shared<Connection>(clientFd);
            ConnectionManager::getInstance().addConnection(conn);
            
            // fmt::print("新连接: {}\n", clientFd);
            
            // 启动协程处理连接
            conn->startHandleConnection(epollFd);
        }catch(const std::exception& e){
            fmt::print("处理连接时发生错误: {}\n", e.what());
            closeConnection(clientFd, epollFd);
            // close(clientFd);
        }
    }
    co_return;
}
//事件循环
void eventLoop(int epollFd) {
    const int MAX_EVENTS = 64;
    struct epoll_event events[MAX_EVENTS];
    while (true) {
        int nfds = epoll_wait(epollFd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            throw std::runtime_error("epoll_wait failed");
        }
        
        for (int i = 0; i < nfds; ++i) {
            // 如果是协程恢复
            if (events[i].data.ptr != nullptr) {
                // 恢复协程
                std::coroutine_handle<>::from_address(events[i].data.ptr).resume();
            }
        }
    }
}
void runServer() {
    // 初始化服务器
    SocketWrapper serverSocket = initializeServer("localhost", "8080");
    setNonBlocking(serverSocket.get());
    
    // 创建 epoll 实例
    int epollFd = createEpoll();
    
    // 创建accept协程任务
    g_acceptTask = acceptConnection(serverSocket.get(), epollFd);
    
    // 开始事件循环
    eventLoop(epollFd);

    // 关闭服务器
    close(epollFd);
    close(serverSocket.get());
}
int main() {
    try {
        // 设置中文
        setlocale(LC_ALL, "zh_CN.UTF-8");
        
        // 启动服务器
        fmt::print("服务器正在启动...\n");
        runServer();
        
        
    } catch (const std::exception& e) {
        fmt::print("错误: {}\n", e.what());
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}