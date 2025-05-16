#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <fmt/format.h>
#include <string>
#include <cstring>
#include <memory>
#include <cerrno>
#include <csignal>
#include <atomic>

#include "network/AsyncIO.hpp"
#include "network/NetworkOperation.hpp"
#include "network/AddrInfoWrapper.hpp"
#include "network/SocketWrapper.hpp"
#include "core/Connection.hpp"
#include "src/core/ConnectionManager.hpp"
#include "utils/PerformanceMonitor.hpp"

// 初始化连接协程
Task g_acceptTask=nullptr;

// 全局变量，用于控制服务器运行状态
std::atomic<bool> g_serverRunning = true;

// 信号处理函数
void signalHandler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        LOG_INFO(fmt::format("接收到信号 {}, 准备关闭服务器...", signum));
        g_serverRunning = false;
    }
}


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
    // fmt::print("getaddrinfo succeeded\n");
    LOG_INFO("getaddrinfo succeeded");
    
    // 创建socket
    SocketWrapper sock(
        addrInfo.get()->ai_family,
        addrInfo.get()->ai_socktype,
        addrInfo.get()->ai_protocol
    );
    // fmt::print("Socket created with fd: {}\n", sock.get());
    LOG_INFO(fmt::format("Socket created with fd: {}", sock.get()));
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
    // fmt::print("Socket bound and listening on {}:{}\n", host, port);
    LOG_INFO(fmt::format("Socket bound and listening on {}:{}", host, port));
    return sock;
}

// 关闭连接
void closeConnection(int fd,int epollFd) {
    ConnectionManager::getInstance().removeConnection(fd);
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
    const int MAX_EVENTS = 10;
    struct epoll_event events[MAX_EVENTS];
    
    // 设置超时，以便定期检查服务器是否应该关闭
    const int TIMEOUT_MS = 100; // 100ms超时，平衡响应性和CPU使用率
    
    while (g_serverRunning) {
        int nfds = epoll_wait(epollFd, events, MAX_EVENTS, TIMEOUT_MS);
        if (nfds == -1) {
            if (errno == EINTR) continue; // 信号中断，继续
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
    
    // 优雅关闭程序
    LOG_INFO("开始进行服务器关闭...");
    
    // 1. 停止接受新连接 (g_acceptTask会在下一次co_await时自动结束)
    // 2. 记录现有活动连接数
    size_t activeConnections = ConnectionManager::getInstance().getActiveConnectionCount();
    LOG_INFO(fmt::format("当前活动连接数: {}", activeConnections));
    
    // 3. 给所有连接一定时间完成当前请求
    const int GRACEFUL_TIMEOUT_SEC = 3;
    LOG_INFO(fmt::format("等待 {} 秒让现有连接完成...", GRACEFUL_TIMEOUT_SEC));
    
    // 等待一段时间或者直到所有连接都关闭
    time_t startTime = time(nullptr);
    while (ConnectionManager::getInstance().getActiveConnectionCount() > 0) {
        // 检查是否超时
        if (difftime(time(nullptr), startTime) > GRACEFUL_TIMEOUT_SEC) {
            LOG_WARNING("优雅关闭超时，强制关闭剩余连接");
            ConnectionManager::getInstance().closeAllConnections();
            break;
        }
        
        // 短暂睡眠，减少CPU使用率
        usleep(100000); // 100ms
    }
    
    LOG_INFO("所有连接已关闭，服务器关闭完成");
}
void runServer(const std::string& host, const std::string& port, const std::string& rootDir) {
    // 初始化服务器
    LOG_INFO(fmt::format("初始化服务器 {}:{}", host, port));
    SocketWrapper serverSocket = initializeServer(host.c_str(), port.c_str());
    setNonBlocking(serverSocket.get());
    
    // 初始化文件服务
    LOG_INFO(fmt::format("初始化文件服务，根目录: {}", rootDir));
    if (!FileService::getInstance().init(rootDir)) {
        LOG_FATAL("文件服务初始化失败");
        throw std::runtime_error("文件服务初始化失败");
    }
    
    // 创建 epoll 实例
    int epollFd = createEpoll();
    LOG_INFO("创建epoll实例成功");
    
    // 创建accept协程任务
    g_acceptTask = acceptConnection(serverSocket.get(), epollFd);
    LOG_INFO("创建accept协程任务成功");
    
    // 开始事件循环
    LOG_INFO("开始事件循环");
    eventLoop(epollFd);

    // 关闭服务器
    close(epollFd);
    close(serverSocket.get());
}

int main() {
    try {
        // 设置信号处理
        signal(SIGINT, signalHandler);  // 处理Ctrl+C
        signal(SIGTERM, signalHandler); // 处理terminate信号
        
        // 设置中文
        setlocale(LC_ALL, "zh_CN.UTF-8");
        
        // 加载配置文件
        if (!Config::getInstance().loadFromFile("server.conf")) {
            fmt::print("警告: 无法加载配置文件，将使用默认配置\n");
        }
        
        // 读取日志配置
        std::string logFile = Config::getInstance().getString("log_file", "server.log");
        std::string logLevelStr = Config::getInstance().getString("log_level", "info");
        bool enableLogging = Config::getInstance().getBool("enable_logging", true);
        bool enableConsoleOutput = Config::getInstance().getBool("enable_console_output", true);
        
        LogLevel logLevel = LogLevel::INFO;
        if (logLevelStr == "debug") logLevel = LogLevel::DEBUG;
        else if (logLevelStr == "info") logLevel = LogLevel::INFO;
        else if (logLevelStr == "warning") logLevel = LogLevel::WARNING;
        else if (logLevelStr == "error") logLevel = LogLevel::ERROR;
        else if (logLevelStr == "fatal") logLevel = LogLevel::FATAL;
        
        // 初始化日志系统
        if (!Logger::getInstance().init(logFile, logLevel, enableLogging, enableConsoleOutput)) {
            if (enableConsoleOutput) {
                fmt::print("警告: 无法初始化日志系统，日志将只输出到控制台\n");
            }
        }
        
        // 设置性能监控
        bool enablePerformanceMonitoring = Config::getInstance().getBool("enable_performance_monitoring", false);
        PerformanceMonitor::getInstance().setEnabled(enablePerformanceMonitoring);
        
        // 获取服务器配置
        std::string host = Config::getInstance().getString("host", "127.0.0.1");
        std::string port = Config::getInstance().getString("port", "8080");
        std::string rootDir = Config::getInstance().getString("root_dir", "./www");
        
        // 启动服务器
        LOG_INFO("服务器正在启动...");
        runServer(host, port, rootDir);
    } catch (const std::exception& e) {
        LOG_FATAL(fmt::format("服务器启动失败: {}", e.what()));
        fmt::print("错误: {}\n", e.what());
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}