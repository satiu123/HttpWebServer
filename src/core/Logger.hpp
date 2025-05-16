#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <fmt/format.h>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    FATAL
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    // 初始化日志系统
    bool init(const std::string& logFilePath, LogLevel minLevel = LogLevel::INFO) {
        std::lock_guard<std::mutex> lock(mutex);
        logFile.open(logFilePath, std::ios::app);
        if (!logFile.is_open()) {
            fmt::print(stderr, "无法打开日志文件: {}\n", logFilePath);
            return false;
        }

        minLogLevel = minLevel;
        isInitialized = true;
        return true;
    }

    // 写入日志
    void log(LogLevel level, const std::string& message, const char* file = "", int line = 0) {
        if (!isInitialized || level < minLogLevel) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex);
        
        std::string timestamp = getCurrentTimestamp();
        std::string levelStr = getLevelString(level);
        std::string locationInfo = "";
        
        if (file && line > 0) {
            locationInfo = fmt::format(" [{}:{}]", file, line);
        }

        std::string logEntry = fmt::format("{} {} {}{}\n", timestamp, levelStr, message, locationInfo);
        
        // 写入文件
        logFile << logEntry;
        logFile.flush();
        
        // 同时输出到控制台
        if (level >= LogLevel::WARNING) {
            fmt::print(stderr, "{}", logEntry);
        } else {
            fmt::print("{}", logEntry);
        }
    }

    void debug(const std::string& message, const char* file = "", int line = 0) {
        log(LogLevel::DEBUG, message, file, line);
    }

    void info(const std::string& message, const char* file = "", int line = 0) {
        log(LogLevel::INFO, message, file, line);
    }

    void warning(const std::string& message, const char* file = "", int line = 0) {
        log(LogLevel::WARNING, message, file, line);
    }

    void error(const std::string& message, const char* file = "", int line = 0) {
        log(LogLevel::ERROR, message, file, line);
    }

    void fatal(const std::string& message, const char* file = "", int line = 0) {
        log(LogLevel::FATAL, message, file, line);
    }

    // 关闭日志
    void close() {
        std::lock_guard<std::mutex> lock(mutex);
        if (logFile.is_open()) {
            logFile.close();
        }
        isInitialized = false;
    }

    ~Logger() {
        close();
    }

private:
    Logger() : isInitialized(false), minLogLevel(LogLevel::INFO) {}
    
    // 删除复制和移动构造/赋值
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    // 获取当前时间戳
    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        
        std::tm tm_now;
        localtime_r(&time_t_now, &tm_now);
        
        std::ostringstream oss;
        oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
        oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        
        return oss.str();
    }

    // 将日志级别转换为字符串
    std::string getLevelString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG:   return "[DEBUG]";
            case LogLevel::INFO:    return "[INFO] ";
            case LogLevel::WARNING: return "[WARN] ";
            case LogLevel::ERROR:   return "[ERROR]";
            case LogLevel::FATAL:   return "[FATAL]";
            default:                return "[?????]";
        }
    }

    std::mutex mutex;
    std::ofstream logFile;
    bool isInitialized;
    LogLevel minLogLevel;
};

// 宏简化日志调用
#define LOG_DEBUG(msg) Logger::getInstance().debug(msg, __FILE__, __LINE__)
#define LOG_INFO(msg) Logger::getInstance().info(msg, __FILE__, __LINE__)
#define LOG_WARNING(msg) Logger::getInstance().warning(msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) Logger::getInstance().error(msg, __FILE__, __LINE__)
#define LOG_FATAL(msg) Logger::getInstance().fatal(msg, __FILE__, __LINE__)
