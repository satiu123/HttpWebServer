#include <sys/epoll.h>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <cstddef>
#include <fmt/format.h>
#include <vector>
#include <string>
#include <cstring>

#include "HttpServer.hpp"
#include "NetworkOperation.hpp"
#include "AddrInfoWrapper.hpp"
#include "SocketWrapper.hpp"
#include "SocketAddressStorage.hpp"

std::vector<std::thread> threads;

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
    
    return sock;
}

// 处理HTTP请求
void handleHttpRequest(int clientFd) {
    while(true) {
        char buffer[1024];
        HttpServer::HttpRequest request;
        do {
            size_t n = NetworkOperation::execute(
                read(clientFd, buffer, sizeof(buffer)),
                "read"
            );
            if (n <= 0) {
                goto end;
            }
            request.parseRequest(std::string_view(buffer, n));
        } while(!request.isComplete());
        
        fmt::print("收到请求:{}\n", clientFd);
    
        // 处理请求
        HttpServer::HttpResponse response;
        response.setStatus("200", "OK");
        // 发送响应
        response.setBody("Hello, World!");
        response.setHeader("Content-Length", std::to_string(response.bodyLength()));
        response.writeResponse(clientFd);
        fmt::print("响应请求:{}\n", clientFd);
    }
end:
    fmt::print("结束请求: {}\n", clientFd);
    close(clientFd); // 关闭客户端连接
}

// 接受并处理客户端连接
void acceptClients(const SocketWrapper& serverSocket) {
    while (true) {
        SocketAddressStorage clientAddr;
        // 接受连接
        int clientFd = NetworkOperation::execute(
            accept(serverSocket.get(), clientAddr.get_addr(), &clientAddr.get_length()),
            "accept"
        );
        
        threads.emplace_back([clientFd]() {
            handleHttpRequest(clientFd);
        });
    }
}

int main() {
    try {
        // 设置中文
        setlocale(LC_ALL, "zh_CN.UTF-8");
        
        // 初始化服务器
        SocketWrapper serverSocket = initializeServer("localhost", "8080");
        
        // 接受并处理客户端连接
        acceptClients(serverSocket);
        
        // 等待所有线程完成
        for(auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        // 关闭服务器socket
        close(serverSocket.get());
        
    } catch (const std::exception& e) {
        fmt::print("错误: {}\n", e.what());
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}