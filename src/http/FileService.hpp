#pragma once
#include <string>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <vector>
#include "../core/Logger.hpp"
#include "../core/Config.hpp"

namespace fs = std::filesystem;

// 文件缓存项
struct CacheEntry {
    std::string content;
    std::string mimeType;
    std::chrono::steady_clock::time_point lastAccess;
    size_t size;
    
    CacheEntry(std::string content, std::string mimeType)
        : content(std::move(content)), mimeType(std::move(mimeType)), 
          lastAccess(std::chrono::steady_clock::now()), size(this->content.size()) {}
    
    void updateLastAccess() {
        lastAccess = std::chrono::steady_clock::now();
    }
};

class FileService {
public:
    static FileService& getInstance() {
        static FileService instance;
        return instance;
    }

    // 初始化文件服务
    bool init(const std::string& rootDir) {
        rootDirectory = rootDir;
        
        // 检查根目录是否存在且可访问
        if (!fs::exists(rootDirectory) || !fs::is_directory(rootDirectory)) {
            LOG_ERROR(fmt::format("根目录不存在或无法访问: {}", rootDirectory));
            return false;
        }
        
        // 初始化MIME类型映射
        initMimeTypes();
        
        // 从配置中读取缓存设置
        auto& config = Config::getInstance();
        maxCacheSize = config.getInt("file_cache_max_size", 100) * 1024 * 1024; // 默认100MB
        maxCacheEntries = config.getInt("file_cache_max_entries", 1000);
        maxCacheFileSize = config.getInt("file_cache_max_file_size", 5) * 1024 * 1024; // 默认最大文件5MB
        
        LOG_INFO(fmt::format("文件服务初始化完成，根目录: {}", rootDirectory));
        return true;
    }

    // 获取文件的MIME类型
    std::string getMimeType(const std::string& path) {
        std::string ext = getFileExtension(path);
        auto it = mimeTypes.find(ext);
        if (it != mimeTypes.end()) {
            return it->second;
        }
        return "application/octet-stream"; // 默认二进制类型
    }

    // 根据请求路径获取文件内容
    struct FileResponse {
        std::string statusCode;
        std::string content;
        std::string mimeType;
        
        FileResponse(std::string status, std::string content = "", std::string mime = "")
            : statusCode(std::move(status)), content(std::move(content)), mimeType(std::move(mime)) {}
    };
    
    FileResponse getFileContent(const std::string& requestPath) {
        // 处理路径，防止路径遍历攻击
        std::string path = sanitizePath(requestPath);
        
        // 确保路径不以"/"开头，因为路径会被拼接到rootDirectory后面
        if (!path.empty() && path[0] == '/') {
            path = path.substr(1);
        }
        
        // 构建完整路径
        std::string fullPath = buildFullPath(rootDirectory, path);
        
        // 首先尝试从缓存中获取文件内容
        auto cachedContent = getCachedContent(fullPath);
        if (cachedContent.has_value()) {
            const CacheEntry& entry = cachedContent.value();
            return {"200", entry.content, entry.mimeType};
        }
        
        // 检查路径是否存在
        if (!fs::exists(fullPath)) {
            // 文件不存在
            return {"404", "", ""};
        }
        
        // 如果是目录且配置允许列出目录
        if (fs::is_directory(fullPath) && Config::getInstance().getBool("allow_directory_listing", false)) {
            // 确保请求路径以/开头
            std::string absolutePath = path;
            if (absolutePath.empty() || absolutePath[0] != '/') {
                absolutePath = "/" + absolutePath;
            }
            // 确保路径以/结尾
            if (!absolutePath.empty() && absolutePath.back() != '/') {
                absolutePath += '/';
            }
            
            std::string listing = generateDirectoryListing(fullPath, absolutePath);
            return {"200", listing, "text/html"};
        }
        
        // 读取文件内容
        try {
            if (fs::is_regular_file(fullPath)) {
                auto fileSize = fs::file_size(fullPath);
                std::string mimeType = getMimeType(fullPath);
                
                // 对于超大文件，不缓存直接读取
                if (fileSize > maxCacheFileSize) {
                    auto [status, content] = readLargeFile(fullPath);
                    return {status, content, mimeType};
                }
                
                // 读取文件内容并缓存
                std::string content = readFile(fullPath, fileSize);
                
                // 缓存文件内容，如果文件不太大
                cacheFile(fullPath, content, mimeType);
                
                return {"200", content, mimeType};
            } else {
                // 不是文件
                return {"404", "", ""};
            }
        } catch (const std::exception& e) {
            // 服务器错误
            return {"500", "", ""};
        }
    }
    
