#pragma once
#include <algorithm>
#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <fmt/format.h>

class Config {
public:
    static Config& getInstance() {
        static Config instance;
        return instance;
    }

    // 从配置文件加载配置
    bool loadFromFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            fmt::print("无法打开配置文件: {}\n", filename);
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            // 跳过注释和空行
            if (line.empty() || line[0] == '#') {
                continue;
            }

            // 解析配置项
            std::istringstream iss(line);
            std::string key, value;
            if (std::getline(iss, key, '=') && std::getline(iss, value)) {
                // 去除前后空格
                key = trim(key);
                value = trim(value);
                
                // 存储配置
                configData[key] = value;
                fmt::print("配置项: {} = {}\n", key, value);
            }
        }

        return true;
    }

    // 获取字符串配置项，如果不存在则返回默认值
    std::string getString(const std::string& key, const std::string& defaultValue = "") const {
        auto it = configData.find(key);
        if (it != configData.end()) {
            return it->second;
        }
        return defaultValue;
    }

    // 获取整数配置项，如果不存在则返回默认值
    int getInt(const std::string& key, int defaultValue = 0) const {
        auto it = configData.find(key);
        if (it != configData.end()) {
            try {
                return std::stoi(it->second);
            } catch (...) {
                return defaultValue;
            }
        }
        return defaultValue;
    }

    // 获取浮点数配置项，如果不存在则返回默认值
    double getDouble(const std::string& key, double defaultValue = 0.0) const {
        auto it = configData.find(key);
        if (it != configData.end()) {
            try {
                return std::stod(it->second);
            } catch (...) {
                return defaultValue;
            }
        }
        return defaultValue;
    }

    // 获取布尔配置项，如果不存在则返回默认值
    bool getBool(const std::string& key, bool defaultValue = false) const {
        auto it = configData.find(key);
        if (it != configData.end()) {
            std::string value = it->second;
            // 转换为小写
            std::transform(value.begin(), value.end(), value.begin(), 
                           [](unsigned char c){ return std::tolower(c); });
            
            if (value == "true" || value == "yes" || value == "1") {
                return true;
            } else if (value == "false" || value == "no" || value == "0") {
                return false;
            }
        }
        return defaultValue;
    }

private:
    Config() {} // 私有构造函数
    ~Config() {}
    
    // 删除复制和移动构造/赋值
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    Config(Config&&) = delete;
    Config& operator=(Config&&) = delete;

    // 去除字符串前后空格
    std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) {
            return "";
        }
        size_t last = str.find_last_not_of(" \t\n\r");
        return str.substr(first, last - first + 1);
    }

    std::unordered_map<std::string, std::string> configData;
};
