#pragma once
#include <unordered_map>
#include <memory>
#include <functional>
#include <vector>

// 前向声明 Connection 类
class Connection;

class ConnectionManager {
private:
    std::unordered_map<int, std::shared_ptr<Connection>> connections;
    static ConnectionManager* instance;

    // 私有构造函数，确保单例模式
    ConnectionManager() = default;
    
public:
    static ConnectionManager& getInstance() {
        if (instance == nullptr) {
            instance = new ConnectionManager();
        }
        return *instance;
    }

    // 添加新连接
    void addConnection(std::shared_ptr<Connection> conn);

    // 获取连接
    std::shared_ptr<Connection> getConnection(int fd);

    // 移除连接
    void removeConnection(int fd);

    // 在下一个事件循环中执行函数
    void postTask(std::function<void()> task) {
        pendingTasks.push_back(task);
    }

    // 执行所有待处理的任务
    void executePendingTasks() {
        auto tasks = std::move(pendingTasks);
        for (auto& task : tasks) {
            task();
        }
    }

    bool hasConnection(int fd) const;
    
    size_t count() const {
        return connections.size();
    }

private:
    std::vector<std::function<void()>> pendingTasks;
};