    // 清除文件缓存
    void clearCache() {
        std::lock_guard<std::mutex> lock(cacheMutex);
        fileCache.clear();
        currentCacheSize = 0;
    }

private:
    FileService() {
        // 设置默认文件列表
        defaultFiles = {"index.html", "index.htm", "default.html"};
        currentCacheSize = 0;
    }
    
    // 删除复制和移动构造/赋值
    FileService(const FileService&) = delete;
    FileService& operator=(const FileService&) = delete;
    FileService(FileService&&) = delete;
    FileService& operator=(FileService&&) = delete;

    // 构建完整路径
    std::string buildFullPath(const std::string& base, const std::string& relativePath) {
        std::string fullPath = base;
        if (!fullPath.empty() && fullPath.back() != '/') {
            fullPath += '/';
        }
        fullPath += relativePath;
        return fullPath;
    }
    
    // 尝试找到默认文件
    std::optional<std::string> findDefaultFile(const std::string& dirPath) {
        for (const auto& defaultFile : defaultFiles) {
            std::string indexPath = dirPath;
            if (indexPath.back() != '/') {
                indexPath += '/';
            }
            indexPath += defaultFile;
            
            if (fs::exists(indexPath) && fs::is_regular_file(indexPath)) {
                return indexPath;
            }
        }
        return std::nullopt;
    }
    
    // 高效读取文件
    std::string readFile(const std::string& fullPath, uintmax_t fileSize) {
        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        if (!file) {
            throw std::runtime_error("Cannot open file");
        }
        
        std::string content;
        content.resize(fileSize);
        
        // 将文件指针移动到开头并一次性读取文件
        file.seekg(0, std::ios::beg);
        file.read(&content[0], fileSize);
        
        return content;
    }
    
    // 读取大文件（不缓存）
    std::pair<std::string, std::string> readLargeFile(const std::string& fullPath) {
        std::ifstream file(fullPath, std::ios::binary);
        if (!file) {
            return {"500", ""};
        }
        
        std::string content;
        constexpr size_t bufferSize = 8192;
        char buffer[bufferSize];
        
        while (file) {
            file.read(buffer, bufferSize);
            content.append(buffer, file.gcount());
        }
        
        return {"200", content};
    }
    
