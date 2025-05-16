#include "ConnectionManager.hpp"
#include "../utils/PerformanceMonitor.hpp"
#include "Connection.hpp"

// 初始化静态实例
ConnectionManager* ConnectionManager::instance = nullptr;

void ConnectionManager::addConnection(std::shared_ptr<Connection> conn) {
    connections[conn->fd] = conn;
}

std::shared_ptr<Connection> ConnectionManager::getConnection(int fd) {
    auto it = connections.find(fd);
    return (it != connections.end()) ? it->second : nullptr;
}

void ConnectionManager::removeConnection(int fd) {
    connections.erase(fd);
}

bool ConnectionManager::hasConnection(int fd) const {
    return connections.find(fd) != connections.end();
}