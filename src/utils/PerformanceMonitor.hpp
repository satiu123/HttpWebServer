#pragma once

#include <chrono>
#include <string>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <fmt/format.h>
#include "../core/Logger.hpp"

class PerformanceMonitor {
public:
    static PerformanceMonitor& getInstance() {
        static PerformanceMonitor instance;
        return instance;
    }
    
    // 设置是否启用性能监控
    void setEnabled(bool enabled) {
        this->enabled = enabled;
    }
    
    // 获取是否启用性能监控
    bool isEnabled() const {
        return enabled;
    }

    // 开始一个请求的计时
    void startRequest(const std::string& requestId, const std::string& method, const std::string& path) {
        if (!enabled) return;
        
        std::lock_guard<std::mutex> lock(mutex);
        auto now = std::chrono::high_resolution_clock::now();
        requests[requestId] = {method, path, now, {}};
        activeRequests++;
        totalRequests++;
    }

    // 结束一个请求的计时并更新统计信息
    void endRequest(const std::string& requestId, int statusCode) {
        if (!enabled) return;
        
        std::lock_guard<std::mutex> lock(mutex);
        auto now = std::chrono::high_resolution_clock::now();
        
        auto it = requests.find(requestId);
        if (it != requests.end()) {
            // 计算处理时间（毫秒）
            double processingTime = std::chrono::duration<double, std::milli>(now - it->second.startTime).count();
            
            // 更新总体统计
            totalProcessingTime += processingTime;
            requestsProcessed++;
            
            // 更新对应状态码的统计
            statusCodes[statusCode]++;
            
            // 更新最大和最小处理时间
            maxProcessingTime = std::max(maxProcessingTime, processingTime);
            minProcessingTime = (minProcessingTime == 0) ? processingTime : std::min(minProcessingTime, processingTime);
            
            // 计算移动平均响应时间
            if (avgProcessingTime == 0) {
                avgProcessingTime = processingTime;
            } else {
                avgProcessingTime = avgProcessingTime * 0.9 + processingTime * 0.1; // 移动平均
            }
            
            // 记录处理时间
            if (processingTime > slowThreshold) {
                LOG_WARNING(fmt::format("慢请求: {} {} {} - {}ms (状态码: {})", 
                    it->second.method, it->second.path, requestId, processingTime, statusCode));
            } else {
                LOG_DEBUG(fmt::format("请求完成: {} {} {} - {}ms (状态码: {})", 
                    it->second.method, it->second.path, requestId, processingTime, statusCode));
            }
            
            // 从活动请求中移除
            requests.erase(it);
            activeRequests--;
        }
    }

    // 记录连接建立
    void connectionEstablished() {
        if (!enabled) return;
        
        activeConnections++;
        totalConnections++;
    }

    // 记录连接关闭
    void connectionClosed() {
        if (!enabled) return;
        
        if (activeConnections > 0) {
            activeConnections--;
        }
    }

    // 获取性能统计摘要
    std::string getStatsSummary() const {
        if (!enabled) return "性能监控已禁用";
        
        std::lock_guard<std::mutex> lock(mutex);
        
        return fmt::format(
            "性能统计:\n"
            "- 总请求数: {}\n"
            "- 处理完成请求数: {}\n"
            "- 活动请求数: {}\n"
            "- 活动连接数: {}\n"
            "- 总连接数: {}\n"
            "- 平均处理时间: {:.2f}ms\n"
            "- 最小处理时间: {:.2f}ms\n"
            "- 最大处理时间: {:.2f}ms\n"
            "- 慢请求阈值: {:.2f}ms\n",
            totalRequests.load(),
            requestsProcessed.load(),
            activeRequests.load(),
            activeConnections.load(),
            totalConnections.load(),
            avgProcessingTime,
            minProcessingTime,
            maxProcessingTime,
            slowThreshold
        );
    }

    // 设置慢请求阈值
    void setSlowThreshold(double threshold) {
        slowThreshold = threshold;
    }

private:
    PerformanceMonitor() : 
        totalRequests(0), 
        requestsProcessed(0), 
        activeRequests(0),
        totalConnections(0),
        activeConnections(0),
        totalProcessingTime(0),
        avgProcessingTime(0),
        minProcessingTime(0),
        maxProcessingTime(0),
        slowThreshold(200) {} // 默认慢请求阈值为200ms
    
    struct RequestInfo {
        std::string method;
        std::string path;
        std::chrono::high_resolution_clock::time_point startTime;
        std::unordered_map<std::string, std::string> metadata;
    };

    mutable std::mutex mutex;
    std::unordered_map<std::string, RequestInfo> requests;
    std::unordered_map<int, int> statusCodes;

    std::atomic<size_t> totalRequests;
    std::atomic<size_t> requestsProcessed;
    std::atomic<size_t> activeRequests;
    std::atomic<size_t> totalConnections;
    std::atomic<size_t> activeConnections;
    
    double totalProcessingTime;
    double avgProcessingTime;
    double minProcessingTime;
    double maxProcessingTime;
    double slowThreshold;
    bool enabled = false;  // 是否启用性能监控

    // 禁止复制和移动
    PerformanceMonitor(const PerformanceMonitor&) = delete;
    PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;
    PerformanceMonitor(PerformanceMonitor&&) = delete;
    PerformanceMonitor& operator=(PerformanceMonitor&&) = delete;
};
