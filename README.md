# 基于C++20协程的异步HTTP服务器

这是一个使用C++20协程和Epoll实现的高性能异步HTTP服务器。

## 项目结构

### 核心组件
- **main.cpp**: 程序入口，包含服务器初始化和事件循环
- **Connection.hpp**: HTTP连接类，处理单个客户端连接的生命周期
- **ConnectionManager.hpp/cpp**: 连接管理器，管理所有活动的连接
- **HttpServer.hpp**: HTTP服务器相关类，包含请求和响应处理
- **HttpParser.hpp**: HTTP解析器，解析HTTP请求和响应
- **Task.hpp**: 协程任务类，封装协程功能

### 网络层
- **AsyncIO.hpp**: 异步IO操作的awaiter类
- **SocketWrapper.hpp**: 套接字RAII包装器
- **SocketAddressStorage.hpp**: 套接字地址存储包装
- **AddrInfoWrapper.hpp**: getaddrinfo结果的RAII包装
- **NetworkOperation.hpp**: 网络操作执行与错误处理
- **NetworkException.hpp**: 网络异常处理

## 技术实现
- 使用C++20协程实现异步非阻塞IO
- 基于Epoll的事件驱动架构
- 使用RAII模式管理资源
- 完全异步的HTTP请求处理流程

## 编译与运行
```bash
mkdir -p build && cd build
cmake ..
make
./main
```

服务器默认监听localhost:8080端口。