    // 从缓存获取文件内容
    std::optional<std::reference_wrapper<const CacheEntry>> getCachedContent(const std::string& path) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = fileCache.find(path);
        if (it != fileCache.end()) {
            it->second.updateLastAccess();
            return std::cref(it->second);
        }
        return std::nullopt;
    }
    
    // 缓存文件
    void cacheFile(const std::string& path, const std::string& content, const std::string& mimeType) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        
        // 检查是否需要进行缓存管理
        if (fileCache.size() >= maxCacheEntries || currentCacheSize + content.size() > maxCacheSize) {
            evictCache(content.size());
        }
        
        // 添加到缓存
        auto [it, success] = fileCache.emplace(path, CacheEntry(content, mimeType));
        if (success) {
            currentCacheSize += content.size();
        }
    }
    
    // 删除最久未访问的缓存项
    void evictCache(size_t requiredSpace) {
        if (fileCache.empty()) return;
        
        // 按最后访问时间排序
        std::vector<std::pair<std::string, std::reference_wrapper<CacheEntry>>> entries;
        entries.reserve(fileCache.size());
        
        for (auto& entry : fileCache) {
            entries.emplace_back(entry.first, std::ref(entry.second));
        }
        
        // 按最后访问时间排序（从旧到新）
        std::sort(entries.begin(), entries.end(), 
            [](const auto& a, const auto& b) {
                return a.second.get().lastAccess < b.second.get().lastAccess;
            });
        
        // 删除足够的缓存项
        size_t freedSpace = 0;
        auto it = entries.begin();
        while (freedSpace < requiredSpace && it != entries.end()) {
            const auto& key = it->first;
            const auto& entry = it->second.get();
            
            freedSpace += entry.size;
            currentCacheSize -= entry.size;
            fileCache.erase(key);
            
            ++it;
        }
    }

    // 初始化MIME类型映射
    void initMimeTypes() {
        mimeTypes = {
            {".html", "text/html"},
            {".htm", "text/html"},
            {".css", "text/css"},
            {".js", "application/javascript"},
            {".json", "application/json"},
            {".txt", "text/plain"},
            {".png", "image/png"},
            {".jpg", "image/jpeg"},
            {".jpeg", "image/jpeg"},
            {".gif", "image/gif"},
            {".svg", "image/svg+xml"},
            {".ico", "image/x-icon"},
            {".pdf", "application/pdf"},
            {".zip", "application/zip"},
            {".xml", "application/xml"},
            {".mp4", "video/mp4"},
            {".webm", "video/webm"},
            {".mp3", "audio/mpeg"},
            {".wav", "audio/wav"},
            {".ogg", "audio/ogg"},
            {".woff", "font/woff"},
            {".woff2", "font/woff2"},
            {".ttf", "font/ttf"},
            {".eot", "application/vnd.ms-fontobject"},
            {".otf", "font/otf"}
        };
    }

    // 获取文件扩展名
    std::string getFileExtension(const std::string& path) {
        fs::path p(path);
        std::string ext = p.extension().string();
        
        // 将扩展名转为小写以进行不区分大小写的比较
        std::transform(ext.begin(), ext.end(), ext.begin(), 
            [](unsigned char c){ return std::tolower(c); });
        return ext;
    }

    // 净化路径，防止路径遍历攻击
    std::string sanitizePath(const std::string& path) {
        std::string result;
        result.reserve(path.size());
        bool prevSlash = false;
        
        for (char c : path) {
            if (c == '/' || c == '\\') {
                if (!prevSlash) {
                    result.push_back('/'); // 将所有路径分隔符统一为 /
                    prevSlash = true;
                }
                // 跳过连续的斜杠
            } else {
                result.push_back(c);
                prevSlash = false;
            }
        }
        
        // 处理 ".." 和 "."，使用更高效的方法
        std::vector<std::string_view> segments;
        size_t start = 0;
        size_t end;
        
        // 手动分割字符串，避免使用 istringstream
        while ((end = result.find('/', start)) != std::string::npos) {
            std::string_view segment(result.data() + start, end - start);
            if (segment == "..") {
                if (!segments.empty()) {
                    segments.pop_back();
                }
            } else if (segment != "." && !segment.empty()) {
                segments.push_back(segment);
            }
            start = end + 1;
        }
        
        // 处理最后一个分段
        if (start < result.size()) {
            std::string_view segment(result.data() + start, result.size() - start);
            if (segment == "..") {
                if (!segments.empty()) {
                    segments.pop_back();
                }
            } else if (segment != "." && !segment.empty()) {
                segments.push_back(segment);
            }
        }
        
        // 重建路径
        std::string sanitized;
        sanitized.reserve(path.size());
        sanitized.push_back('/');
        
        for (size_t i = 0; i < segments.size(); ++i) {
            sanitized.append(segments[i]);
            if (i < segments.size() - 1) {
                sanitized.push_back('/');
            }
        }
        
        return sanitized;
    }

    // 生成目录列表HTML
    std::string generateDirectoryListing(const std::string& dirPath, const std::string& requestPath) {
        std::ostringstream html;
        html << "<!DOCTYPE html>\n";
        html << "<html>\n<head>\n";
        html << "<meta charset=\"UTF-8\">\n";
        html << "<title>目录列表: " << requestPath << "</title>\n";
        html << "<style>\n";
        html << "body { font-family: Arial, sans-serif; margin: 20px; }\n";
        html << "h1 { color: #333; }\n";
        html << "ul { list-style-type: none; padding: 0; }\n";
        html << "li { margin: 5px 0; }\n";
        html << "a { text-decoration: none; color: #0066cc; }\n";
        html << "a:hover { text-decoration: underline; }\n";
        html << ".directory { font-weight: bold; }\n";
        html << "</style>\n";
        html << "</head>\n<body>\n";
        html << "<h1>目录: " << requestPath << "</h1>\n";
        html << "<ul>\n";
        
        // 添加返回上级目录的链接
        if (requestPath != "/" && requestPath != "") {
            // 确保路径以/结尾
            std::string normalizedPath = requestPath;
            if (!normalizedPath.empty() && normalizedPath.back() != '/') {
                normalizedPath += '/';
            }
            
            // 移除最后一个/
            if (normalizedPath.length() > 1) {
                normalizedPath.pop_back();
            }
            
            // 查找倒数第二个/
            size_t pos = normalizedPath.find_last_of('/');
            std::string parentDir = "/";
            
            if (pos != std::string::npos) {
                // 确保最终路径以/结尾
                parentDir = normalizedPath.substr(0, pos + 1);
                if (parentDir.empty()) {
                    parentDir = "/";
                }
            }
            
            html << "<li><a href=\"" << parentDir << "\">..</a> (上级目录)</li>\n";
            LOG_DEBUG(fmt::format("生成目录列表，当前路径：{}，父目录：{}", requestPath, parentDir));
        }
        
        // 列出所有文件和目录
        try {
            std::vector<fs::directory_entry> entries;
            for (const auto& entry : fs::directory_iterator(dirPath)) {
                entries.push_back(entry);
            }
            
            // 先排序：目录在前，文件在后，按名称字母顺序排序
            std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
                if (fs::is_directory(a) && !fs::is_directory(b)) return true;
                if (!fs::is_directory(a) && fs::is_directory(b)) return false;
                return a.path().filename().string() < b.path().filename().string();
            });
            
            for (const auto& entry : entries) {
                std::string name = entry.path().filename().string();
                std::string url = requestPath;
                if (url.back() != '/') url += '/';
                url += name;
                
                if (fs::is_directory(entry)) {
                    html << "<li><a class=\"directory\" href=\"" << url << "/\">" 
                         << name << "/</a></li>\n";
                } else {
                    // 对于文件，显示其大小
                    auto fileSize = fs::file_size(entry);
                    std::string sizeStr;
                    
                    if (fileSize < 1024) {
                        sizeStr = fmt::format("{} B", fileSize);
                    } else if (fileSize < 1024 * 1024) {
                        sizeStr = fmt::format("{:.1f} KB", fileSize / 1024.0);
                    } else if (fileSize < 1024 * 1024 * 1024) {
                        sizeStr = fmt::format("{:.1f} MB", fileSize / (1024.0 * 1024));
                    } else {
                        sizeStr = fmt::format("{:.1f} GB", fileSize / (1024.0 * 1024 * 1024));
                    }
                    
                    html << "<li><a href=\"" << url << "\">" 
                         << name << "</a> (" << sizeStr << ")</li>\n";
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR(fmt::format("生成目录列表时发生错误: {}", e.what()));
            html << "<li>读取目录内容时发生错误</li>\n";
        }
        
        html << "</ul>\n";
        html << "<hr>\n";
        html << "<p>C++20 HTTP Server</p>\n";
        html << "</body>\n</html>";
        
        return html.str();
    }

    std::string rootDirectory;
    std::unordered_map<std::string, std::string> mimeTypes;
    std::vector<std::string> defaultFiles;
    
    // 文件缓存相关
    std::unordered_map<std::string, CacheEntry> fileCache;
    std::mutex cacheMutex;
    size_t currentCacheSize{0};
    size_t maxCacheSize{100 * 1024 * 1024}; // 默认100MB
    size_t maxCacheEntries{1000};
    size_t maxCacheFileSize{5 * 1024 * 1024}; // 默认最大文件5MB
};
