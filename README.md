# 基于C++20协程的异步HTTP服务器

这是一个使用C++20协程和Epoll实现的高性能异步HTTP服务器，专为Linux环境设计。

## 功能特性

- **高性能异步IO**：基于C++20协程实现真正的非阻塞IO操作
- **静态文件服务**：支持静态文件托管，自动识别MIME类型
- **目录浏览**：可配置的目录内容浏览功能
- **默认索引文件**：访问目录时自动查找默认文件（index.html, index.htm, default.html）
- **性能监控**：内置实时性能监控，可通过/server-status访问
- **服务器信息**：通过/server-info查看服务器配置和运行状态
- **完全配置化**：支持通过配置文件自定义服务器设置

## 项目结构

### 核心组件
- **main.cpp**: 程序入口，包含服务器初始化和事件循环
- **Connection.hpp**: HTTP连接类，处理单个客户端连接的生命周期
- **ConnectionManager.hpp/cpp**: 连接管理器，管理所有活动的连接
- **Config.hpp**: 配置管理器，读取和管理服务器配置
- **FileService.hpp**: 文件服务组件，处理静态文件访问和目录列表
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
- 文件缓存系统优化静态文件访问
- 使用fmt库进行高性能文本格式化

## 配置选项

服务器通过`server.conf`配置文件支持以下设置：

```
host=127.0.0.1            # 服务器监听地址
port=8080                 # 服务器监听端口
root_dir=./www            # 静态文件根目录
allow_directory_listing=true  # 是否允许列出目录内容
log_level=info            # 日志级别：debug, info, warning, error, fatal
max_connections=10000     # 最大并发连接数
connection_timeout=5      # 连接超时时间（秒）
```

## 编译与运行
```bash
mkdir -p build && cd build
cmake ..
make
./HttpWebServer
```

服务器默认监听127.0.0.1:8080端口，访问http://127.0.0.1:8080/可以查看web内容。
